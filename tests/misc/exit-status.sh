#!/bin/sh
# Exit statuses: 0 success / EPIPE; 1 usage, missing, binary, write.
. "${srcdir=.}/tests/init.sh"

print_ver_
# --version is English; first line is "iv MAJOR.MINOR.PATCH".
"$IV" --version | head -n 1 | grep -E '^iv [0-9]+\.[0-9]+\.[0-9]+$' >/dev/null || fail=1
"$IV" --version | grep -F 'License GPLv3+' >/dev/null || fail=1
"$IV" --version | grep -F 'Copyright (C)' >/dev/null || fail=1

returns_ 1 $IV </dev/null || fail=1
returns_ 1 $IV -s missing-file a b -q || fail=1

printf 'ok\0bad' > bin || framework_failure_
returns_ 1 $IV -s bin ok OK -q || fail=1

printf 'a\n' > in || framework_failure_
returns_ 0 $IV -s in a b -q || fail=1

printf 'same\n' > nm || framework_failure_
returns_ 0 $IV -s nm missing X -q || fail=1

Exit $fail
