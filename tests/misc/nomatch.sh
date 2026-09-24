#!/bin/sh
# No-match is success and does not replace the inode.
. "${srcdir=.}/tests/init.sh"

printf 'same\n' > in || framework_failure_
cp in exp || framework_failure_
ino=$(ls -i in | awk '{print $1}') || framework_failure_

returns_ 0 $IV -s in missing X -q || fail=1
compare exp in || fail=1
ino2=$(ls -i in | awk '{print $1}') || framework_failure_
test "$ino" = "$ino2" || fail=1

: > empty || framework_failure_
returns_ 0 $IV -s empty a b -q || fail=1
compare /dev/null empty || fail=1

Exit $fail
