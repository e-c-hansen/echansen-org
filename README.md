# Eric Hansen Personal Web Server & Portfolio Website

A hand-crafted, high-performance personal portfolio website and terminal-friendly utility API hosted on a Raspberry Pi.

The backend is built in **modern, multithreaded C++17** using raw POSIX sockets and standard threads, with absolute zero third-party dependencies. It is designed to consume virtually zero system resources (typically `<2MB` RAM and `0%` CPU idle), making it perfect for a lightweight Raspberry Pi home server.

---

## 🛠️ Technology Stack & Architecture

- **Web Server Backend**: Modern, multithreaded C++17.
  - Multi-threaded connection pool using raw POSIX sockets and `std::thread`.
  - HTTP/1.1 path validation (strict traversal protection to prevent `..` hacks).
  - Custom Host header subdomain router.
  - Dynamic plain-text / JSON generator for terminal integrations.
- **Portfolio Frontend**: Pure, semantic HTML5 and sleek modern CSS.
  - Premium glassmorphic dark-mode interface utilizing Google Fonts (Inter) and HSL colors.
  - Custom-crafted SVG social/contact icons.
  - Responsive layout adjusting perfectly to mobile, tablet, and 4K displays.
  - **Print Stylesheet**: `@media print` layout formatting that automatically compresses spacing and reflows elements into a gorgeous, clean, **standard 1-page paper/PDF resume** when printed or saved.
- **Subdomain API Helpers**: Terminal-friendly plain-text/JSON endpoints mapping to a DNS subdomain.

---

## 📂 Repository Structure

```
├── main.cpp              # High-performance C++17 socket server source
├── deploy.sh             # Mac asset-packaging & SCP deployment script
├── setup_pi.sh           # Pi compiler & systemd registration installer
├── README.md             # Comprehensive operational documentation
├── .github/workflows/
│   └── pages.yml         # GitHub Pages static-deploy workflow (publishes public/)
├── assets/
│   └── resume.tex        # Original LaTeX resume source (reference)
└── public/               # Static web dir — served identically by C++ or Pages
    ├── index.html        # Landing page (+ hidden terminal hooks)
    ├── resume.html       # Experience & skills
    ├── github.html       # Code & systems
    ├── view.html         # Generic Markdown -> HTML viewer (?f=/posts/x.md)
    ├── style.css         # Core design system
    ├── terminal.css      # Hidden-terminal + Markdown rendering styles
    ├── terminal.js       # The simulated tmux-style shell (client-side)
    ├── fs.json           # Virtual filesystem manifest for the terminal
    ├── resume.md         # Plain-text resume (also the curl / endpoint)
    ├── content/          # Markdown surfaced inside the terminal (about, etc.)
    ├── posts/            # Root-writable Markdown posts (live dir)
    └── vendor/
        └── marked.min.js # Vendored Markdown parser (MIT)
```

---

## 🌍 Two Hosting Modes (Single Source of Truth)

The `public/` directory is the **only** set of web files, and it is served two
ways without modification:

1. **Local / Raspberry Pi (dynamic):** the C++ binary serves `public/` and adds
   the live curl utility endpoints (`/quote`, `/uuid`, `/stats`, …) and the
   root-write terminal API (`/api/term/*`).
2. **GitHub Pages (static):** `.github/workflows/pages.yml` publishes `public/`
   on every push to `main`. The hidden terminal still works (it is entirely
   client-side); the dynamic backend endpoints simply 404, and the terminal
   detects their absence and runs read-only.

To enable Pages: in the repo settings, set **Pages → Build and deployment →
Source = GitHub Actions**. The included workflow handles the rest. (`public/.nojekyll`
keeps GitHub from running Jekyll over the files.)

---

## 🖥️ The Hidden Terminal

