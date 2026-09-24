#!/bin/sh
# Write into a directory without write permission fails; original intact.
. "${srcdir=.}/tests/init.sh"

mkdir locked || framework_failure_
printf 'keep-me\nsecond\n' > locked/f.txt || framework_failure_
chmod a-w locked || framework_failure_

returns_ 1 $IV -s locked/f.txt keep KEEP -q || fail=1
chmod u+w locked || framework_failure_
printf 'keep-me\nsecond\n' > exp || framework_failure_
compare exp locked/f.txt || fail=1

Exit $fail
