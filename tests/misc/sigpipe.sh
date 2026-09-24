#!/bin/sh
# Closed downstream pipe is success: exit 0, empty stderr.
. "${srcdir=.}/tests/init.sh"

i=0
while test $i -lt 20000; do
  printf 'foo bar\n'
  i=$((i + 1))
done > huge || framework_failure_

mkfifo p || framework_failure_
$IV -s huge foo FOO --stdout >p 2>err &
ivpid=$!
head -n 1 <p >out || framework_failure_
wait $ivpid
test $? -eq 0 || fail=1
printf 'FOO bar\n' > exp || framework_failure_
compare exp out || fail=1
compare /dev/null err || fail=1

# Immediate close of stdout (the ":" consumer).
$IV -s huge foo FOO --stdout 2>err2 | :
compare /dev/null err2 || fail=1

Exit $fail
