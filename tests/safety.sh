#!/usr/bin/env bash
set -euo pipefail

BIN="${1:?usage: safety.sh /path/to/iv}"
case "$BIN" in
  /*) ;;
  *) BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")" ;;
esac

TMP="$(mktemp -d)"
export IV_BACKUP_DIR="$TMP/backups"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

fail() { echo "FAIL: $1"; exit 1; }

# ── write failure leaves original intact ──
mkdir locked
printf 'keep-me\nsecond\n' > locked/f.txt
chmod a-w locked
if "$BIN" -s locked/f.txt keep KEEP --no-backup -q 2>/dev/null; then
	chmod u+w locked
	fail "write into read-only dir should fail"
fi
chmod u+w locked
[[ "$(cat locked/f.txt)" == $'keep-me\nsecond' ]] || fail "original mutated after failed write"

# ── symlink: follow referent, keep the link inode ──
printf 'hello\n' > real.txt
ln -s real.txt link.txt
"$BIN" -s link.txt hello world --no-backup -q
[[ -L link.txt ]] || fail "symlink replaced by regular file"
[[ "$(cat real.txt)" == "world" ]] || fail "referent not updated"
[[ "$(readlink link.txt)" == "real.txt" ]] || fail "symlink target changed"

# ── hardlink: this name is replaced; the other name keeps old bytes ──
printf 'hello\n' > ha.txt
ln ha.txt hb.txt
"$BIN" -s ha.txt hello world --no-backup -q
[[ "$(cat ha.txt)" == "world" ]] || fail "hardlink name not updated"
[[ "$(cat hb.txt)" == "hello" ]] || fail "other hardlink should keep old inode"

# ── mode preserved ──
printf 'abc\n' > mode.txt
chmod 640 mode.txt
oldmode="$(stat -c %a mode.txt)"
"$BIN" -s mode.txt abc ABC --no-backup -q
[[ "$(stat -c %a mode.txt)" == "$oldmode" ]] || fail "mode not preserved"

# ── no final newline ──
printf 'no-nl' > nonewline.txt
"$BIN" -s nonewline.txt no-nl yes --no-backup -q
[[ "$(cat nonewline.txt)" == "yes" ]] || fail "file without trailing newline"

# ── empty file ──
: > empty.txt
"$BIN" -s empty.txt a b --no-backup -q
[[ ! -s empty.txt ]] || fail "empty file grew on no-match"

# ── no-match does not rewrite inode ──
printf 'same\n' > nomatch.txt
ino_before="$(stat -c %i nomatch.txt)"
"$BIN" -s nomatch.txt missing X --no-backup -q
ino_after="$(stat -c %i nomatch.txt)"
[[ "$ino_before" == "$ino_after" ]] || fail "no-match should not replace inode"

# ── regex captures: \1 \2 and & ──
printf 'foo42\n' > re.txt
"$BIN" -s re.txt '([a-z]+)([0-9]+)' '\2-\1' -E --no-backup -q
[[ "$(cat re.txt)" == "42-foo" ]] || fail "regex backref \\1/\\2"
printf 'foo42\n' > re2.txt
"$BIN" -s re2.txt 'foo[0-9]+' '[&]' -E --no-backup -q
[[ "$(cat re2.txt)" == "[foo42]" ]] || fail "regex & backref"

# ── -m uses regex when -E ──
printf 'DEBUG: a\nINFO: a\n' > filt.txt
"$BIN" -s filt.txt a B -m '^DEBUG:' -E --no-backup -q
[[ "$(cat filt.txt)" == $'DEBUG: B\nINFO: a' ]] || fail "-m -E filter"

# ── delimiter fields, not CSV quoting ──
printf 'Juan,"Buenos Aires, Argentina",24\n' > fields.txt
"$BIN" -s fields.txt -F ',' 2 X --no-backup -q
[[ "$(cat fields.txt)" == 'Juan,X, Argentina",24' ]] || fail "field split is delimiter-only"

# ── stdin is stdout ──
out="$(printf 'old\n' | "$BIN" -s - old new -q)"
[[ "$out" == "new" ]] || fail "stdin -s should write stdout"

# ── --stdout write error ──
printf 'old\n' > fullsrc.txt
if "$BIN" -s fullsrc.txt old new --stdout -q >/dev/full 2>/dev/null; then
	fail "--stdout to /dev/full should fail"
fi
[[ "$(cat fullsrc.txt)" == "old" ]] || fail "/dev/full mutated source"

# ── -wc / -n stream ──
printf 'a\nb\na\n' > view.txt
[[ "$("$BIN" -wc view.txt)" == "3" ]] || fail "stream -wc"
[[ "$("$BIN" -n view.txt a --json)" == '{"lines":[1,3]}' ]] || fail "stream -n json"

# ── ring buffer: last-N ranges without loading the file ──
printf 'a\nb\nc\nd\ne\n' > tail.txt
[[ "$("$BIN" -va -3--1 tail.txt --no-numbers)" == $'c\nd\ne' ]] || fail "-va -3--1"
[[ "$("$BIN" -va -3 tail.txt --no-numbers)" == "c" ]] || fail "-va -3"
[[ "$("$BIN" -va 2- tail.txt --no-numbers)" == $'b\nc\nd\ne' ]] || fail "-va 2- to EOF"
[[ "$(printf 'a\nb\nc\nd\n' | "$BIN" -va -2--1 - --no-numbers)" == $'c\nd' ]] || fail "stdin -va tail"
"$BIN" -d tail.txt -2--1 --no-backup -q
[[ "$(cat tail.txt)" == $'a\nb\nc' ]] || fail "-d -2--1"
printf 'a\nb\nc\nd\ne\n' > tail2.txt
"$BIN" -r tail2.txt -3--1 X -q --no-backup
[[ "$(cat tail2.txt)" == $'a\nb\nX\nX\nX' ]] || fail "-r -3--1"
printf 'a\nb\nc\nd\ne\n' > hyb.txt
"$BIN" -d hyb.txt 2--2 --no-backup -q
[[ "$(cat hyb.txt)" == $'a\ne' ]] || fail "-d 2--2 hybrid"
printf 'a\nb\n' > short.txt
[[ "$("$BIN" -va -5- short.txt --no-numbers)" == $'a\nb' ]] || fail "tail longer than file"

echo "safety OK"
