#!/bin/sh
# Text is literal unless "-". An existing path is not file contents.
. "${srcdir=.}/tests/init.sh"

printf 'a\nb\nc\n' > foo || framework_failure_
printf 'FROMFILE\n' > bar || framework_failure_

returns_ 0 $IV -r foo 2 bar -q || fail=1
printf 'a\nbar\nc\n' > exp || framework_failure_
compare exp foo || fail=1

printf 'a\nb\nc\n' > foo || framework_failure_
printf 'FROMSTDIN' | $IV -r foo 2 - -q || fail=1
printf 'a\nFROMSTDIN\nc\n' > exp || framework_failure_
compare exp foo || fail=1

printf 'keep\n' > dest || framework_failure_
printf 'patched' > snippet || framework_failure_
returns_ 0 $IV -p dest - -q < snippet || fail=1
printf 'keep\npatched\n' > exp || framework_failure_
compare exp dest || fail=1

Exit $fail
