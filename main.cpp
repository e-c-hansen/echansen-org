#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <random>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

// Global state for graceful shutdown and stats
std::atomic<bool> keep_running{true};
std::atomic<uint64_t> global_request_count{0};
int server_fd = -1;
const auto server_start_time = std::chrono::steady_clock::now();

// Mutex for synchronized logging to prevent mixed console output
std::mutex log_mutex;

// Configuration variables
std::string serve_port = "8080";
std::string serve_dir = "./public";

// ---- Root-write terminal configuration ----------------------------------
// The hidden in-browser terminal can let an authenticated "root" user create
// Markdown posts. This is intentionally gated behind BOTH a shared password
// and a source-IP allowlist, and is only active when the C++ backend serves
// the site (it is inert on static hosts like GitHub Pages).
std::string root_password = "";              // empty => writes fully disabled
std::vector<std::string> root_allowed_ips;   // empty => no host may write
const std::string posts_subdir = "posts";    // writable dir, under serve_dir
const int TOKEN_TTL_SECONDS = 3600;          // root session lifetime

// In-memory set of valid session tokens -> expiry timestamps.
std::mutex token_mutex;
std::map<std::string, std::chrono::steady_clock::time_point> active_tokens;

// Quotes collection for the /quote endpoint
const std::vector<std::string> dev_quotes = {
    "\"C++ is designed to be a systems programming language. It is a language "
    "for writing things that must be fast and run in small amounts of "
    "memory.\" — Bjarne Stroustrup",
    "\"There are only two hard things in Computer Science: cache invalidation "
    "and naming things.\" — Phil Karlton",
    "\"Simplicity is prerequisite for reliability.\" — Edsger W. Dijkstra",
    "\"The best way to predict the future is to invent it.\" — Alan Kay",
    "\"A language that doesn't affect your way of thinking about programming, "
    "is not worth knowing.\" — Alan Perlis",
    "\"Computers are good at following instructions, but not at reading your "
    "mind.\" — Donald Knuth",
    "\"The first rule of functions is that they should be small. The second "
    "rule of functions is that they should be smaller than that.\" — Robert C. "
    "Martin",
    "\"Any fool can write code that a computer can understand. Good "
    "programmers write code that humans can understand.\" — Martin Fowler",
    "\"Measure twice, cut once. Profile twice, optimize once.\" — Developer "
    "Saying",
    "\"Chemistry is the study of matter, but I prefer to see it as the study "
    "of change.\" — Walter White (PhD Chemistry)",
    "\"An agent is only as good as its adversarial post-training.\" — ML "
    "Proverb",
    "\"If you think cryptography is the solution to your problem, then you "
    "don't understand your problem and you don't understand cryptography.\" — "
    "Roger Needham"};

// Markdown resume is now dynamically loaded from public/resume.md to avoid recompiles

// Helper function to trim whitespaces
std::string trim(const std::string &str) {
  size_t first = str.find_first_not_of(" \t\r\n");
  if (std::string::npos == first)
    return "";
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first + 1));
}

// Generate standard compliant UUIDv4
std::string generate_uuid() {
  static std::random_device rd;
  static std::mt19937 gen(rd());
  std::uniform_int_distribution<> dis(0, 15);
  std::uniform_int_distribution<> dis2(8, 11);

  std::stringstream ss;
  ss << std::hex;
  for (int i = 0; i < 8; i++)
    ss << dis(gen);
  ss << "-";
  for (int i = 0; i < 4; i++)
    ss << dis(gen);
  ss << "-4";
  for (int i = 0; i < 3; i++)
    ss << dis(gen);
  ss << "-";
  ss << dis2(gen);
  for (int i = 0; i < 3; i++)
    ss << dis(gen);
  ss << "-";
  for (int i = 0; i < 12; i++)
    ss << dis(gen);
  return ss.str();
}

