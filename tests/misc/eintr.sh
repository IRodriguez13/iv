#!/bin/sh
# stdio/getline must survive EINTR on read (glibc and musl both retry).
. "${srcdir=.}/tests/init.sh"

so=$srcdir/tests/helpers/eintr_read.so
if test ! -f "$so"; then
  cc=${CC:-cc}
  $cc -shared -fPIC -o "$so" "$srcdir/tests/helpers/eintr_read.c" -ldl \
    2>/dev/null || skip_ 'could not build eintr_read.so'
fi

printf 'foo\nbar\n' > in || framework_failure_
LD_PRELOAD=$so
export LD_PRELOAD
returns_ 0 $IV -s in foo FOO -q || fail=1
unset LD_PRELOAD
printf 'FOO\nbar\n' > exp || framework_failure_
compare exp in || fail=1

Exit $fail
