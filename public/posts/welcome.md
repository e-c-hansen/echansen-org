# Welcome to /posts

This directory is **live** and writable — but only to `root`, and only when the
site is served by the C++ backend with a configured password and source-IP
allowlist.

Each `.md` file here is rendered to HTML on demand by
[view.html](view.html?f=posts/welcome.md) using the vendored `marked` library.
New posts can be published through the backend's authenticated Markdown API,
then opened at `view.html?f=posts/<name>.md`.