// Determine MIME type based on file extension
std::string get_mime_type(const std::string &filename) {
  size_t dot_idx = filename.find_last_of('.');
  if (dot_idx == std::string::npos)
    return "application/octet-stream";
  std::string ext = filename.substr(dot_idx);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".html" || ext == ".htm")
    return "text/html; charset=utf-8";
  if (ext == ".css")
    return "text/css; charset=utf-8";
  if (ext == ".js")
    return "application/javascript; charset=utf-8";
  if (ext == ".png")
    return "image/png";
  if (ext == ".jpg" || ext == ".jpeg")
    return "image/jpeg";
  if (ext == ".gif")
    return "image/gif";
  if (ext == ".svg")
    return "image/svg+xml";
  if (ext == ".ico")
    return "image/x-icon";
  if (ext == ".txt")
    return "text/plain; charset=utf-8";
  if (ext == ".json")
    return "application/json; charset=utf-8";
  return "application/octet-stream";
}

// Signal handler for graceful termination
void signal_handler(int sig) {
  if (sig == SIGINT || sig == SIGTERM) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cout << "\n\033[1;33m[SYSTEM] Signal received. Shutting down "
                 "gracefully...\033[0m"
              << std::endl;
    keep_running = false;
    if (server_fd != -1) {
      close(server_fd);
      server_fd = -1;
    }
  }
}

// Thread-safe request logging
void log_request(const std::string &ip, const std::string &method,
                 const std::string &host, const std::string &path,
                 int status_code, double elapsed_ms) {
  std::lock_guard<std::mutex> lock(log_mutex);

  // Get formatted current time
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  std::tm tm_now;
  localtime_r(&time_t_now, &tm_now);

  // Color code based on HTTP response status
  std::string color = "\033[0m"; // Default
  if (status_code >= 200 && status_code < 300)
    color = "\033[1;32m"; // Green
  else if (status_code >= 300 && status_code < 400)
    color = "\033[1;34m"; // Blue
  else if (status_code >= 400 && status_code < 500)
    color = "\033[1;33m"; // Yellow
  else if (status_code >= 500)
    color = "\033[1;31m"; // Red

  std::cout << "[" << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S") << "] " << ip
            << " "
            << "\033[1;35m" << method << "\033[0m "
            << "\033[1;36m" << host << "\033[0m " << path << " -> " << color
            << status_code << "\033[0m "
            << "(" << std::fixed << std::setprecision(2) << elapsed_ms << "ms)"
            << std::endl;
}

// Send error response
void send_error(int client_fd, int status_code, const std::string &status_text,
                const std::string &message) {
  std::string body =
      "<html><head><title>" + std::to_string(status_code) + " " + status_text +
      "</"
      "title><style>body{font-family:sans-serif;text-align:center;padding:50px;"
      "background:#0f172a;color:#cbd5e1;}" +
      "h1{color:#f43f5e;}p{color:#94a3b8;}</style></head><body><h1>" +
      std::to_string(status_code) + " " + status_text + "</h1><p>" + message +
      "</p><hr/><p style='font-size:0.8em;'>echansen C++ "
      "Server</p></body></html>";

  std::stringstream ss;
  ss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
     << "Content-Type: text/html; charset=utf-8\r\n"
     << "Content-Length: " << body.size() << "\r\n"
     << "Connection: close\r\n\r\n"
     << body;

  std::string response = ss.str();
  write(client_fd, response.c_str(), response.size());
}

// Send a minimal response with explicit content type (used by the terminal API)
void send_simple(int client_fd, int status_code, const std::string &status_text,
                 const std::string &content_type, const std::string &body) {
  std::stringstream ss;
  ss << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
     << "Content-Type: " << content_type << "\r\n"
     << "Content-Length: " << body.size() << "\r\n"
     << "Cache-Control: no-store\r\n"
     << "Connection: close\r\n\r\n"
     << body;
  std::string response = ss.str();
  write(client_fd, response.c_str(), response.size());
}

// Is this source IP permitted to use root-write features?
bool ip_allowed(const std::string &ip) {
  for (const auto &a : root_allowed_ips)
    if (a == ip)
      return true;
  return false;
}

