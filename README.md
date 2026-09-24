# iv

Line-oriented text editor for the command line. Runtime depends only on libc.
Edits compose with pipes.

Spanish: [README.es.md](README.es.md).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
Copyright (C) 2026 Iván Ezequiel Rodriguez.

`--help`, `--version`, and diagnostics are English. Distros translate the
manual (`man -L es iv`), not `--version` (scripts parse the first line).

## Build

```bash
make
make test             # smoke + safety + completions + misc
make test-musl        # same suite, musl-gcc binary
make bench            # iv vs GNU sed / mawk (1M+3M, equiv then time)
make install          # default PREFIX=~/.local
PREFIX=/usr/local make install
./install.sh
make clean
```

## Completions

Installed by `make install`:

| Shell | Path (`PREFIX=~/.local`) |
|-------|--------------------------|
| bash  | `~/.local/share/bash-completion/completions/iv` |
| zsh   | `~/.local/share/zsh/site-functions/_iv` |
| fish  | `~/.local/share/fish/vendor_completions.d/iv.fish` |

zsh: add that directory to `fpath` before `compinit`. bash needs the
`bash-completion` package. fish loads `vendor_completions.d` automatically.

## Invocation

### View

| Command | Effect |
|---------|--------|
| `iv -v file` | Print the file with line numbers |
| `iv -v file --no-numbers` | Print the file without numbers |
| `iv -va start-end file` | Print a line range |
| `iv -V`, `iv --version` | Print version and license |
| `iv -h`, `iv --help` | Print usage |

Count or search with the usual tools: `iv -v file --no-numbers | wc -l`,
`… | grep`.

### Edit

| Command | Effect |
|---------|--------|
| `iv -i file text` | Append *text* (alias `-insert`) |
| `iv -i file start-end text` | Insert *text* before line *start* |
| `iv -a file text` | Append *text* |
| `iv -p file [file...] [range] content` | Patch one or more files; *range* optional |
| `iv -pi file [file...] line content` | Insert *content* before *line* (no replace) |
| `iv -d file [start-end]` | Delete lines (alias `-delete`) |
| `iv -d file -m pattern` | Delete lines that contain *pattern* |
| `iv -r file [start-end] text` | Replace lines (alias `-replace`) |
| `iv -r file -m pattern text` | Replace matching lines |
| `iv -s file pattern replacement` | Literal substitute (first match per line) |
| `iv -s file pattern replacement -m filter` | Substitute only on lines that contain *filter* |
| `iv -s file -F ',' 2 X` | Replace field 2 (single-byte delimiter; no CSV quotes) |
| `iv -s file pat repl -e pat2 repl2` | Further pairs |
| `iv -s file pattern replacement -E` | POSIX ERE; replacement may use `\1`–`\9` and `&` |
| `iv -s file pattern replacement -g` | Every match on the line |

An empty substitute pattern is rejected (exit 1). `-m` uses the same language
as the substitute (literal, or ERE with `-E`).

### Options

| Option | Effect |
|--------|--------|
| `--dry-run` | Print the result; do not write the file |
| `-b` | GNU backup, method `existing` |
| `--backup[=METHOD]` | GNU backup. No method: `$VERSION_CONTROL`, else `existing` |
| `-S SUFFIX`, `--suffix=SUFFIX` | Backup suffix (also enables backup). Default `$SIMPLE_BACKUP_SUFFIX` or `~` |
| `--no-numbers` | No line numbers (`-v`, `-va`) |
| `-q` | No tee of inserted text (`-i`, `-a`, `-r`, `-p`, `-pi`); no `Replaced N` on `-s` |
| `--stdout` | Write the result to stdout; leave the file unchanged |
| `-g` | Global substitute |
| `-E`, `--regex` | POSIX ERE for `-s` and `-m` |

## Ranges

1-based.

