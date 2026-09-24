#!/bin/sh
# Exit statuses: 0 success / EPIPE; 1 usage, missing, binary, write.
. "${srcdir=.}/tests/init.sh"

print_ver_
# --version is English; first line is GNU-style "iv (Inline Viewer) MAJOR.MINOR.PATCH".
"$IV" --version | head -n 1 | grep -E '^iv \(Inline Viewer\) [0-9]+\.[0-9]+\.[0-9]+$' >/dev/null || fail=1
"$IV" --version | grep -F 'License GPLv3+' >/dev/null || fail=1
"$IV" --version | grep -F 'Copyright (C)' >/dev/null || fail=1

# --help is English and goes to stdout; no-args usage goes to stderr.
returns_ 0 $IV --help >help.out 2>help.err || fail=1
test -s help.err && fail=1
grep -F -- '-s  FILE PAT REPL' help.out >/dev/null || fail=1
grep -F -- '-m PAT' help.out >/dev/null || fail=1
returns_ 1 $IV </dev/null 2>noargs.err || fail=1
grep -F -- 'Usage:' noargs.err >/dev/null || fail=1
returns_ 1 $IV -s missing-file a b -q || fail=1

printf 'ok\0bad' > bin || framework_failure_
returns_ 1 $IV -s bin ok OK -q || fail=1

printf 'a\n' > in || framework_failure_
returns_ 0 $IV -s in a b -q || fail=1

printf 'same\n' > nm || framework_failure_
returns_ 0 $IV -s nm missing X -q || fail=1

Exit $fail
