#!/bin/sh
# Follow the referent; keep the symlink inode. Reject dangling links.
. "${srcdir=.}/tests/init.sh"

printf 'hello\n' > real || framework_failure_
ln -s real link || framework_failure_
returns_ 0 $IV -s link hello world -q || fail=1
test -L link || fail=1
printf 'world\n' > exp || framework_failure_
compare exp real || fail=1
test "$(readlink link)" = real || fail=1

ln -s missing dang || framework_failure_
returns_ 1 $IV -s dang a b -q || fail=1
test -L dang || fail=1
test "$(readlink dang)" = missing || fail=1
test ! -e missing || fail=1

mkdir -p a/b || framework_failure_
printf 'hello\n' > a/target || framework_failure_
ln -s ../target a/b/link || framework_failure_
returns_ 0 $IV -s a/b/link hello world -q || fail=1
test -L a/b/link || fail=1
test "$(readlink a/b/link)" = ../target || fail=1
compare exp a/target || fail=1

Exit $fail