// Constant-time string comparison to avoid leaking the password via timing.
bool const_time_eq(const std::string &a, const std::string &b) {
  if (a.size() != b.size())
    return false;
  unsigned char r = 0;
  for (size_t i = 0; i < a.size(); ++i)
    r |= (unsigned char)(a[i] ^ b[i]);
  return r == 0;
}

// Mint and register a fresh root session token.
std::string make_token() {
  std::string t = generate_uuid();
  std::lock_guard<std::mutex> lock(token_mutex);
  active_tokens[t] =
      std::chrono::steady_clock::now() + std::chrono::seconds(TOKEN_TTL_SECONDS);
  return t;
}

// Validate (and lazily expire) a session token.
bool token_valid(const std::string &t) {
  if (t.empty())
    return false;
  std::lock_guard<std::mutex> lock(token_mutex);
  auto it = active_tokens.find(t);
  if (it == active_tokens.end())
    return false;
  if (std::chrono::steady_clock::now() > it->second) {
    active_tokens.erase(it);
    return false;
  }
  return true;
}

// Restrict post filenames to a safe character set (no traversal, no slashes).
bool safe_post_name(const std::string &n) {
  if (n.empty() || n.size() > 128)
    return false;
  if (n.find("..") != std::string::npos)
    return false;
  for (char c : n) {
    if (!(std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-'))
      return false;
  }
  return true;
}

// Look up a header in the parsed (lowercase-keyed) map, returning "" if absent.
std::string header_val(const std::map<std::string, std::string> &headers,
                       const std::string &key) {
  auto it = headers.find(key);
  return it == headers.end() ? std::string() : it->second;
}

// Hidden-terminal / root-write API. Handles its own HTTP methods and writes the
// full response. Returns the HTTP status code (for logging).
int handle_terminal_api(int client_fd, const std::string &method,
                        const std::string &subpath, const std::string &real_ip,
                        const std::map<std::string, std::string> &headers,
                        const std::string &body) {
  const std::string json_ct = "application/json; charset=utf-8";

  // GET /api/term/info  -> capabilities for the current caller
  if (method == "GET" && subpath == "/info") {
    bool writes_enabled = !root_password.empty();
    std::string b = std::string("{") +
                    "\"backend\":true," +
                    "\"writes_enabled\":" + (writes_enabled ? "true" : "false") +
                    ",\"ip_allowed\":" + (ip_allowed(real_ip) ? "true" : "false") +
                    ",\"client_ip\":\"" + real_ip + "\"," +
                    "\"writable_dir\":\"/posts\"}";
    send_simple(client_fd, 200, "OK", json_ct, b);
    return 200;
  }

  // GET /api/term/posts -> JSON array of the .md files currently in posts/
  if (method == "GET" && subpath == "/posts") {
    std::string dir = serve_dir + "/" + posts_subdir;
    std::string arr = "[";
    DIR *d = opendir(dir.c_str());
    if (d) {
      struct dirent *e;
      bool first = true;
      while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;
        if (n == "." || n == "..")
          continue;
        if (n.size() < 4 || n.substr(n.size() - 3) != ".md")
          continue;
        if (!first)
          arr += ",";
        first = false;
        arr += "\"" + n + "\"";
      }
      closedir(d);
    }
    arr += "]";
    send_simple(client_fd, 200, "OK", json_ct, arr);
    return 200;
  }

  // POST /api/term/login  (X-Root-Password header) -> { token }
  if (method == "POST" && subpath == "/login") {
    if (root_password.empty()) {
      send_simple(client_fd, 403, "Forbidden", json_ct,
                  "{\"error\":\"root writes are disabled on this server\"}");
      return 403;
    }
    if (!ip_allowed(real_ip)) {
      send_simple(client_fd, 403, "Forbidden", json_ct,
                  "{\"error\":\"source IP not authorized\"}");
      return 403;
    }
    if (!const_time_eq(header_val(headers, "x-root-password"), root_password)) {
      send_simple(client_fd, 401, "Unauthorized", json_ct,
                  "{\"error\":\"invalid password\"}");
      return 401;
    }
    std::string tok = make_token();
    send_simple(client_fd, 200, "OK", json_ct, "{\"token\":\"" + tok + "\"}");
    return 200;
  }

  // POST /api/term/write  (Authorization: Bearer, X-File-Name, body=content)
  if (method == "POST" && subpath == "/write") {
    if (root_password.empty() || !ip_allowed(real_ip)) {
      send_simple(client_fd, 403, "Forbidden", json_ct,
                  "{\"error\":\"not authorized\"}");
      return 403;
    }
    std::string auth = header_val(headers, "authorization");
    std::string tok = (auth.rfind("Bearer ", 0) == 0) ? auth.substr(7) : "";
    if (!token_valid(tok)) {
      send_simple(client_fd, 401, "Unauthorized", json_ct,
                  "{\"error\":\"invalid or expired session\"}");
      return 401;
    }
    std::string name = header_val(headers, "x-file-name");
    if (name.size() < 4 || name.substr(name.size() - 3) != ".md")
      name += ".md";
    if (!safe_post_name(name)) {
      send_simple(client_fd, 400, "Bad Request", json_ct,
                  "{\"error\":\"invalid filename\"}");
      return 400;
    }
    std::string dir = serve_dir + "/" + posts_subdir;
    mkdir(dir.c_str(), 0755); // no-op if it already exists
    std::ofstream out(dir + "/" + name, std::ios::binary | std::ios::trunc);
    if (!out) {
      send_simple(client_fd, 500, "Internal Server Error", json_ct,
                  "{\"error\":\"could not write file\"}");
      return 500;
    }
    out << body;
    out.close();
    send_simple(client_fd, 200, "OK", json_ct,
                "{\"ok\":true,\"path\":\"/" + posts_subdir + "/" + name + "\"}");
    return 200;
  }

  send_simple(client_fd, 404, "Not Found", json_ct,
              "{\"error\":\"unknown terminal endpoint\"}");
  return 404;
}

// Connection handler executed on separate threads
void handle_client(int client_fd, std::string client_ip) {
  auto start_time = std::chrono::high_resolution_clock::now();
  global_request_count++;

  // Set connection timeouts (5 seconds for read/write to prevent hung sockets)
  struct timeval tv;
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
  setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

  std::string request_raw;
  char buf[2048];
  ssize_t bytes_read;
  size_t header_end = std::string::npos;

  // Read socket buffer until end of HTTP headers (\r\n\r\n)
  while ((bytes_read = read(client_fd, buf, sizeof(buf) - 1)) > 0) {
    request_raw.append(buf, bytes_read);
    header_end = request_raw.find("\r\n\r\n");
    if (header_end != std::string::npos) {
      break;
    }
    if (request_raw.size() > 16384) { // Guard against oversized header attacks
      break;
    }
  }

  if (request_raw.empty()) {
    close(client_fd);
    return;
  }

  // Split the header block from any body bytes already buffered.
  std::string headers_blob = (header_end == std::string::npos)
                                 ? request_raw
                                 : request_raw.substr(0, header_end);
  std::string body =
      (header_end == std::string::npos) ? "" : request_raw.substr(header_end + 4);

  // Basic HTTP request parsing
  std::stringstream req_stream(headers_blob);
  std::string method, path, version;
  std::string line;
  if (std::getline(req_stream, line)) {
    std::stringstream first_line(line);
    first_line >> method >> path >> version;
  }

  // Parse all headers into a lowercase-keyed map.
  std::map<std::string, std::string> headers;
  while (std::getline(req_stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    line = trim(line);
    if (line.empty()) {
      continue;
    }
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string header_name = trim(line.substr(0, colon));
      std::transform(header_name.begin(), header_name.end(),
                     header_name.begin(), ::tolower);
      headers[header_name] = trim(line.substr(colon + 1));
    }
  }

  std::string host_header =
      headers.count("host") ? headers["host"] : "unknown";

  // Read the remainder of any request body (bounded) for POST uploads.
  size_t content_length = 0;
  if (headers.count("content-length")) {
    try {
      content_length = std::stoul(headers["content-length"]);
    } catch (...) {
      content_length = 0;
    }
  }
  const size_t MAX_BODY = 512 * 1024;
  if (content_length > MAX_BODY) {
    content_length = MAX_BODY;
  }
  while (body.size() < content_length) {
    bytes_read = read(client_fd, buf, sizeof(buf) - 1);
    if (bytes_read <= 0) {
      break;
    }
    body.append(buf, bytes_read);
  }

  // Resolve the real client IP. Behind a loopback reverse proxy (Caddy or the
  // iptables redirect) the peer is 127.0.0.1, so trust the first X-Forwarded-For
  // hop in that case only; otherwise use the genuine socket peer address. This
  // prevents direct callers from spoofing their source IP for the allowlist.
  std::string real_ip = client_ip;
  if ((client_ip == "127.0.0.1" || client_ip == "::1") &&
      headers.count("x-forwarded-for")) {
    std::string xff = headers["x-forwarded-for"];
    size_t comma = xff.find(',');
    real_ip = trim(comma == std::string::npos ? xff : xff.substr(0, comma));
  }

  // Standard tracking stats
  int status_code = 200;

  // ---- Hidden terminal / root-write API (handles its own HTTP methods) ----
  if (path.rfind("/api/term", 0) == 0) {
    std::string subpath = path.substr(9); // strip "/api/term"
    if (subpath.empty()) {
      subpath = "/";
    }
    status_code = handle_terminal_api(client_fd, method, subpath, real_ip,
                                      headers, body);
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();
    log_request(real_ip, method, host_header, path, status_code, elapsed);
    close(client_fd);
    return;
  }

  // Check request method validity
  if (method != "GET") {
    status_code = 405;
    send_error(client_fd, 405, "Method Not Allowed",
               "This web server only supports HTTP GET requests.");
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed =
        std::chrono::duration<double, std::milli>(end_time - start_time)
            .count();
    log_request(client_ip, method, host_header, path, status_code, elapsed);
    close(client_fd);
    return;
  }

  // Prevent Path Traversal security vulnerabilities
  if (path.find("..") != std::string::npos) {
    status_code = 403;
    send_error(client_fd, 403, "Forbidden",
               "Path traversal is strictly prohibited.");
    auto end_time = std::chrono::high_resolution_clock::now();
    double elapsed =
        std::chrono::duration<double, std::milli>(end_time - start_time)
            .count();
    log_request(client_ip, method, host_header, path, status_code, elapsed);
    close(client_fd);
    return;
  }

  // Determine domain type (Main website vs Curl subdomain)
  std::string host_lower = host_header;
  std::transform(host_lower.begin(), host_lower.end(), host_lower.begin(),
                 ::tolower);

  bool is_curl_subdomain = (host_lower.rfind("api.", 0) == 0) ||
                           (host_lower.rfind("curl.", 0) == 0) ||
                           (path.rfind("/api", 0) == 0) ||
                           (path.rfind("/curl", 0) == 0);

  if (is_curl_subdomain) {
    // Strip path prefix if mapped via path-based fallback rather than DNS
    // subdomain
    std::string api_route = path;
    if (api_route.rfind("/api", 0) == 0) {
      api_route = api_route.substr(4);
    } else if (api_route.rfind("/curl", 0) == 0) {
      api_route = api_route.substr(5);
    }
    if (api_route.empty())
      api_route = "/";

    std::string content_type = "text/markdown; charset=utf-8";
    std::string body;

    if (api_route == "/" || api_route == "/resume") {
      std::string resume_path = serve_dir + "/resume.md";
      std::ifstream file(resume_path, std::ios::binary);
      if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        body = buffer.str();
      } else {
        body = "Error: Markdown resume file (" + resume_path + ") could not be loaded from disk.\n";
      }
    } else if (api_route == "/quote") {
      static std::random_device rd;
      static std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, dev_quotes.size() - 1);
      body = dev_quotes[dis(gen)] + "\n";
    } else if (api_route == "/uuid") {
      body = generate_uuid() + "\n";
    } else if (api_route == "/coin") {
      static std::random_device rd;
      static std::mt19937 gen(rd());
      std::uniform_int_distribution<> dis(0, 1);
      body = (dis(gen) == 0 ? "Heads\n" : "Tails\n");
    } else if (api_route == "/stats") {
      auto now = std::chrono::steady_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                          now - server_start_time)
                          .count();

      long long days = duration / 86400;
      long long hours = (duration % 86400) / 3600;
      long long minutes = (duration % 3600) / 60;
      long long seconds = duration % 60;

      std::stringstream uptime_ss;
      if (days > 0)
        uptime_ss << days << "d ";
      uptime_ss << hours << "h " << minutes << "m " << seconds << "s";

      content_type = "application/json; charset=utf-8";
      body = "{\n"
             "  \"status\": \"online\",\n"
             "  \"uptime\": \"" +
             uptime_ss.str() +
             "\",\n"
             "  \"uptime_seconds\": " +
             std::to_string(duration) +
             ",\n"
             "  \"requests_processed\": " +
             std::to_string(global_request_count.load()) +
             ",\n"
             "  \"host\": \"" +
             host_header +
             "\",\n"
             "  \"architecture\": \"Raspberry Pi Custom C++17\"\n"
             "}\n";
    } else {
      status_code = 404;
      body = "404 Not Found\nSupported curl endpoints:\n"
             "  /        - Plaintext ASCII Resume\n"
             "  /quote   - Random inspiring developer quote\n"
             "  /uuid    - Generate a standard UUIDv4\n"
             "  /coin    - Flip a virtual coin\n"
             "  /stats   - Fetch server diagnostic details\n";
    }

    std::stringstream ss;
    ss << "HTTP/1.1 " << status_code << " "
       << (status_code == 200 ? "OK" : "Not Found") << "\r\n"
       << "Content-Type: " << content_type << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n\r\n"
       << body;

    std::string response = ss.str();
    write(client_fd, response.c_str(), response.size());

  } else {
    // Standard Website Request (Static Files)
    std::string file_path = path;

    // Root path defaults to index.html
    if (file_path == "/") {
      file_path = "/index.html";
    }

    std::string full_path = serve_dir + file_path;

    // Open file in binary mode
    std::ifstream file(full_path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
      status_code = 404;
      send_error(client_fd, 404, "Not Found",
                 "The requested file \"" + path + "\" could not be found.");
    } else {
      std::streamsize size = file.tellg();
      file.seekg(0, std::ios::beg);

      std::vector<char> buffer(size);
      if (file.read(buffer.data(), size)) {
        std::stringstream header_stream;
        header_stream << "HTTP/1.1 200 OK\r\n"
                      << "Content-Type: " << get_mime_type(file_path) << "\r\n"
                      << "Content-Length: " << size << "\r\n"
                      << "Cache-Control: max-age=3600\r\n" // Cache for 1 hour
                      << "Connection: close\r\n\r\n";

        std::string headers = header_stream.str();
        write(client_fd, headers.c_str(), headers.size());
        write(client_fd, buffer.data(), buffer.size());
      } else {
        status_code = 500;
        send_error(client_fd, 500, "Internal Server Error",
                   "Could not read static resource file.");
      }
    }
  }

  // Complete timing metrics and close connection
  auto end_time = std::chrono::high_resolution_clock::now();
  double elapsed =
      std::chrono::duration<double, std::milli>(end_time - start_time).count();
  log_request(client_ip, method, host_header, path, status_code, elapsed);
  close(client_fd);
}

