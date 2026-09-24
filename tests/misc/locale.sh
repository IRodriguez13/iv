#!/bin/sh
# Byte-oriented even under a UTF-8 locale. -F is the first byte.
. "${srcdir=.}/tests/init.sh"

LC_ALL=en_US.UTF-8
export LC_ALL

# ñ is C3 B1; the file has C3 but not the pair.
printf 'p\303q\n' > in || framework_failure_
returns_ 0 $IV -s in -F "$(printf '\303\261')" 1 X -q || fail=1
printf 'X\303q\n' > exp || framework_failure_
compare exp in || fail=1

printf 'ok\377\n' > bad || framework_failure_
returns_ 0 $IV -s bad ok OK -q || fail=1
printf 'OK\377\n' > exp2 || framework_failure_
compare exp2 bad || fail=1

Exit $fail
