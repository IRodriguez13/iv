#!/bin/sh
# -q suppresses Replaced N on -s; without -q the count is printed.
. "${srcdir=.}/tests/init.sh"

printf 'a\na\n' > in || framework_failure_
returns_ 0 $IV -s in a b -q 2>err || fail=1
grep Replaced err && fail=1

printf 'a\na\n' > in2 || framework_failure_
returns_ 0 $IV -s in2 a b 2>err2 || fail=1
grep -F 'Replaced 2 occurrence(s)' err2 || fail=1

Exit $fail