// Parse a comma-separated allowlist (e.g. "127.0.0.1,192.168.1.10") into ips.
void parse_ip_list(const std::string &csv, std::vector<std::string> &out) {
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item = trim(item);
    if (!item.empty())
      out.push_back(item);
  }
}

int main(int argc, char *argv[]) {
  // Environment defaults for the root-write feature (overridable by flags).
  if (const char *p = std::getenv("ROOT_PASSWORD"))
    root_password = p;
  if (const char *ips = std::getenv("ROOT_ALLOWED_IPS"))
    parse_ip_list(ips, root_allowed_ips);

  // Basic argument parsing
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
      serve_port = argv[++i];
    } else if (std::strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
      serve_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--root-pass") == 0 && i + 1 < argc) {
      root_password = argv[++i];
    } else if (std::strcmp(argv[i], "--root-ips") == 0 && i + 1 < argc) {
      root_allowed_ips.clear();
      parse_ip_list(argv[++i], root_allowed_ips);
    } else if (std::strcmp(argv[i], "--help") == 0) {
      std::cout << "Usage: " << argv[0]
                << " [-p port] [-d serve_directory]"
                   " [--root-pass PASS] [--root-ips ip1,ip2]\n"
                << "Default port: 8080\n"
                << "Default directory: ./public\n"
                << "Root writes (hidden terminal) are enabled only when both a\n"
                << "password and an IP allowlist are configured (flags or the\n"
                << "ROOT_PASSWORD / ROOT_ALLOWED_IPS environment variables)."
                << std::endl;
      return 0;
    }
  }

  // Register signals for graceful shutdown
  struct sigaction sa;
  sa.sa_handler = signal_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  // Create IPv4 listener socket
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == -1) {
    std::cerr << "\033[1;31m[ERROR] Failed to create socket\033[0m"
              << std::endl;
    return 1;
  }

  // Set SO_REUSEADDR to prevent "Address already in use" errors on restarts
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    std::cerr << "\033[1;31m[ERROR] setsockopt SO_REUSEADDR failed\033[0m"
              << std::endl;
    close(server_fd);
    return 1;
  }

  // Configure address structure
  struct sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY; // Bind to all interfaces

  int port_num = std::stoi(serve_port);
  address.sin_port = htons(port_num);

  // Bind socket to port
  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
    std::cerr
        << "\033[1;31m[ERROR] Bind failed on port " << serve_port
        << ". Is the port already in use or do you need root privileges?\033[0m"
        << std::endl;
    close(server_fd);
    return 1;
  }

  // Start listening (maximum backlogged connections set to 128)
  if (listen(server_fd, 128) == -1) {
    std::cerr << "\033[1;31m[ERROR] Listen failed\033[0m" << std::endl;
    close(server_fd);
    return 1;
  }

  std::cout
      << "\033[1;32m[SYSTEM] C++17 Multithreaded Web Server running...\033[0m"
      << std::endl;
  std::cout << "[SYSTEM] Serving directory: \033[1;34m" << serve_dir
            << "\033[0m" << std::endl;
  std::cout << "[SYSTEM] Listening on port: \033[1;34m" << serve_port
            << "\033[0m" << std::endl;

  // Report the root-write (hidden terminal) status.
  if (!root_password.empty() && !root_allowed_ips.empty()) {
    std::stringstream ips;
    for (size_t i = 0; i < root_allowed_ips.size(); ++i)
      ips << (i ? ", " : "") << root_allowed_ips[i];
    std::cout << "[SYSTEM] Root terminal writes: \033[1;32mENABLED\033[0m "
              << "(allowlist: " << ips.str() << ")" << std::endl;
  } else {
    std::cout << "[SYSTEM] Root terminal writes: \033[1;33mdisabled\033[0m "
              << "(set --root-pass and --root-ips to enable)" << std::endl;
  }

  // Connection accept loop
  while (keep_running) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd =
        accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == -1) {
      if (!keep_running)
        break; // Interrupted by shutdown signal
      continue;
    }

    // Get IP of connected client
    char client_ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, INET_ADDRSTRLEN);
    std::string ip(client_ip_str);

    // Spin off thread to handle connection concurrently
    std::thread t(handle_client, client_fd, ip);
    t.detach(); // Detach to clean up resources automatically on complete
  }

  std::cout
      << "\033[1;32m[SYSTEM] Server terminated gracefully. Goodbye!\033[0m"
      << std::endl;
  return 0;
}
