#!/bin/sh
# Directories, FIFOs and devices are rejected. Original names stay.
. "${srcdir=.}/tests/init.sh"

mkdir d || framework_failure_
returns_ 1 $IV -s d a b -q || fail=1
test -d d || fail=1

mkfifo fifo || framework_failure_
returns_ 1 $IV -s fifo a b -q || fail=1
test -p fifo || fail=1

test -c /dev/null || skip_ '/dev/null is required'
returns_ 1 $IV -s /dev/null a b -q || fail=1

Exit $fail
