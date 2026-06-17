# echansen.org

A hand-built personal site served by a zero-dependency C++17 HTTP server — and,
when hosted statically on GitHub Pages, the very same files with no backend.

## The hidden terminal

Pull up the bar at the bottom of any page (or press the grip) to reveal a
tmux-style shell. Supported commands:

| command        | description                                   |
| -------------- | --------------------------------------------- |
| `ls [-h] [-a]` | list the current directory (`-h` = details)   |
| `cd <dir>`     | change directory (`cd ..`, `cd ~`, `cd /`)    |
| `cat <file>`   | print a file (Markdown renders; `-r` for raw) |
| `nano <file>`  | open the editor (read-only unless root)       |
| `pwd` `whoami` | where am I / who am I                          |
| `login` `su`   | authenticate as root (backend only)           |
| `help` `clear` | the basics                                     |

## Root mode

When the site is served by the C++ backend (not static Pages) and the operator
has configured a password + source-IP allowlist, `login` unlocks `nano` writes
into `~/eric/posts`. New posts render at `view.html?f=posts/<name>.md`.
