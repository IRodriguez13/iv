#!/bin/sh
# Run tests/misc/*.sh with Coreutils-style helpers.
# Exit 0 if all pass (SKIP=77 is not a failure).

set -u

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
srcdir=$root
export srcdir

IV=${1:-}
if test -z "$IV"; then
  IV=$root/iv
fi
case $IV in
  /*) ;;
  *) IV=$(CDPATH= cd -- "$(dirname "$IV")" && pwd)/$(basename "$IV") ;;
esac
export IV

if test ! -x "$IV"; then
  echo "run-misc.sh: $IV is not executable" >&2
  exit 1
fi

fail=0
n_ok=0
n_skip=0
n_fail=0

for t in "$srcdir"/tests/misc/*.sh; do
  test -f "$t" || continue
  name=$(basename "$t")
  printf '%s' "$name ... "
  set +e
  out=$("$t" 2>&1)
  st=$?
  set -e
  case $st in
    0)
      echo OK
      n_ok=$((n_ok + 1))
      ;;
    77)
      echo SKIP
      echo "$out" | sed 's/^/  /'
      n_skip=$((n_skip + 1))
      ;;
    *)
      echo FAIL
      echo "$out" | sed 's/^/  /'
      n_fail=$((n_fail + 1))
      fail=1
      ;;
  esac
done

echo "misc: $n_ok ok, $n_skip skip, $n_fail fail"
exit $fail
