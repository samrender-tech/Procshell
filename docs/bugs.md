# Known bugs

1. Nested quotes / backslashes inside double quotes:
   `echo "hello 'world \n' world'"` prints `hello 'world n' world'`
   (bash prints `hello 'world \n' world'`).
2. Mixed quoting in one word disables variable expansion for the whole word:
   `echo $HOME'x'` prints `$HOMEx` (bash expands `$HOME`).
3. No `2>` / `2>&1` (stderr redirection). `cmd 2>&1` treats `&` as "run in background".
4. No `fg` / `bg` / `jobs` yet. Ctrl+Z is ignored for now.
5. `~` is not expanded (`cd ~/Desktop` does not work; plain `cd` does go home).
6. Multi-byte (UTF-8) characters confuse the cursor position in the line editor.

# Fixed (Oct 2026)

- `false; echo hi` now runs `echo` (a failing command no longer stops a `;` list)
- `&&` / `||` chains follow bash rules (`false && a || b` runs `b`)
- Ctrl+C now stops the rest of the line
- Exit statuses lost randomly (SIGCHLD handler race)
- Ctrl+Z made the shell hang
- `/bin/ls`, `../script.sh` reported "command not found"
- Out-of-bounds read in PATH lookup
- `export path=x` deleted `PATH` (env names were case-insensitive)
- `source` failed on every external command (kept the trailing newline)
- `--msg="a b"` was split into two arguments
- A lone `$` disappeared
- Background jobs discarded what you were typing
- `ai` only worked from the project folder; temp files cluttered the current folder
- AI ran commands on Enter by default (now `[y/N]`, default No)
- AI retry only noticed if the *last* command failed (now `set -e`)
- Buffer overflow on long `ai` queries
- Memory leaks in vectors and AI structs
