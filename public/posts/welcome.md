# Welcome to /posts

This directory is **live** and writable — but only to `root`, and only when the
site is served by the C++ backend with a configured password and source-IP
allowlist.

Each `.md` file here is rendered to HTML on demand by [view.html](view.html?f=posts/welcome.md)
using the vendored `marked` library. To add one:

```
login              # enter the root password (authorized IPs only)
nano hello.md      # write some Markdown
^O                 # save  ->  /posts/hello.md
```

Then open `view.html?f=posts/hello.md`.
