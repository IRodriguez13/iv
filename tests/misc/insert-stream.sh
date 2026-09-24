#!/bin/sh
# -i / -a / -p / -pi stream; they must not load the file as a line array.
. "${srcdir=.}/tests/init.sh"

printf 'a\nb\nc\n' > in || framework_failure_

returns_ 0 $IV -a in d -q || fail=1
printf 'a\nb\nc\nd\n' > exp || framework_failure_
compare exp in || fail=1

printf 'a\nb\nc\n' > in || framework_failure_
returns_ 0 $IV -i in 1 X -q || fail=1
printf 'X\na\nb\nc\n' > exp || framework_failure_
compare exp in || fail=1

printf 'a\nb\nc\n' > in || framework_failure_
returns_ 0 $IV -i in 2-3 Y -q || fail=1
printf 'a\nY\nb\nY\nc\n' > exp || framework_failure_
compare exp in || fail=1

printf 'a\nb\nc\n' > in || framework_failure_
returns_ 0 $IV -i in Z -q || fail=1
printf 'a\nb\nc\nZ\n' > exp || framework_failure_
compare exp in || fail=1

printf 'a\nb\nc\n' > in || framework_failure_
returns_ 0 $IV -p in 2 Q -q || fail=1
printf 'a\nQ\nb\nc\n' > exp || framework_failure_
compare exp in || fail=1

printf 'a\nb\nc\n' > in || framework_failure_
returns_ 0 $IV -p in 1-2 R -q || fail=1
printf 'R\nR\nc\n' > exp || framework_failure_
compare exp in || fail=1

printf 'a\nb\nc\n' > in || framework_failure_
returns_ 0 $IV -pi in 2 INS -q || fail=1
printf 'a\nINS\nb\nc\n' > exp || framework_failure_
compare exp in || fail=1

printf 'a\nb\nc\n' > in || framework_failure_
returns_ 0 $IV -i in -1 T -q || fail=1
printf 'a\nb\nT\nc\n' > exp || framework_failure_
compare exp in || fail=1

# New file: create + append.
returns_ 0 $IV -a newf hello -q || fail=1
printf 'hello\n' > exp || framework_failure_
compare exp newf || fail=1

Exit $fail
