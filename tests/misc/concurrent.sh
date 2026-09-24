#!/bin/sh
# Destination inode change before renameat aborts; the new file is not clobbered.
# IV_TEST_PAUSE_BEFORE_RENAME opens a 200ms window so this is not a race lottery.
. "${srcdir=.}/tests/init.sh"

i=0
while test $i -lt 2000; do
  printf 'keep\n'
  i=$((i + 1))
done > conc || framework_failure_

IV_TEST_PAUSE_BEFORE_RENAME=1
export IV_TEST_PAUSE_BEFORE_RENAME

$IV -s conc keep KEEP -q &
ivpid=$!
swapped=0
n=0
while test $n -lt 80; do
  if ls .iv.* >/dev/null 2>&1; then
    rm -f conc
    # Consume an inode so tmpfs does not recycle conc's number.
    printf 'x\n' > pad || framework_failure_
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

test $swapped -eq 1 || fail=1
test $st -ne 0 || fail=1
printf 'other\n' > exp || framework_failure_
compare exp conc || fail=1

leftover=$(ls -d .iv.* 2>/dev/null | wc -l)
test "$leftover" -eq 0 || fail=1

Exit $fail
