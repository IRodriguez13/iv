#!/bin/sh
# File without a trailing newline stays well-defined.
. "${srcdir=.}/tests/init.sh"

printf 'no-nl' > in || framework_failure_
returns_ 0 $IV -s in no-nl yes -q || fail=1
printf 'yes' > exp || framework_failure_
compare exp in || fail=1

printf 'keep' > in2 || framework_failure_
returns_ 0 $IV -s in2 missing X -q || fail=1
printf 'keep' > exp2 || framework_failure_
compare exp2 in2 || fail=1

Exit $fail
