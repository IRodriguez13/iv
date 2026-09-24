#!/bin/sh
# Mode and uid/gid of the referent are copied. No root required.
. "${srcdir=.}/tests/init.sh"

printf 'abc\n' > in || framework_failure_
chmod 640 in || framework_failure_
uid=$(ls -n in | awk '{print $3}') || framework_failure_
gid=$(ls -n in | awk '{print $4}') || framework_failure_
mode=$(ls -l in | awk '{print $1}') || framework_failure_

returns_ 0 $IV -s in abc ABC -q || fail=1
uid2=$(ls -n in | awk '{print $3}') || framework_failure_
gid2=$(ls -n in | awk '{print $4}') || framework_failure_
mode2=$(ls -l in | awk '{print $1}') || framework_failure_
test "$uid" = "$uid2" || fail=1
test "$gid" = "$gid2" || fail=1
test "$mode" = "$mode2" || fail=1

Exit $fail
