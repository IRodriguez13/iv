#!/bin/sh
# Empty pattern is rejected. Invalid ERE is rejected. File unchanged.
. "${srcdir=.}/tests/init.sh"

printf 'ab\n' > in || framework_failure_
cp in exp || framework_failure_

returns_ 1 $IV -s in '' X -q || fail=1
compare exp in || fail=1

returns_ 1 $IV -s in '' X -E -q || fail=1
compare exp in || fail=1

returns_ 1 $IV -s in '(' X -E -q || fail=1
compare exp in || fail=1

Exit $fail
