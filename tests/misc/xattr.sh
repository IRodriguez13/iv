#!/bin/sh
# xattrs/ACLs are not preserved. This test documents that promise.
. "${srcdir=.}/tests/init.sh"

command -v setfattr >/dev/null 2>&1 || skip_ 'setfattr not installed'
command -v getfattr >/dev/null 2>&1 || skip_ 'getfattr not installed'

printf 'abc\n' > in || framework_failure_
if ! setfattr -n user.ivtest -v 1 in 2>/dev/null; then
  skip_ 'xattrs not supported on this filesystem'
fi
returns_ 0 $IV -s in abc ABC -q || fail=1
printf 'ABC\n' > exp || framework_failure_
compare exp in || fail=1

# After renameat the new inode has no user.ivtest.
if getfattr -n user.ivtest in >/dev/null 2>&1; then
  echo "xattr survived on the new inode" >&2
  fail=1
fi

Exit $fail
