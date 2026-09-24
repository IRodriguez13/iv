#!/bin/sh
# Destination inode change before renameat aborts; the new file is not clobbered.
. "${srcdir=.}/tests/init.sh"

# Enough data that a temp appears before rename.
i=0
while test $i -lt 80000; do
  printf 'keep\n'
  i=$((i + 1))
done > conc || framework_failure_

$IV -s conc keep KEEP -q &
ivpid=$!
swapped=0
n=0
while test $n -lt 80; do
  if ls .iv.* >/dev/null 2>&1; then
    rm -f conc
    printf 'other\n' > conc
    swapped=1
    break
  fi
  if ! kill -0 $ivpid 2>/dev/null; then
    break
  fi
  n=$((n + 1))
  sleep 0.01
done
wait $ivpid
st=$?

if test $swapped = 1; then
  # iv must fail the inode check, not overwrite "other".
  test $st -ne 0 || fail=1
  printf 'other\n' > exp || framework_failure_
  compare exp conc || fail=1
fi
# If iv finished first, the file is fully KEEP or keep — not a mix.
if test $swapped = 0; then
  first=$(head -n 1 conc)
  case $first in
    keep|KEEP) ;;
    *) fail=1 ;;
  esac
fi

leftover=$(ls -d .iv.* 2>/dev/null | wc -l)
test "$leftover" -eq 0 || fail=1

Exit $fail
