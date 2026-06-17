/* ==========================================================================
   HIDDEN TERMINAL  —  a simulated, tmux-style shell for echansen.org
   --------------------------------------------------------------------------
   - 100% client-side: NO code is executed on any server. `ls`, `cd`, `cat`
     and `nano` operate over a virtual filesystem described by /fs.json.
   - Works identically whether the page is served by the C++ backend or by a
     static host like GitHub Pages. The only backend-dependent feature is
     root-authenticated writing (`login` + `nano` save), which is detected at
     runtime and gracefully disabled when no backend is present.
   ========================================================================== */
(function () {
    "use strict";

    var HOME = "/home/eric";
    var HOST = "echansen";

    var state = {
        cwd: HOME.split("/").filter(Boolean), // path segments, e.g. ["home","eric"]
        root: null,                            // VFS root node (from fs.json)
        backend: null,                         // /api/term/info result, or null
        user: "eric",
        token: null,                           // root session token
        history: [],
        histIdx: -1,
        pending: null,                         // resolver when reading a raw line
        opened: false,
        panelHeight: Math.round(window.innerHeight * 0.7)
    };

    // ---- tiny DOM helpers ---------------------------------------------------
    function el(tag, cls, text) {
        var e = document.createElement(tag);
        if (cls) e.className = cls;
        if (text != null) e.textContent = text;
        return e;
    }
    function esc(s) {
        return String(s).replace(/[&<>]/g, function (c) {
            return { "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c];
        });
    }

    // ---- build the UI -------------------------------------------------------
    var bar = el("div", "term-bar");
    bar.appendChild(el("span", "term-bar-grip", "▲ ▲ ▲"));
    bar.appendChild(el("span", null, "pull up for a shell"));
    bar.appendChild(el("span", "term-bar-grip", "▲ ▲ ▲"));

    var panel = el("div", "term-panel");
    var resize = el("div", "term-resize");
    resize.setAttribute("title", "Drag to resize · click or pull down to collapse");
    resize.appendChild(el("span", "term-resize-grip"));
    var screen = el("div", "term-screen");
    var inputRow = el("div", "term-inputrow");
    var ps1 = el("span", "term-ps1");
    var input = el("input", "term-input");
    input.setAttribute("autocomplete", "off");
    input.setAttribute("autocapitalize", "off");
    input.setAttribute("autocorrect", "off");
    input.setAttribute("spellcheck", "false");
    inputRow.appendChild(ps1);
    inputRow.appendChild(input);

    var status = el("div", "term-status");
    var statusLeft = el("div", "term-status-left");
    statusLeft.appendChild(el("span", "term-session", "[echansen]"));
    statusLeft.appendChild(el("span", "term-window", "0:bash*"));
    var statusRight = el("span", null, "");
    status.appendChild(statusLeft);
    status.appendChild(statusRight);

    // nano overlay
    var nano = el("div", "term-nano");
    var nanoTop = el("div", "term-nano-top", "GNU nano");
    var nanoArea = el("textarea", "term-nano-area");
    nanoArea.setAttribute("spellcheck", "false");
    var nanoMsg = el("div", "term-nano-msg");
    var nanoKeys = el("div", "term-nano-keys");
    nanoKeys.innerHTML =
        '<span><b>^O</b>Write Out</span><span><b>^X</b>Exit</span>' +
        '<span><b>^K</b>Cut Line</span><span><b>^G</b>Help</span>';
    nano.appendChild(nanoTop);
    nano.appendChild(nanoArea);
    nano.appendChild(nanoMsg);
    nano.appendChild(nanoKeys);

    panel.appendChild(resize);
    panel.appendChild(screen);
    panel.appendChild(inputRow);
    panel.appendChild(status);
    panel.appendChild(nano);

    function mount() {
        document.body.appendChild(bar);
        document.body.appendChild(panel);
    }
    if (document.body) mount();
    else document.addEventListener("DOMContentLoaded", mount);

    // ---- printing -----------------------------------------------------------
    function printNode(node) {
        screen.appendChild(node);
        screen.scrollTop = screen.scrollHeight;
    }
    function print(text, cls) {
        var line = el("div", "term-line" + (cls ? " " + cls : ""));
        line.textContent = text;
        printNode(line);
    }
    function printHTML(html, cls) {
        var line = el("div", "term-line" + (cls ? " " + cls : ""));
        line.innerHTML = html;
        printNode(line);
    }
    function echoCommand(cmd) {
        printHTML(promptHTML() + esc(cmd));
    }

    // ---- prompt -------------------------------------------------------------
    function cwdString() {
        var p = "/" + state.cwd.join("/");
        if (p === HOME) return "~";
        if (p.indexOf(HOME + "/") === 0) return "~" + p.slice(HOME.length);
        return p;
    }
    function promptHTML() {
        var u = state.user;
        var sym = u === "root" ? "#" : "$";
        var uColor = u === "root" ? "term-err" : "term-prompt-user";
        return '<span class="' + uColor + '">' + esc(u) + "@" + HOST + "</span>" +
            ':<span class="term-prompt-path">' + esc(cwdString()) + "</span>" + sym + " ";
    }
    function refreshPS1() {
        ps1.innerHTML = promptHTML();
    }

    // ---- virtual filesystem -------------------------------------------------
    function resolve(arg) {
        // Returns an array of path segments for `arg` relative to cwd.
        // No argument / "" means the current directory; "~" means home.
        var segs;
        if (arg === undefined || arg === "" || arg === ".") segs = state.cwd.slice();
        else if (arg === "~") segs = HOME.split("/").filter(Boolean);
        else if (arg[0] === "~") segs = (HOME + arg.slice(1)).split("/").filter(Boolean);
        else if (arg[0] === "/") segs = arg.split("/").filter(Boolean);
        else segs = state.cwd.concat(arg.split("/").filter(Boolean));

        var out = [];
        for (var i = 0; i < segs.length; i++) {
            if (segs[i] === ".") continue;
            if (segs[i] === "..") { out.pop(); continue; }
            out.push(segs[i]);
        }
        return out;
    }
    function nodeAt(segs) {
        var node = state.root;
        for (var i = 0; i < segs.length; i++) {
            if (!node || node.type !== "dir" || !node.children) return null;
            var found = null;
            for (var j = 0; j < node.children.length; j++) {
                if (node.children[j].name === segs[i]) { found = node.children[j]; break; }
            }
            if (!found) return null;
            node = found;
        }
        return node;
    }
    // Refresh a "live" directory's children from the backend (e.g. /posts).
    function refreshLive(node) {
        if (!node || !node.live || !state.backend || !state.backend.backend) return Promise.resolve();
        return fetch("/api/term/posts")
            .then(function (r) { return r.ok ? r.json() : []; })
            .then(function (names) {
                var existing = {};
                (node.children || []).forEach(function (c) { existing[c.name] = true; });
                names.forEach(function (n) {
                    if (!existing[n]) {
                        node.children.push({ name: n, type: "file", src: node.base + "/" + n });
                    }
                });
            })
            .catch(function () { });
    }

    // ---- backend detection --------------------------------------------------
    function detectBackend() {
        return fetch("/api/term/info", { headers: { "Accept": "application/json" } })
            .then(function (r) { return r.ok ? r.json() : null; })
            .then(function (j) { if (j && j.backend) state.backend = j; })
            .catch(function () { state.backend = null; });
    }

    // ---- read a raw line (used by `login` for the password prompt) ----------
    function readLine(opts) {
        opts = opts || {};
        return new Promise(function (resolve) {
            state.pending = resolve;
            if (opts.mask) input.type = "password";
            ps1.textContent = opts.label || "";
            input.focus();
        });
    }
    function endReadLine() {
        state.pending = null;
        input.type = "text";
        refreshPS1();
    }

    // ======================================================================
    //  COMMANDS
    // ======================================================================
    var COMMANDS = {
        help: function () {
            printHTML([
                '<span class="term-ok">Available commands</span> — this is a simulated shell.',
                '  <b>ls</b> [-h] [-a]      list the current directory',
                '  <b>cd</b> &lt;dir&gt;          change directory (cd .. , cd ~ , cd /)',
                '  <b>cat</b> [-r] &lt;file&gt;   print a file (Markdown renders; -r = raw)',
                '  <b>nano</b> &lt;file&gt;       open the editor (read-only unless root)',
                '  <b>pwd</b>               print working directory',
                '  <b>whoami</b>            print the current user',
                '  <b>login</b> / <b>su</b>       authenticate as root (backend only)',
                '  <b>logout</b>            drop root privileges',
                '  <b>echo</b> &lt;text&gt;       print text',
                '  <b>neofetch</b>          system info',
                '  <b>clear</b>             clear the screen',
                '  <b>exit</b>              close the terminal',
                '',
                'Shortcuts: <b>Ctrl+`</b> toggle &nbsp; <b>Esc</b> close &nbsp; <b>↑/↓</b> history &nbsp; <b>Tab</b> complete'
            ].join("\n"));
        },

        pwd: function () { print("/" + state.cwd.join("/")); },

        whoami: function () { print(state.user); },

        echo: function (args) { print(args.join(" ")); },

        clear: function () { screen.innerHTML = ""; },

        exit: function () { closePanel(); },

        neofetch: function () {
            var host = state.backend && state.backend.backend
                ? "C++17 socket server" : "static (GitHub Pages)";
            printHTML(
                '<span class="term-green">      _              </span>  <b>' + state.user + '@' + HOST + '</b>\n' +
                '<span class="term-green">  ___| |__   __ _ _ __ </span>  ----------------\n' +
                '<span class="term-green"> / _ \\ \'_ \\ / _` | \'_ \\</span>  <b>OS</b>: echansen.org\n' +
                '<span class="term-green">|  __/ | | | (_| | | | |</span> <b>Host</b>: ' + esc(host) + '\n' +
                '<span class="term-green"> \\___|_| |_|\\__,_|_| |_|</span> <b>Shell</b>: bash (simulated)\n' +
                '                          <b>Uptime</b>: ∞');
        },

        ls: function (args) {
            var flags = args.filter(function (a) { return a[0] === "-"; }).join("");
            var rest = args.filter(function (a) { return a[0] !== "-"; });
            var longFmt = flags.indexOf("h") !== -1;
            var segs = resolve(rest[0]);
            var node = nodeAt(segs);
            if (!node) { print("ls: cannot access '" + (rest[0] || ".") + "': No such file or directory", "term-err"); return; }
            if (node.type === "file") { print(node.name); return; }
            return refreshLive(node).then(function () {
                var kids = (node.children || []).slice().sort(function (a, b) {
                    if ((a.type === "dir") !== (b.type === "dir")) return a.type === "dir" ? -1 : 1;
                    return a.name < b.name ? -1 : 1;
                });
                if (!kids.length) { print("", "term-muted"); return; }
                if (longFmt) {
                    kids.forEach(function (c) {
                        var perm = c.type === "dir" ? "drwxr-xr-x" : "-rw-r--r--";
                        var size = c.type === "dir" ? String((c.children || []).length).padStart(3) + " items" : "       file";
                        var name = c.type === "dir"
                            ? '<span class="term-prompt-path">' + esc(c.name) + "/</span>"
                            : esc(c.name);
                        printHTML('<span class="term-muted">' + perm + "  " + size + "</span>  " + name);
                    });
                } else {
                    var html = kids.map(function (c) {
                        return c.type === "dir"
                            ? '<span class="term-prompt-path">' + esc(c.name) + "/</span>"
                            : esc(c.name);
                    }).join("    ");
                    printHTML(html);
                }
            });
        },

        cd: function (args) {
            var target = args.filter(function (a) { return a[0] !== "-"; })[0];
            if (!target) target = "~"; // bare `cd` returns home
            var segs = resolve(target);
            var node = nodeAt(segs);
            if (!node) { print("cd: " + (target || "~") + ": No such file or directory", "term-err"); return; }
            if (node.type !== "dir") { print("cd: " + target + ": Not a directory", "term-err"); return; }
            state.cwd = segs;
            refreshPS1();
        },

        cat: function (args) {
            var raw = args.indexOf("-r") !== -1 || args.indexOf("--raw") !== -1;
            var rest = args.filter(function (a) { return a[0] !== "-"; });
            if (!rest.length) { print("cat: missing file operand", "term-err"); return; }
            var segs = resolve(rest[0]);
            var node = nodeAt(segs);
            if (!node) { print("cat: " + rest[0] + ": No such file or directory", "term-err"); return; }
            if (node.type === "dir") { print("cat: " + rest[0] + ": Is a directory", "term-err"); return; }
            if (!node.src) { print(node.content || "", null); return; }
            return fetch(node.src)
                .then(function (r) { if (!r.ok) throw new Error(r.status); return r.text(); })
                .then(function (text) {
                    var isMd = /\.(md|markdown)$/i.test(node.name);
                    if (isMd && !raw && window.marked) {
                        var div = el("div", "markdown-body");
                        div.innerHTML = window.marked.parse(text);
                        printNode(div);
                    } else {
                        print(text);
                    }
                })
                .catch(function (e) { print("cat: " + rest[0] + ": could not read (" + e.message + ")", "term-err"); });
        },

        nano: function (args) {
            var rest = args.filter(function (a) { return a[0] !== "-"; });
            if (!rest.length) { print("nano: missing filename", "term-err"); return; }
            return openNano(rest[0]);
        },

        login: cmdLogin,
        su: cmdLogin,

        logout: function () {
            if (state.user !== "root") { print("logout: not logged in as root", "term-muted"); return; }
            state.user = "eric";
            state.token = null;
            refreshPS1();
            print("Dropped root privileges.", "term-muted");
        }
    };

    function cmdLogin() {
        if (!state.backend || !state.backend.backend) {
            print("login: no backend on this host — root writes are unavailable on the static site.", "term-err");
            return;
        }
        if (!state.backend.writes_enabled) {
            print("login: root access is disabled (no password configured on the server).", "term-err");
            return;
        }
        if (!state.backend.ip_allowed) {
            print("login: your address (" + state.backend.client_ip + ") is not on the root allowlist.", "term-err");
            return;
        }
        print("Authenticating as root…", "term-muted");
        return readLine({ mask: true, label: "Password: " }).then(function (pw) {
            endReadLine();
            return fetch("/api/term/login", {
                method: "POST",
                headers: { "X-Root-Password": pw }
            }).then(function (r) {
                return r.json().then(function (j) { return { ok: r.ok, j: j }; });
            }).then(function (res) {
                if (res.ok && res.j.token) {
                    state.token = res.j.token;
                    state.user = "root";
                    refreshPS1();
                    print("Welcome, root. You may now write into ~/posts.", "term-ok");
                } else {
                    print("login: " + (res.j.error || "authentication failed"), "term-err");
                }
            }).catch(function (e) {
                print("login: request failed (" + e.message + ")", "term-err");
            });
        });
    }

    // ---- nano ---------------------------------------------------------------
    var nanoCtx = null; // { name, postName, writable }
    function openNano(arg) {
        var segs = resolve(arg);
        var node = nodeAt(segs);
        var inPosts = segs.length >= 4 && segs[0] === "home" && segs[1] === "eric" && segs[2] === "posts";
        var writable = !!(inPosts && state.user === "root" && state.token &&
            state.backend && state.backend.backend && state.backend.writes_enabled);
        var name = segs[segs.length - 1];

        nanoCtx = { name: name, postName: name, writable: writable };
        nanoTop.textContent = "GNU nano  —  " + name + (writable ? "" : "  (read-only)");
        nanoArea.readOnly = !writable;
        nanoMsg.textContent = writable
            ? "Editing as root. ^O to write into /posts, ^X to exit."
            : (inPosts ? "Read-only: log in as root to write here." : "Read-only: this path is not writable.");

        var done = function (text) {
            nanoArea.value = text || "";
            nano.classList.add("open");
            nanoArea.focus();
        };
        if (node && node.src) {
            fetch(node.src).then(function (r) { return r.ok ? r.text() : ""; }).then(done).catch(function () { done(""); });
        } else {
            done("");
        }
    }
    function closeNano() {
        nano.classList.remove("open");
        nanoCtx = null;
        input.focus();
    }
    function saveNano() {
        if (!nanoCtx || !nanoCtx.writable) {
            nanoMsg.textContent = "[ Read-only — cannot write ]";
            return;
        }
        var name = nanoCtx.postName;
        if (!/\.(md|markdown|txt)$/i.test(name)) name += ".md";
        nanoMsg.textContent = "Writing " + name + "…";
        fetch("/api/term/write", {
            method: "POST",
            headers: {
                "Authorization": "Bearer " + state.token,
                "X-File-Name": name,
                "Content-Type": "text/plain"
            },
            body: nanoArea.value
        }).then(function (r) {
            return r.json().then(function (j) { return { ok: r.ok, j: j }; });
        }).then(function (res) {
            if (res.ok && res.j.ok) {
                nanoMsg.textContent = "[ Wrote " + res.j.path + " ]";
                var posts = nodeAt(["home", "eric", "posts"]);
                if (posts && !nodeAt(["home", "eric", "posts", name])) {
                    posts.children.push({ name: name, type: "file", src: "posts/" + name });
                }
            } else {
                nanoMsg.textContent = "[ Error: " + (res.j.error || "write failed") + " ]";
            }
        }).catch(function (e) {
            nanoMsg.textContent = "[ Error: " + e.message + " ]";
        });
    }
    nanoArea.addEventListener("keydown", function (e) {
        if (e.ctrlKey && (e.key === "x" || e.key === "X")) { e.preventDefault(); closeNano(); }
        else if (e.ctrlKey && (e.key === "o" || e.key === "O")) { e.preventDefault(); saveNano(); }
        else if (e.ctrlKey && (e.key === "k" || e.key === "K")) {
            e.preventDefault();
            if (nanoArea.readOnly) return;
            // crude "cut current line"
            var v = nanoArea.value, p = nanoArea.selectionStart;
            var start = v.lastIndexOf("\n", p - 1) + 1;
            var end = v.indexOf("\n", p); if (end === -1) end = v.length; else end += 1;
            nanoArea.value = v.slice(0, start) + v.slice(end);
            nanoArea.selectionStart = nanoArea.selectionEnd = start;
        }
    });

    // ======================================================================
    //  COMMAND DISPATCH
    // ======================================================================
    function runCommand(raw) {
        var cmd = raw.trim();
        echoCommand(raw);
        if (!cmd) return;
        state.history.push(cmd);
        state.histIdx = state.history.length;

        var parts = cmd.match(/(?:[^\s"]+|"[^"]*")+/g) || [];
        parts = parts.map(function (p) { return p.replace(/^"|"$/g, ""); });
        var name = parts[0];
        var args = parts.slice(1);

        if (COMMANDS.hasOwnProperty(name)) {
            try {
                var r = COMMANDS[name](args);
                if (r && r.then) { lock(true); r.then(function () { lock(false); }); }
            } catch (e) {
                print(name + ": " + e.message, "term-err");
            }
        } else {
            print(name + ": command not found", "term-err");
        }
    }
    function lock(on) { input.disabled = on; if (!on) input.focus(); }

    // ---- input handling -----------------------------------------------------
    input.addEventListener("keydown", function (e) {
        if (e.key === "Enter") {
            e.preventDefault();
            var val = input.value;
            input.value = "";
            if (state.pending) {
                var res = state.pending;
                state.pending = null;
                input.type = "text";
                res(val);
                return;
            }
            runCommand(val);
        } else if (e.key === "ArrowUp") {
            e.preventDefault();
            if (state.histIdx > 0) { state.histIdx--; input.value = state.history[state.histIdx] || ""; }
        } else if (e.key === "ArrowDown") {
            e.preventDefault();
            if (state.histIdx < state.history.length) { state.histIdx++; input.value = state.history[state.histIdx] || ""; }
        } else if (e.key === "Tab") {
            e.preventDefault();
            tabComplete();
        } else if (e.key === "l" && e.ctrlKey) {
            e.preventDefault();
            screen.innerHTML = "";
        }
    });

    function tabComplete() {
        var v = input.value;
        var m = v.match(/(\S*)$/);
        var frag = m ? m[1] : "";
        var dirPart = frag.lastIndexOf("/") >= 0 ? frag.slice(0, frag.lastIndexOf("/") + 1) : "";
        var leaf = frag.slice(dirPart.length);
        var baseSegs = resolve(dirPart || ".");
        var node = nodeAt(baseSegs);
        if (!node || node.type !== "dir") return;
        var matches = (node.children || []).filter(function (c) { return c.name.indexOf(leaf) === 0; });
        if (matches.length === 1) {
            var completion = matches[0].name + (matches[0].type === "dir" ? "/" : " ");
            input.value = v.slice(0, v.length - leaf.length) + completion;
        } else if (matches.length > 1) {
            echoCommand(v);
            printHTML(matches.map(function (c) {
                return c.type === "dir"
                    ? '<span class="term-prompt-path">' + esc(c.name) + "/</span>"
                    : esc(c.name);
            }).join("    "));
        }
    }

    // ======================================================================
    //  PANEL OPEN / CLOSE / RESIZE
    // ======================================================================
    function setHeight(px) {
        state.panelHeight = Math.max(160, Math.min(window.innerHeight * 0.92, px));
        panel.style.height = state.panelHeight + "px";
    }
    // Focus the command line. Called synchronously inside pointer/key gestures
    // so mobile browsers will also raise the on-screen keyboard; the deferred
    // call is a desktop backup in case the panel is still transitioning in.
    function focusInput() {
        if (input.disabled) return;
        try { input.focus({ preventScroll: true }); } catch (e) { input.focus(); }
    }
    function openPanel() {
        if (!state.opened) {
            state.opened = true;
            if (!screen.childElementCount) banner();
        }
        setHeight(state.panelHeight);
        panel.classList.add("open");
        bar.style.opacity = "0";
        bar.style.pointerEvents = "none";
        refreshPS1();
        focusInput();
        setTimeout(focusInput, 60);
    }
    function closePanel() {
        panel.classList.remove("open");
        bar.style.opacity = "";
        bar.style.pointerEvents = "";
        if (nano.classList.contains("open")) closeNano();
        // If it was collapsed by shrinking, restore a sensible height for next open.
        if (state.panelHeight < 240) state.panelHeight = Math.round(window.innerHeight * 0.7);
    }
    function togglePanel() {
        if (panel.classList.contains("open")) closePanel(); else openPanel();
    }

    function banner() {
        printHTML(
            '<span class="term-green">echansen.org</span> simulated shell — type ' +
            '<span class="term-ok">help</span> to begin, ' +
            '<span class="term-ok">exit</span> to leave.');
        var mode = state.backend && state.backend.backend
            ? '<span class="term-ok">C++ backend detected</span> — root login available to authorized hosts.'
            : '<span class="term-muted">static host — running read-only (no backend).</span>';
        printHTML(mode);
        print("");
    }

    // Drag from the closed bar to lift the panel; a plain click toggles it.
    function bindDrag(handle, opts) {
        var startY = 0, startH = 0, moved = false, active = false;
        handle.addEventListener("pointerdown", function (e) {
            active = true; moved = false;
            startY = e.clientY;
            startH = state.panelHeight;
            handle.setPointerCapture && handle.setPointerCapture(e.pointerId);
            handle.classList.add("dragging");
            panel.classList.add("no-anim");
            if (opts.openOnGrab) { panel.classList.add("open"); bar.style.opacity = "0"; bar.style.pointerEvents = "none"; }
        });
        handle.addEventListener("pointermove", function (e) {
            if (!active) return;
            var dy = startY - e.clientY;
            if (Math.abs(dy) > 4) moved = true;
            if (opts.openOnGrab) setHeight(window.innerHeight - e.clientY);
            else setHeight(startH + dy);
        });
        function up() {
            if (!active) return;
            active = false;
            handle.classList.remove("dragging");
            panel.classList.remove("no-anim");
            if (opts.openOnGrab && !moved) {
                // treat as a click → open to default height
                openPanel();
            } else if (opts.openOnGrab && moved) {
                state.opened = true;
                if (!screen.childElementCount) banner();
                refreshPS1();
                focusInput(); // type immediately after lifting the slider
            } else if (!opts.openOnGrab) {
                // Top resize handle: a tap collapses the panel; dragging it most
                // of the way down (small remaining height) also collapses.
                if (!moved || state.panelHeight <= 200) closePanel();
            }
        }
        handle.addEventListener("pointerup", up);
        handle.addEventListener("pointercancel", up);
    }
    bindDrag(bar, { openOnGrab: true });
    bindDrag(resize, { openOnGrab: false });

    // Clicking/tapping anywhere in the scrollback (re)focuses the prompt,
    // unless the user is selecting text or the nano overlay is up.
    screen.addEventListener("click", function () {
        if (nano.classList.contains("open")) return;
        var sel = window.getSelection && window.getSelection();
        if (sel && String(sel).length) return;
        focusInput();
    });

    // Global shortcuts
    document.addEventListener("keydown", function (e) {
        if (e.key === "`" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); togglePanel(); return; }
        if (e.key === "Escape" && panel.classList.contains("open")) {
            if (nano.classList.contains("open")) closeNano();
            else closePanel();
            return;
        }
        // Safety net: if the terminal is open but the prompt isn't focused,
        // redirect keystrokes to it so the user can "just start typing".
        if (!panel.classList.contains("open")) return;
        if (nano.classList.contains("open")) return;
        if (document.activeElement === input) return;
        if (e.metaKey || e.ctrlKey || e.altKey) return;
        if (e.key && e.key.length === 1) {
            input.value += e.key; // don't drop the first character
            e.preventDefault();
            focusInput();
        } else if (e.key === "Backspace" || e.key === "Enter" ||
            (e.key && e.key.indexOf("Arrow") === 0)) {
            focusInput();
        }
    });
    window.addEventListener("resize", function () {
        if (panel.classList.contains("open")) setHeight(state.panelHeight);
    });

    // ---- boot ---------------------------------------------------------------
    Promise.all([
        fetch("fs.json").then(function (r) { return r.json(); }).then(function (data) {
            state.root = data.tree;
            if (data.cwd) state.cwd = data.cwd.split("/").filter(Boolean);
        }).catch(function () {
            state.root = { name: "/", type: "dir", children: [] };
        }),
        detectBackend()
    ]).then(function () {
        refreshPS1();
        updateStatusClock();
    });

    function updateStatusClock() {
        var d = new Date();
        var hh = String(d.getHours()).padStart(2, "0");
        var mm = String(d.getMinutes()).padStart(2, "0");
        statusRight.textContent = '"' + HOST + '" ' + hh + ":" + mm;
        setTimeout(updateStatusClock, 15000);
    }
})();
