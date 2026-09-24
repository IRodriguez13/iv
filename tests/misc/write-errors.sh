#!/bin/sh
# /dev/full and closed stdout are errors; the source file is untouched.
. "${srcdir=.}/tests/init.sh"

test -w /dev/full && test -c /dev/full || skip_ '/dev/full is required'

printf 'old\n' > src || framework_failure_
cp src exp || framework_failure_

returns_ 1 $IV -s src old new --stdout -q >/dev/full || fail=1
compare exp src || fail=1

# Closed stdout (EBADF), not EPIPE.
returns_ 1 $IV -s src old new --stdout -q >&- || fail=1
compare exp src || fail=1

Exit $fail
