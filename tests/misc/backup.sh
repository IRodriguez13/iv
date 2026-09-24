#!/bin/sh
# GNU Coreutils --backup / -S / VERSION_CONTROL / SIMPLE_BACKUP_SUFFIX.
. "${srcdir=.}/tests/init.sh"

printf 'old\n' > f || framework_failure_

# Default: no backup.
returns_ 0 $IV -s f old new -q || fail=1
printf 'new\n' > exp || framework_failure_
compare exp f || fail=1
test ! -e f~ || fail=1

# -b → existing → simple when no numbered backups exist.
printf 'old\n' > f || framework_failure_
returns_ 0 $IV -s f old new -q -b || fail=1
compare exp f || fail=1
printf 'old\n' > expbak || framework_failure_
compare expbak f~ || fail=1

# --backup=simple and -S .old
printf 'old\n' > g || framework_failure_
returns_ 0 $IV -s g old new -q --backup=simple -S .old || fail=1
compare exp g || fail=1
compare expbak g.old || fail=1

# --suffix alone enables backup (GNU).
printf 'old\n' > h || framework_failure_
returns_ 0 $IV -s h old new -q --suffix=.bak || fail=1
compare exp h || fail=1
compare expbak h.bak || fail=1

# numbered always uses .~N~
printf 'old\n' > n || framework_failure_
returns_ 0 $IV -s n old new -q --backup=numbered || fail=1
compare exp n || fail=1
compare expbak n.~1~ || fail=1
printf 'mid\n' > n || framework_failure_
returns_ 0 $IV -s n mid last -q --backup=numbered || fail=1
printf 'last\n' > explast || framework_failure_
compare explast n || fail=1
compare expbak n.~1~ || fail=1
printf 'mid\n' > expmid || framework_failure_
compare expmid n.~2~ || fail=1

# existing: numbered once .~N~ is present
printf 'x\n' > e || framework_failure_
returns_ 0 $IV -s e x y -q --backup=existing || fail=1
test -f e~ || fail=1
printf 'y\n' > e || framework_failure_
# still simple: no numbered backups exist
returns_ 0 $IV -s e y z -q --backup=existing || fail=1
test -f e~ || fail=1
test ! -e e.~1~ || fail=1
# create a numbered name, then existing must number
printf 'old\n' > e2 || framework_failure_
: > e2.~1~
returns_ 0 $IV -s e2 old new -q --backup=existing || fail=1
compare expbak e2.~2~ || fail=1

# --backup=none even with -b (last --backup= wins if after)
printf 'old\n' > z || framework_failure_
returns_ 0 $IV -s z old new -q -b --backup=none || fail=1
test ! -e z~ || fail=1

# VERSION_CONTROL
printf 'old\n' > v || framework_failure_
returns_ 0 env VERSION_CONTROL=numbered $IV -s v old new -q --backup || fail=1
compare expbak v.~1~ || fail=1

# SIMPLE_BACKUP_SUFFIX
printf 'old\n' > s || framework_failure_
returns_ 0 env SIMPLE_BACKUP_SUFFIX=.sv $IV -s s old new -q -b || fail=1
compare expbak s.sv || fail=1

# --stdout never writes a backup
printf 'old\n' > p || framework_failure_
returns_ 0 $IV -s p old new --stdout -q -b > /dev/null || fail=1
test ! -e p~ || fail=1
compare expbak p || fail=1

# invalid method
returns_ 1 $IV -s p old new --backup=nope -q || fail=1

# no replacement → no overwrite → no backup
printf 'keep\n' > k || framework_failure_
returns_ 0 $IV -s k missing X -q -b || fail=1
test ! -e k~ || fail=1
printf 'keep\n' > expkeep || framework_failure_
compare expkeep k || fail=1

Exit $fail