| Form | Meaning |
|------|---------|
| `1-5` | Lines 1–5 |
| `5` | Line 5 |
| `-3` | Third line from the end |
| `-3--1` | Last three lines |
| `-5-` | Last five lines |
| `2-` | Line 2 through EOF |

## Text arguments

For `-i`, `-a`, `-r`, and patch content: `-` is stdin; anything else is
literal. An existing path is not read as a file (`iv -r foo 3 bar` stays
`bar` even if `./bar` exists). File contents go through stdin:

```bash
echo "new line" | iv -p file
iv -p dest - < snippet.c
iv -p dest 5 - < snippet.c
iv -pi main.c 1 "#include <foo.h>"
iv -s file "[0-9]+" "X" -E
iv -s file a b -e c d
cat file | iv -s - old new --stdout
iv -s file a b --stdout | iv -s - b c --stdout
```

A target file whose name is `-` must be passed as `./-`.

## Escapes

In insert/replace text: `\n` newline, `\t` tab, `\\` backslash, `\r` CR.

## Stdout

`-i`, `-a`, `-r`, `-p`, and `-pi` echo the added text to stdout (like `tee`)
unless `-q`.

`--stdout` writes the transformed file to stdout. If the reader closes the
pipe, iv exits 0 and prints no `Broken pipe` diagnostic.

```bash
iv -s huge foo bar --stdout | head -n 1
```

## Backups

In-place edits do not write a backup unless asked (`-b`, `--backup`, or `-S`).
The names and methods are those of GNU Coreutils (`cp`, `mv`, `install`):

| Method | Also | Effect |
|--------|------|--------|
| `none` | `off` | No backup, even if `-b` was given earlier |
| `numbered` | `t` | `file.~1~`, `file.~2~`, … |
| `existing` | `nil` | Numbered if `file.~N~` already exists, else simple |
| `simple` | `never` | `file` + suffix (`~` unless `-S` / `SIMPLE_BACKUP_SUFFIX`) |

`-b` is `--backup=existing`. Unique abbreviations are accepted (`nu`, `no`, …).
`--stdout` and `--dry-run` do not write a backup. A substitute that matches
nothing does not overwrite the file and does not write a backup.

Restore and compare with the filesystem: `mv file~ file`, `diff -u file~ file`.

## In-place writes

read → transform → exclusive temp (`openat` + `O_EXCL` in the parent
directory) → `fsync` → same-inode check → `renameat`.

A failed edit leaves the original path unchanged. If a backup was requested,
it is a copy of the original taken immediately before the rename.

- Only regular files are edited. Directories, FIFOs, and devices are rejected
  before open.
- A file that contains a NUL byte is refused.
- Symlink: the referent is replaced; the symlink inode is kept.
- Dangling symlink: rejected. iv does not replace the link with a regular file.
- Hard link: this pathname gets a new inode; other names keep the old bytes.
- Mode and owner of the referent are copied when permitted. xattrs and ACLs
  are not copied.
- `SIGINT` / `SIGTERM` / `SIGHUP` unlink a leftover `.iv.*` temp. `SIGKILL`
  may leave one; the original path is not renamed.
- This is not a security boundary in a directory writable by an untrusted
  party.

## Text model

iv is deliberately byte-oriented. It sets `LC_ALL=C` and uses C-locale
POSIX ERE. That is the interface, not a hidden implementation choice.

- A line is bytes through `\n`, or through EOF if the last line has no newline.
- `-F` uses the first byte of its delimiter argument.
- POSIX ERE (`-E`) is compiled and matched as C-locale bytes.
- Invalid UTF-8 is data. It is not repaired.

## Exit status

| Status | Meaning |
|--------|---------|
| 0 | Success, including stdout `EPIPE` |
| 1 | Error (usage, missing file, binary, invalid range, write failure, empty pattern, non-regular file) |

## Manuals

| Language | Path |
|----------|------|
| English | `iv.1` → `man iv` |
| Spanish | `man/es/iv.1` → `man -L es iv` |