Every page carries a slim bar pinned to the bottom of the viewport. **Drag it up**
(or click it, or press <kbd>Ctrl</kbd>+<kbd>`</kbd>) to reveal a tmux-style shell.
It is a *simulation* — no commands execute on any server. It navigates a virtual
filesystem described by [`public/fs.json`](public/fs.json):

| command        | behaviour                                              |
| -------------- | ----------------------------------------------------- |
| `ls [-h] [-a]` | list the current directory (`-h` = long/detailed)     |
| `cd <dir>`     | change directory (`cd ..`, `cd ~`, `cd /www`)         |
| `cat [-r] <f>` | print a file; Markdown is rendered (`-r` = raw text)  |
| `nano <file>`  | open the editor overlay (read-only unless root)       |
| `pwd` `whoami` | location / current user                               |
| `login` `su`   | authenticate as root (backend only)                   |
| `help` `clear` `exit` `neofetch` `echo` | the usual                    |

Markdown is rendered to HTML by the vendored [`marked`](https://github.com/markedjs/marked)
library — both inside `cat` and on the standalone viewer page
`view.html?f=/posts/<name>.md`.

### Root mode — adding posts

Item (3): a password- and IP-gated `root` user who can create Markdown posts via
`nano`. This requires the **C++ backend** (it persists files and checks the
source IP), so it is automatically disabled on static GitHub Pages.

Enable it by giving the server both a password and a source-IP allowlist — via
flags or environment variables:

```bash
./server -p 8080 -d ./public \
    --root-pass 'choose-a-strong-secret' \
    --root-ips '127.0.0.1,192.168.1.50'

# …or via the environment (handy for systemd):
ROOT_PASSWORD='…' ROOT_ALLOWED_IPS='127.0.0.1' ./server
```

Then, from the terminal on the live site:

```
login              # prompts for the password (authorized IPs only)
nano hello.md      # write some Markdown
^O                 # writes to public/posts/hello.md
^X                 # exit the editor
```

The new post is immediately viewable at `/view.html?f=/posts/hello.md` and shows
up under `~/posts` in the shell.

**Security model & caveats:**

- Writes require **both** a matching password **and** a source IP on the allowlist.
- The write target is restricted to `public/posts/` with a strict filename
  charset (`[A-Za-z0-9._-]`, forced `.md`) — no path traversal, no subdirectories.
- Sessions are in-memory bearer tokens that expire after 1 hour.
- Behind a loopback reverse proxy (Caddy / the iptables redirect) the server
  trusts the first `X-Forwarded-For` hop; for direct connections it uses the
  real socket peer. **Only put the allowlist behind a proxy you control**, since
  `X-Forwarded-For` is otherwise client-settable.
- The password is compared in constant time, but is a shared secret sent over
  the wire — **only run this over HTTPS** (e.g. behind Caddy, per the section below).

To wire the credentials into the systemd service on the Pi, add an override:

```bash
sudo systemctl edit echansen-org
# In the editor, add:
#   [Service]
#   Environment=ROOT_PASSWORD=your-secret
#   Environment=ROOT_ALLOWED_IPS=127.0.0.1
sudo systemctl restart echansen-org
```

---

## 💻 Local Development & Testing on Mac

You can easily compile and run the web server on your local Mac to preview the design and test subdomain routing.

### 1. Compile locally

Open your terminal and run standard compilation (works out-of-the-box on macOS using `clang++`/`g++`):

```bash
g++ -O3 -std=c++17 -pthread main.cpp -o server
```

### 2. Run the server

Launch the compiled binary, specifying a port and target public directory:

```bash
./server -p 8080 -d ./public
```

### 3. Verification

- **Web Browser**: Open `http://localhost:8080` to see your portfolio site.
- **Terminal Curl (Subdomain Fallback)**: Open a second terminal window and test subdomain routing by simulating `Host` headers:
  - **Plaintext Resume**: `curl -H "Host: api.localhost" http://localhost:8080/`
  - **Inspiring Quote**: `curl -H "Host: api.localhost" http://localhost:8080/quote`
  - **UUIDv4 Generator**: `curl -H "Host: api.localhost" http://localhost:8080/uuid`
  - **Coin Flip**: `curl -H "Host: api.localhost" http://localhost:8080/coin`
  - **System/Uptime Stats**: `curl -H "Host: api.localhost" http://localhost:8080/stats`

---

## 🚀 Raspberry Pi Deployment

We have prepared two lightweight scripts to handle deployment and service setup seamlessly.

### Step 1: Push Files from Mac to Raspberry Pi

From the repository root on your Mac, execute `deploy.sh` passing your Raspberry Pi's SSH target address (e.g. `pi@raspberrypi.local` or an IP):

```bash
./deploy.sh pi@raspberrypi.local
```

*This packages the source code, setup script, and public directory, and transfers them to the Pi in `~/echansen_org`.*

### Step 2: Install and Compile on Raspberry Pi

Simply copy-paste the single-line command outputted by the deployment script to SSH in, compile the binary locally on the Pi, and set up the background daemon:

```bash
ssh -t pi@raspberrypi.local "cd ~/echansen_org && chmod +x setup_pi.sh && sudo ./setup_pi.sh"
```

*This script will compile the code, create a systemd configuration file, register the daemon, enable it to start on Pi boots, and launch it.*

---

## 🎛️ Managing the Background Service

Once installed on the Pi, the C++ web server runs continuously in the background. You can manage it using standard `systemctl` commands:

- **Check Service Status**:

  ```bash
  sudo systemctl status echansen-org
  ```

- **View Real-Time Server Logs**:

  ```bash
  sudo journalctl -u echansen-org -f
  ```

- **Restart the Server**:

  ```bash
  sudo systemctl restart echansen-org
  ```

- **Stop the Server**:

  ```bash
  sudo systemctl stop echansen-org
  ```

---

## 🌐 Networking: Binding to Ports 80 & 443 (SSL/HTTPS)

To keep the C++ web server running securely as a non-root user, it binds to port `8080` by default. You can route external traffic on standard HTTP/HTTPS ports (80/443) using one of these two standard methods:

### Option A: Clean Reverse Proxy with Caddy (Recommended & Easiest)

Using **Caddy** is the modern gold standard. It takes under 5 minutes to set up, runs with minimal footprint, and automatically requests, installs, and renews free **SSL/HTTPS certificates** from Let's Encrypt out-of-the-box.

1. **Install Caddy on the Pi**:

   ```bash
   sudo apt install -y debian-keyring debian-archive-keyring apt-transport-https
   curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/gpg.key' | sudo gpg --dearmor -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
   curl -1sLf 'https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt' | sudo tee /etc/apt/sources.list.d/caddy-stable.list
   sudo apt update
   sudo apt install caddy
   ```

2. **Edit Caddyfile**:
   Open `/etc/caddy/Caddyfile` on the Pi (`sudo nano /etc/caddy/Caddyfile`) and replace its content with the following:

   ```caddy
   yourdomain.com {
       reverse_proxy localhost:8080
   }

   api.yourdomain.com, curl.yourdomain.com {
       reverse_proxy localhost:8080
   }
   ```

3. **Restart Caddy**:

   ```bash
   sudo systemctl restart caddy
   ```

   *Caddy will automatically fetch SSL certificates and serve your site via HTTPS on `https://yourdomain.com` and `https://api.yourdomain.com`, proxying traffic instantly to the C++ server.*

---

### Option B: Firewalls & Port Forwarding (Port 80 Only, No SSL)

If you do not want to run a reverse proxy and just want to map port 80 traffic directly, you can set up a simple `iptables` rule to route port 80 to 8080 in the kernel:

```bash
sudo iptables -t nat -A PREROUTING -p tcp --dport 80 -j REDIRECT --to-port 8080
```

To make this rule persistent across reboots:

```bash
sudo apt-get install iptables-persistent
```

---

## ⚡ Subdomain Curl Utilities Cheat-Sheet

Once your DNS subdomain (e.g. `api.yourdomain.com` or `curl.yourdomain.com`) is pointed to the Pi, you can fetch plain-text and JSON utilities from any terminal:

```bash
# Fetch clean plain-text ASCII layout of your resume
curl api.yourdomain.com

# Fetch a random inspiring developer/research quote
curl api.yourdomain.com/quote

# Generate a fresh standard UUIDv4 (highly useful for terminal automation!)
curl api.yourdomain.com/uuid

# Flip a virtual coin (returns "Heads" or "Tails")
curl api.yourdomain.com/coin

# Fetch real-time server stats (JSON format containing server uptime, total requests, etc.)
curl api.yourdomain.com/stats
```
