# iv

Line-oriented text editor for the command line. No external dependencies.
Designed for composition with pipes and scripts.

> Spanish documentation: [README.es.md](README.es.md)

## Build

```bash
make
make test
make install          # default: ~/.local
PREFIX=/usr/local make install
./install.sh
make clean
```

## Shell completions

`make install` installs completions for **bash**, **zsh**, and **fish**:

| Shell | Path (default `PREFIX=~/.local`) |
|-------|----------------------------------|
| bash  | `~/.local/share/bash-completion/completions/iv` |
| zsh   | `~/.local/share/zsh/site-functions/_iv` |
| fish  | `~/.local/share/fish/vendor_completions.d/iv.fish` |

**zsh** — in `~/.zshrc` before `compinit`:

```bash
fpath=(~/.local/share/zsh/site-functions $fpath)
autoload -Uz compinit && compinit
```

**bash** — requires the `bash-completion` package.  
**fish** — auto-loads from `vendor_completions.d`.

## Commands

### View

| Command | Description |
|---------|-------------|
| `iv -v file` | Show entire file with line numbers |
| `iv -v file --no-numbers` | Show file without line numbers |
| `iv -va start-end file` | Show line range |
| `iv -wc file` | Count lines |
| `iv -n file "pattern"` | Line numbers where pattern appears |
| `iv -n file "pattern" --json` | JSON output: `{"lines":[1,5,7]}` (for jq, Python, etc.) |
| `iv -nv file "pattern"` | Show matching lines (grep-like), with line numbers |
| `iv -u file [N]` | Undo: restore from backup slot N (default 1); N=1..10 |
| `iv -diff [-u] [N] file` | Compare backup N vs current; `-u` = unified diff |
| `iv -l [file] [--persist]` | List backups (path and size). Default: **ephemeral + persisted**; with `--persist` only persisted |
| `iv -lsbak [file] [N] [--persist]` | List backups **with metadata** (date and user). With N, show slot content |
| `iv -rmbak [file] [--persist]` | Remove backups (alias: `-z`) |
| `iv --persist file` | Move backup repo from `/tmp` to `~/.local/share/iv/` |
| `iv --unpersist file` | Move backup repo from `~/.local/share/iv/` to `/tmp` |
| `iv -V` / `iv --version` | Show version |

### Edit

| Command | Description |
|---------|-------------|
| `iv -i file "text"` | Insert text at end (alias: `-insert`) |
| `iv -i file start-end "text"` | Insert text before line `start` |
| `iv -a file "text"` | Append text at end of file |
| `iv -p file [file...] [range] content` | Patch one or more files; optional range |
| `iv -pi file [file...] line content` | Patch insert: insert before line (does not replace) |
| `iv -d file [start-end]` | Delete lines (alias: `-delete`) |
| `iv -d file -m "pattern"` | Delete only matching lines |
| `iv -r file [start-end] "text"` | Replace lines (alias: `-replace`) |
| `iv -r file -m "pattern" "text"` | Replace only matching lines |
| `iv -s file pattern replacement` | Substitute (literal) |
| `iv -s file pattern replacement -m "filter"` | Substitute only on lines containing filter |
| `iv -s file -F ',' 2 "X"` | Replace field 2 with "X" (CSV/TSV) |
| `iv -s file pat repl -e pat2 repl2` | Multiple substitutions (like sed -e) |
| `iv -s file pattern replacement -E` | Regex substitute |
| `iv -s file pattern replacement -g` | Replace all matches per line |

### Global options

| Option | Effect |
|--------|--------|
| `--dry-run` | Show what would be done without modifying the file |
| `--no-backup` | Skip backup before edit |
| `--no-numbers` | Omit line numbers (with `-v` and `-va` only) |
| `-q` | Suppress tee-like output on `-i`, `-a`, `-r`, `-p` |
| `--stdout` | Write to stdout without modifying the file (pipeline-friendly) |

## Ranges

Ranges are 1-based:

| Format | Meaning |
|--------|---------|
| `1-5` | Lines 1 through 5 |
| `5` | Line 5 |
| `-3` | Third line from end |
| `-3--1` | Last three lines |
| `-5-` | Last five lines |
| `2-` | From line 2 to end |

## Text input: stdin, file, or literal

Text arguments for `-i`, `-a`, and `-r` accept:

| Argument | Behavior |
|----------|----------|
| `-` | Read from stdin |
| Path to existing file | Read file content |
| Any other text | Used as literal |

```bash
echo "new line" | iv -p file
iv -p main.c snippet.c
iv -p main.c 5 snippet.c
iv -p main.c 1-3 template.txt
iv -pi main.c 1 "#include <foo.h>"
iv -p f1.c f2.c snippet.c
iv -s file "[0-9]+" "X" -E
iv -s file "a" "b" -e "c" "d"
iv -nv file "TODO"
cat file | iv -s - "old" "new" --stdout
```

## Escape sequences

In insert/replace text:

| Sequence | Character |
|----------|-----------|
| `\n` | Newline |
| `\t` | Tab |
| `\\` | Backslash |
| `\r` | Carriage return |

## Tee-like behavior

`-insert`, `-replace`, `-a`, `-p`, and `-pi` echo added text to stdout (like `tee`). Use `-q` to suppress.

## Pipelines with `--stdout`

```bash
iv -s file "a" "b" --stdout | iv -s - "b" "c" --stdout
```

## Code layout

```
iv.h      — declarations, constants, IvOpts
main.c    — entry, argument parsing, dispatch
view.c    — show_file, show_range, wc_lines, find_line_numbers
edit.c    — backup, apply_patch, search_replace, list_backups
range.c   — parse_range
```

## Man pages

| Locale | Path |
|--------|------|
| English (default) | `iv.1` → `man iv` |
| Spanish | `man/es/iv.1` → `man -L es iv` (when installed) |

## Diff format

`iv -diff file` compares backup 1 vs current. `-u` uses unified diff (`diff -u` compatible).

## Backup

- Backups may be **ephemeral** (default) or **persisted**.
- Ephemeral root: `/tmp/iv_<user>/` (override with `IV_BACKUP_DIR`).
- Persisted root: `$XDG_DATA_HOME/iv` or `~/.local/share/iv/`.
- Per-file subdirectories; slots as `N.bak` with optional `N.meta` (epoch + user).
- `iv -u file` restores slot 1; `iv -u file 2` restores slot 2.
- `iv --persist file` / `iv --unpersist file` move backup storage.

## Safety

- **Binary files**: iv refuses to edit files containing NUL bytes.

## Exit codes

- `0`: success
- `1`: error (binary file, invalid range, usage error, etc.)

## Limits

- Lines: dynamic array (no fixed cap)
- Line length: unbounded (`getline` POSIX)

## License

GPLv3+ — see [LICENSE](LICENSE).
