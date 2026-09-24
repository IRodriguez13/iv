#!/bin/sh
# This pathname gets a new inode; the other name keeps the old bytes.
. "${srcdir=.}/tests/init.sh"

printf 'hello\n' > ha || framework_failure_
ln ha hb || framework_failure_
returns_ 0 $IV -s ha hello world -q || fail=1
printf 'world\n' > exp_a || framework_failure_
printf 'hello\n' > exp_b || framework_failure_
compare exp_a ha || fail=1
compare exp_b hb || fail=1

Exit $fail
