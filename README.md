# ProcShell

**A Unix shell built from scratch in C — with its own system-call commands, a live OS trace mode, and an AI assistant.**

![C](https://img.shields.io/badge/C-POSIX-00599C?logo=c&logoColor=white)
![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey)
![Build](https://img.shields.io/badge/build-0%20warnings-brightgreen)
![License](https://img.shields.io/badge/license-MIT-blue)

ProcShell implements everything a shell does between the keystroke and the result — reading input
in raw terminal mode, parsing it into a command tree, creating processes, wiring pipes and
redirections, handling signals and reaping children — directly on POSIX system calls.

On top of that it adds three things a normal terminal does not have:

- **Its own commands** — 20+ commands (`display`, `tree`, `diskuse`, `fileinfo`, `sysinfo`, `procinfo`, `signal`, `timeit`, …) written on system calls instead of calling `ls`, `du`, `stat` or `kill`.
- **Trace mode** — `trace on` prints every `pipe`, `fork`, `exec`, `wait` and signal as it happens, plus CPU time and peak memory for each command.
- **An AI assistant** — `ai <plain English>` turns a request into shell commands, shows them, and runs them only after you confirm.

Built by **Samrender Singh Saini** as an Operating Systems project at VIT Vellore.

---

## Demo

```text
$ trace on
trace is ON - showing fork, pipe, exec, wait, signals and stats

$ ls | wc -l
[parse]    pipeline with 2 commands
[pipe]     pipe 1 created: read end fd 3, write end fd 4
[fork]     child 1195 created for 'ls' -> process group 1195
[terminal] terminal given to process group 1195 (Ctrl+C now goes to it, not the shell)
[fork]     child 1196 created for 'wc' -> process group 1195
[exec]     child 1195 replaces itself with /usr/bin/ls
[exec]     child 1196 replaces itself with /usr/bin/wc
       3
[wait]     child 1195 (ls) exited with status 0 - reaped, no zombie left
[wait]     child 1196 (wc) exited with status 0 - reaped, no zombie left
[terminal] terminal returned to the shell (process group 1194)
[stats]    real 0.003s | user CPU 0.000s | system CPU 0.004s | peak memory 2.2 MB

$ sleep 5
^C
[signal]   child 1198 (sleep) was killed by SIGINT (Ctrl+C)

$ cd demo
[builtin]  'cd' runs inside the shell itself - no fork, because it changes the shell's own state
```

---

## Features

### Core shell
| Feature | Example |
|---|---|
| Run any program, by name or path | `ls -l`, `gcc main.c`, `./a.out`, `/bin/echo hi` |
| Pipelines | `ls \| grep c \| wc -l` |
| Redirection | `sort < in.txt > out.txt`, `echo hi >> log.txt` |
| Conditional chaining (bash rules) | `make && ./app \|\| echo failed`, `a ; b` |
| Background jobs | `sleep 10 &` |
| Ctrl+C stops only the running job | the shell itself never dies |
| Environment variables | `export NAME=value`, `echo $NAME`, `unset NAME` |
| Run a script | `source script.txt` |
| Own line editor | ↑ ↓ history, ← → cursor, backspace, Ctrl+W |

### ProcShell's own commands
Implemented in [`src/mycommands.c`](src/mycommands.c). None of them start an external program.

| Command | Like | System calls |
|---|---|---|
| `display [-a] [folder]` | `ls` (permissions, size, date, colours) | `opendir`, `readdir`, `lstat` |
| `show <file>...` | `cat` | `open`, `read`, `write` |
| `makedir <folder>...` | `mkdir` | `mkdir` |
| `makefile <file>...` | `touch` | `open`, `futimens` |
| `remove [-y] <path>...` | `rm` (asks before deleting) | `unlink`, `rmdir` |
| `copy <src> <dest>` | `cp` | `open`, `read`, `write`, `fstat` |
| `move <src> <dest>` | `mv` | `rename` (copy + `unlink` across disks) |
| `fileinfo <path>...` | `stat` (inode, owner, links, times) | `lstat`, `getpwuid`, `getgrgid` |
| `tree [-d depth] [folder]` | `tree` | `opendir`, `readdir`, `lstat` (recursive) |
| `search <text> [folder]` | `find -name` | `opendir`, `readdir` (recursive) |
| `diskuse [folder]` | `du` (with a size bar per item) | `lstat` (recursive) |
| `count [file]...` | `wc` | `read` |
| `peek [-n N] [file]...` | `head` | `read`, `write` |
| `chmode <octal> <path>...` | `chmod` (shows old → new) | `stat`, `chmod` |
| `makelink [-s] <target> <name>` | `ln` (shows inode / link count) | `link`, `symlink`, `lstat` |
| `where` / `goto [folder]` | `pwd` / `cd` | `getcwd` / `chdir` |
| `help` | — | lists every command |

They compose with the rest of the shell: `display | count`, `history | peek -n 3`, `tree > layout.txt`.

### Process and OS commands
Implemented in [`src/oscommands.c`](src/oscommands.c).

| Command | What it shows | System calls |
|---|---|---|
| `trace [on\|off]` | every `pipe`, `fork`, `exec`, `wait` and signal, plus CPU/memory per command | `wait4` |
| `timeit <command line>` | real time, user/system CPU, peak memory, process count | `wait4`, `clock_gettime` |
| `sysinfo` | OS, architecture, CPUs, physical memory, page size, load average, shell uptime | `uname`, `sysconf`, `getloadavg`, `getrlimit` |
| `procinfo [pid]` | PID, parent, process group, session, terminal ownership, nice value, umask, resource limits | `getpgid`, `getsid`, `tcgetpgrp`, `getpriority`, `getrlimit` |
| `signal [-g] <pid> <sig>` | send `stop`, `cont`, `term`, `kill`, … to a process or a whole job (`signal -l` lists them) | `kill` |
| `history [n]` | numbered command history | history linked list |

Pausing and resuming a background job:
```text
$ sleep 60 &
Process Group: 4417
$ signal -g 4417 stop          # every process of the job is now stopped (state T)
sent SIGSTOP (19) to process group 4417
$ procinfo 4417
  Process group    4417  (group leader)
  Has terminal     no (background)
$ signal -g 4417 cont          # running again
$ timeit "seq 1 200000 | sort -r > /dev/null"
─── timeit ───────────────────────
  real time         0.047 s
  user CPU          0.034 s
  system CPU        0.011 s
  peak memory         9.5 MB
  processes             2
  exit status           0
──────────────────────────────────
```

### AI assistant
```text
$ ai create a folder named demo, write a C hello world in it, then compile and run it
```
1. The request is sent with context — the last 5 commands, the OS and the current folder — to an LLM
   (**Groq** first for speed, **Google Gemini** as an automatic fallback).
2. ProcShell shows the generated commands, a one-line explanation and any warning.
3. **Nothing runs until you type `y`.** The default answer is No.
4. Commands run under `bash` with `set -e`; their stderr is captured through a pipe. If they fail,
   the error is sent back so the model can correct itself (up to 4 attempts).
5. If the model is unsure, it asks a clarifying question instead of guessing.

---

## Engineering highlights

- **Process groups and terminal control.** Each job gets its own process group (`setpgid` from both parent and child, avoiding the classic race) and is handed the terminal with `tcsetpgrp`, so Ctrl+C reaches the job and never the shell.
- **A real race condition, found and fixed.** The `SIGCHLD` handler could reap a foreground child before the shell's own `wait`, silently losing exit statuses (`false && echo yes` sometimes printed `yes`). Fixed by blocking `SIGCHLD` with `sigprocmask` while a pipeline runs and restoring the original mask in children before `execve`. Verified across 40 repeated runs.
- **bash-compatible chaining.** `&&`/`||` skip a pipeline rather than abort the chain, so `false && a || b` runs `b`; a failing command does not stop a `;` list, but Ctrl+C (exit 130) does.
- **Whole-job signalling.** Every process of a background job joins the job's process group, so `signal -g <pgid> stop` pauses the entire job, exactly like job control in a real shell.
- **Per-command resource accounting** with `wait4` (`ru_utime`, `ru_stime`, `ru_maxrss`, handled for both Linux and macOS units).
- **Raw-mode line editor** with line-wrap-aware cursor movement and a fixed-size doubly linked list for history.
- **Clean build** with `-Wall -Wextra` and no warnings.

---

## How a command runs

```text
keystrokes ──► line editor (termios raw mode)
           ──► tokenizer  ($VAR expansion, quotes, operators)
           ──► command tree   Commands ▸ Command ▸ Pipeline ▸ Component ▸ Token
           ──► dispatch
                 ├─ built-in (cd, export, …)      runs in the shell, no fork
                 ├─ own command (display, …)      fork → our C code
                 ├─ program (ls, gcc, …)          fork → execve
                 └─ ai                            LLM → confirm → bash
           ──► pipe() · fork() · setpgid() · tcsetpgrp() · dup2() · execve()
           ──► wait4() · terminal back to the shell · prompt
```

| OS concept | System calls |
|---|---|
| Process creation | `fork`, `execve` |
| Waiting, exit status, zombie reaping | `wait4`, `waitpid`, `SIGCHLD` handler |
| Inter-process communication | `pipe` |
| File descriptors and redirection | `open`, `dup2`, `close` |
| Process groups and terminal control | `setpgid`, `tcsetpgrp`, `getpgrp` |
| Signals | `sigaction` (`SIGINT`, `SIGCHLD`, `SIGWINCH`), `kill` for `signal`, `SIGTTOU` / `SIGTSTP` ignored |
| Process attributes and limits | `getpgid`, `getsid`, `getpriority`, `getrlimit`, `umask` |
| System information | `uname`, `sysconf`, `getloadavg` |
| Synchronization | `sigprocmask` |
| Terminal I/O | `tcgetattr`, `tcsetattr` (raw mode), `ioctl(TIOCGWINSZ)` |
| File system | `opendir`, `readdir`, `lstat`, `mkdir`, `unlink`, `rmdir`, `rename`, `chmod`, `link`, `symlink` |

Design diagrams are in [`docs/diagrams/`](docs/diagrams).

---

## Getting started

**Requirements:** macOS or Linux, a C compiler, `make`. Python 3 only for the `ai` command.

```bash
git clone https://github.com/samrender-tech/procshell.git
cd procshell
make
./procshell
```

`make re` rebuilds from scratch. Type `help` inside the shell for the command list, `exit` to leave.

### Enabling the AI command
```bash
pip3 install -r requirements.txt
cp .env.example .env      # add GROQ_API_KEY and/or GEMINI_API_KEY
```
Free keys: [Groq](https://console.groq.com) (fastest) or [Google AI Studio](https://aistudio.google.com/apikey). One is enough.
`.env` is git-ignored, and the AI's working files live in `~/.procshell/`, not in your project folders.

---

## Project structure

```text
procshell/
├── src/
│   ├── main.c           start-up, signal setup, main loop
│   ├── input.c          raw-mode line editor and history keys
│   ├── parser.c         tokenizer, $VAR expansion, command tree
│   ├── executor.c       fork / exec / wait, pipes, redirection, process groups, trace points
│   ├── builtins.c       cd, export, unset, source, echo, which, env, ai
│   ├── mycommands.c     file commands (display, tree, diskuse, …) and trace mode
│   ├── oscommands.c     sysinfo, procinfo, signal, timeit, history
│   ├── helpers.c        signal handler, command dispatch, allocation helpers
│   ├── aiParser.c       parses the AI's structured reply
│   └── forgettingDLL.c  fixed-capacity doubly linked list for history
├── include/             headers (include/types/ holds the data structures)
├── ai/interface.py      LLM client (Groq, Gemini fallback)
├── docs/                diagrams, known issues
├── tests/               test cases
└── Makefile
```

---

## Roadmap

- [ ] Job control: `jobs`, `fg`, `bg` and Ctrl+Z — background jobs can already be paused and resumed with `signal -g <pgid> stop|cont`
- [ ] Persistent history across sessions, `alias`, wildcard globbing (`*.c`), tab completion
- [ ] stderr redirection (`2>`, `2>&1`) and full bash quoting rules
- [ ] Automated test suite
- [ ] AI: `why` to explain the last failure; a C-side safety check on generated commands

Known limitations are tracked in [`docs/bugs.md`](docs/bugs.md).

---

## License

[MIT](LICENSE) © 2026 Samrender Singh Saini
