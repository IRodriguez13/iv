#!/usr/bin/env bash
set -euo pipefail

BIN="${1:?usage: safety.sh /path/to/iv}"
case "$BIN" in
  /*) ;;
  *) BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")" ;;
esac

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cd "$TMP"

fail() { echo "FAIL: $1"; exit 1; }

# ── write failure leaves original intact ──
mkdir locked
printf 'keep-me\nsecond\n' > locked/f.txt
chmod a-w locked
if "$BIN" -s locked/f.txt keep KEEP -q 2>/dev/null; then
	chmod u+w locked
	fail "write into read-only dir should fail"
fi
chmod u+w locked
[[ "$(cat locked/f.txt)" == $'keep-me\nsecond' ]] || fail "original mutated after failed write"

# ── symlink: follow referent, keep the link inode ──
printf 'hello\n' > real.txt
ln -s real.txt link.txt
"$BIN" -s link.txt hello world -q
[[ -L link.txt ]] || fail "symlink replaced by regular file"
[[ "$(cat real.txt)" == "world" ]] || fail "referent not updated"
[[ "$(readlink link.txt)" == "real.txt" ]] || fail "symlink target changed"

# ── hardlink: this name is replaced; the other name keeps old bytes ──
printf 'hello\n' > ha.txt
ln ha.txt hb.txt
"$BIN" -s ha.txt hello world -q
[[ "$(cat ha.txt)" == "world" ]] || fail "hardlink name not updated"
[[ "$(cat hb.txt)" == "hello" ]] || fail "other hardlink should keep old inode"

# ── mode preserved ──
printf 'abc\n' > mode.txt
chmod 640 mode.txt
oldmode="$(stat -c %a mode.txt)"
"$BIN" -s mode.txt abc ABC -q
[[ "$(stat -c %a mode.txt)" == "$oldmode" ]] || fail "mode not preserved"

# ── no final newline ──
printf 'no-nl' > nonewline.txt
"$BIN" -s nonewline.txt no-nl yes -q
[[ "$(cat nonewline.txt)" == "yes" ]] || fail "file without trailing newline"

# ── empty file ──
: > empty.txt
"$BIN" -s empty.txt a b -q
[[ ! -s empty.txt ]] || fail "empty file grew on no-match"

# ── no-match does not rewrite inode ──
printf 'same\n' > nomatch.txt
ino_before="$(stat -c %i nomatch.txt)"
"$BIN" -s nomatch.txt missing X -q
ino_after="$(stat -c %i nomatch.txt)"
[[ "$ino_before" == "$ino_after" ]] || fail "no-match should not replace inode"

# ── regex captures: \1 \2 and & ──
printf 'foo42\n' > re.txt
"$BIN" -s re.txt '([a-z]+)([0-9]+)' '\2-\1' -E -q
[[ "$(cat re.txt)" == "42-foo" ]] || fail "regex backref \\1/\\2"
printf 'foo42\n' > re2.txt
"$BIN" -s re2.txt 'foo[0-9]+' '[&]' -E -q
[[ "$(cat re2.txt)" == "[foo42]" ]] || fail "regex & backref"

# ── -m uses regex when -E ──
printf 'DEBUG: a\nINFO: a\n' > filt.txt
"$BIN" -s filt.txt a B -m '^DEBUG:' -E -q
[[ "$(cat filt.txt)" == $'DEBUG: B\nINFO: a' ]] || fail "-m -E filter"

# ── delimiter fields, not CSV quoting ──
printf 'Juan,"Buenos Aires, Argentina",24\n' > fields.txt
"$BIN" -s fields.txt -F ',' 2 X -q
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
"$BIN" -d tail.txt -2--1 -q
[[ "$(cat tail.txt)" == $'a\nb\nc' ]] || fail "-d -2--1"
printf 'a\nb\nc\nd\ne\n' > tail2.txt
"$BIN" -r tail2.txt -3--1 X -q
[[ "$(cat tail2.txt)" == $'a\nb\nX\nX\nX' ]] || fail "-r -3--1"
printf 'a\nb\nc\nd\ne\n' > hyb.txt
"$BIN" -d hyb.txt 2--2 -q
[[ "$(cat hyb.txt)" == $'a\ne' ]] || fail "-d 2--2 hybrid"
printf 'a\nb\n' > short.txt
[[ "$("$BIN" -va -5- short.txt --no-numbers)" == $'a\nb' ]] || fail "tail longer than file"

# ── owner preserved (same uid/gid, no root required) ──
printf 'own\n' > owner.txt
ouid="$(stat -c %u owner.txt)"
ogid="$(stat -c %g owner.txt)"
"$BIN" -s owner.txt own OWN -q
[[ "$(stat -c %u owner.txt)" == "$ouid" ]] || fail "uid not preserved"
[[ "$(stat -c %g owner.txt)" == "$ogid" ]] || fail "gid not preserved"

# ── dangling symlink: fail, do not replace the link ──
ln -s missing dang.txt
if "$BIN" -s dang.txt a b -q 2>/dev/null; then
	fail "dangling symlink should fail"
fi
[[ -L dang.txt ]] || fail "dangling symlink replaced"
[[ "$(readlink dang.txt)" == "missing" ]] || fail "dangling target changed"
[[ ! -e missing ]] || fail "created missing referent"

# ── relative symlink across directories ──
mkdir -p a/b
printf 'hello\n' > a/target.txt
ln -s ../target.txt a/b/link.txt
"$BIN" -s a/b/link.txt hello world -q
[[ -L a/b/link.txt ]] || fail "cross-dir symlink replaced"
[[ "$(readlink a/b/link.txt)" == "../target.txt" ]] || fail "cross-dir symlink retargeted"
[[ "$(cat a/target.txt)" == "world" ]] || fail "cross-dir referent not updated"

# ── byte-oriented: -F uses first byte; invalid UTF-8 is data ──
# ñ is C3 B1; file has C3 but not the pair, so a character delimiter would no-op.
printf 'p\xc3q\n' > mb.txt
"$BIN" -s mb.txt -F $'\xc3\xb1' 1 X -q
[[ "$(cat mb.txt)" == $'X\xc3q' ]] || fail "delimiter is first byte only"
printf 'ok\xff\n' > badutf.txt
"$BIN" -s badutf.txt ok OK -q
[[ "$(cat badutf.txt)" == $'OK\xff' ]] || fail "invalid UTF-8 should pass as bytes"

# ── SIGPIPE: downstream close is silent success ──
python3 -c 'open("huge.txt","w").write("foo bar\n"*200000)'
set +e
set +o pipefail
pipe_err="$TMP/pipe.err"
pipe_out="$("$BIN" -s huge.txt foo FOO --stdout 2>"$pipe_err" | head -n 1)"
pipe_rc="${PIPESTATUS[0]}"
set -o pipefail
set -e
[[ "$pipe_out" == "FOO bar" ]] || fail "pipe first line: $pipe_out"
[[ "$pipe_rc" == "0" ]] || fail "iv should exit 0 when downstream closes (got $pipe_rc)"
[[ ! -s "$pipe_err" ]] || fail "spurious stderr on EPIPE: $(cat "$pipe_err")"
set +e
set +o pipefail
view_err="$TMP/view.err"
view_out="$("$BIN" -v huge.txt --no-numbers 2>"$view_err" | head -n 1)"
view_rc="${PIPESTATUS[0]}"
set -o pipefail
set -e
[[ "$view_out" == "foo bar" ]] || fail "view pipe first line"
[[ "$view_rc" == "0" ]] || fail "iv -v should exit 0 when downstream closes"
[[ ! -s "$view_err" ]] || fail "spurious stderr on view EPIPE: $(cat "$view_err")"

# ── SIGTERM during commit: no leftover .iv.* ; original intact or fully committed ──
python3 -c 'open("sig.txt","w").write("keep\n"*400000)'
"$BIN" -s sig.txt keep KEEP -q &
sigpid=$!
for _ in $(seq 1 80); do
	if find . -maxdepth 1 -name '.iv.*' | grep -q .; then
		kill -TERM "$sigpid" 2>/dev/null || true
		break
	fi
	if ! kill -0 "$sigpid" 2>/dev/null; then
		break
	fi
	sleep 0.01
done
wait "$sigpid" 2>/dev/null || true
leftover="$(find . -maxdepth 1 -name '.iv.*' | wc -l)"
[[ "$leftover" == "0" ]] || fail "leftover .iv.* after SIGTERM"
first="$(head -n 1 sig.txt)"
[[ "$first" == "keep" || "$first" == "KEEP" ]] || fail "sig.txt corrupted after signal"
if [[ "$first" == "keep" ]]; then
	[[ "$(tail -n 1 sig.txt)" == "keep" ]] || fail "partial rewrite after signal"
fi

# ── ENOSPC during real commit (user+mount ns + tiny tmpfs) ──
if unshare --user --mount --map-root-user true >/dev/null 2>&1; then
	unshare --user --mount --map-root-user /bin/bash -c '
		set -euo pipefail
		mkdir -p /tmp/ivtiny
		mount -t tmpfs -o size=64k tmpfs /tmp/ivtiny
		printf "a\n" > /tmp/ivtiny/f.txt
		huge=$(python3 -c "print(\"b\"*80000)")
		if "'"$BIN"'" -s /tmp/ivtiny/f.txt a "$huge" -q 2>/dev/null; then
			echo "ENOSPC should fail" >&2
			exit 1
		fi
		[[ "$(cat /tmp/ivtiny/f.txt)" == "a" ]]
	' || fail "ENOSPC commit mutated original or succeeded"
else
	echo "skip ENOSPC (unshare user+mount ns unavailable)"
fi

echo "safety OK"
