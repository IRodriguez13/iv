#!/bin/sh
# -e pairs are not capped at 16.
. "${srcdir=.}/tests/init.sh"

printf 'abcdefghijKLMNOPQRST\n' > in || framework_failure_

# 20 substitutions: a..t → A..T on one line.
set --
i=0
for ch in a b c d e f g h i j K L M N O P Q R S T; do
  up=$(printf '%s' "$ch" | tr 'a-z' 'A-Z')
  if test $i -eq 0; then
    set -- -s in "$ch" "$up" -q
  else
    set -- "$@" -e "$ch" "$up"
  fi
  i=$((i + 1))
done

returns_ 0 $IV "$@" || fail=1
printf 'ABCDEFGHIJKLMNOPQRST\n' > exp || framework_failure_
compare exp in || fail=1

Exit $fail
