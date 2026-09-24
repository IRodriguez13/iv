#!/usr/bin/env bash
set -euo pipefail

BIN="${1:?usage: smoke.sh /path/to/iv}"
case "$BIN" in
  /*) ;;
  *) BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")" ;;
esac

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$TMP"

# view
printf 'a\nb\nc\n' > sample.txt
[[ "$("$BIN" -v sample.txt --no-numbers)" == $'a\nb\nc' ]] || { echo "FAIL: -v"; exit 1; }

# append
"$BIN" -a sample.txt "d" -q
[[ "$(tail -1 sample.txt)" == "d" ]] || { echo "FAIL: -a"; exit 1; }

# replace range
"$BIN" -r sample.txt 2 "B" -q
[[ "$(sed -n '2p' sample.txt)" == "B" ]] || { echo "FAIL: -r"; exit 1; }

# delete with -m
"$BIN" -d sample.txt -m "B" -q
grep -q '^B$' sample.txt && { echo "FAIL: -d -m"; exit 1; }

# substitute
"$BIN" -s sample.txt c C -q
grep -q '^C$' sample.txt || { echo "FAIL: -s"; exit 1; }

# line range view
out="$("$BIN" -va 2-3 sample.txt --no-numbers)"
[[ "$out" == $'C\nd' ]] || { echo "FAIL: -va range"; echo "$out"; exit 1; }

# dry-run leaves file unchanged
cp sample.txt before.txt
"$BIN" -s sample.txt C Z --dry-run -q
cmp -s before.txt sample.txt || { echo "FAIL: --dry-run"; exit 1; }

# GNU -b simple backup next to the file
printf 'one\n' > bak.txt
"$BIN" -s bak.txt one TWO -q -b
[[ "$(cat bak.txt)" == "TWO" ]] || { echo "FAIL: -b edit"; exit 1; }
[[ "$(cat bak.txt~)" == "one" ]] || { echo "FAIL: -b backup"; exit 1; }

# binary rejection
printf 'ok\0bad' > bin.txt
if "$BIN" -a bin.txt x -q 2>/dev/null; then
	echo "FAIL: binary should be rejected"
	exit 1
fi

echo "smoke OK"
