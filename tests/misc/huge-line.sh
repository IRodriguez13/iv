#!/bin/sh
# A multi-megabyte line is a line, not a crash.
. "${srcdir=.}/tests/init.sh"

# ~2MiB of 'a' plus a token and newline.
awk 'BEGIN{for(i=0;i<2000000;i++)printf "a"; print "TOK"}' > in ||
  framework_failure_
returns_ 0 $IV -s in TOK END -q || fail=1
tail -c 4 in > tail || framework_failure_
printf 'END\n' > exp || framework_failure_
compare exp tail || fail=1

Exit $fail
