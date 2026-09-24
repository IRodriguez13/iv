#!/bin/sh
# SIGTERM during commit: no leftover .iv.*; original intact or fully committed.
. "${srcdir=.}/tests/init.sh"

i=0
while test $i -lt 200000; do
  printf 'keep\n'
  i=$((i + 1))
done > sig || framework_failure_

$IV -s sig keep KEEP -q &
ivpid=$!
n=0
while test $n -lt 80; do
  if ls .iv.* >/dev/null 2>&1; then
    kill -TERM $ivpid 2>/dev/null || true
    break
  fi
  if ! kill -0 $ivpid 2>/dev/null; then
    break
  fi
  n=$((n + 1))
  sleep 0.01
done
wait $ivpid 2>/dev/null || true

leftover=$(ls -d .iv.* 2>/dev/null | wc -l)
test "$leftover" -eq 0 || fail=1
first=$(head -n 1 sig)
case $first in
  keep|KEEP) ;;
  *) fail=1 ;;
esac
if test "$first" = keep; then
  last=$(tail -n 1 sig)
  test "$last" = keep || fail=1
fi

Exit $fail
