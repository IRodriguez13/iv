#!/bin/sh
# Cases an experienced reviewer types at the binary.
. "${srcdir=.}/tests/init.sh"

# Missing field: line is copied through.
printf 'a,b\n' > in || framework_failure_
cp in exp || framework_failure_
returns_ 0 $IV -s in -F , 9 X -q || fail=1
compare exp in || fail=1

# Replace a match with the empty string.
printf 'abXcd\n' > in || framework_failure_
returns_ 0 $IV -s in X '' -q || fail=1
printf 'abcd\n' > exp || framework_failure_
compare exp in || fail=1

# Recursive symlink.
ln -s loop loop || framework_failure_
returns_ 1 $IV -s loop a b -q || fail=1

# File whose name is "-": must be passed as ./- so stdin is not implied.
printf 'old\n' > ./- || framework_failure_
returns_ 0 $IV -s ./- old new -q || fail=1
printf 'new\n' > exp || framework_failure_
compare exp ./- || fail=1

# -g replaces every occurrence on the line.
printf 'foo foo\n' > in || framework_failure_
returns_ 0 $IV -s in foo FOO -g -q || fail=1
printf 'FOO FOO\n' > exp || framework_failure_
compare exp in || fail=1

# Without -g, first match only.
printf 'foo foo\n' > in || framework_failure_
returns_ 0 $IV -s in foo FOO -q || fail=1
printf 'FOO foo\n' > exp || framework_failure_
compare exp in || fail=1

Exit $fail
