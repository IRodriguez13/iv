#!/bin/sh
# "-" is a filter: stdout only, never a file named "-".
. "${srcdir=.}/tests/init.sh"

printf 'old\n' | $IV -s - old new -q > out || fail=1
printf 'new\n' > exp || framework_failure_
compare exp out || fail=1
test ! -e - || fail=1

printf 'a\nb\nc\n' | $IV -va -2--1 - --no-numbers > out2 || fail=1
printf 'b\nc\n' > exp2 || framework_failure_
compare exp2 out2 || fail=1

Exit $fail
