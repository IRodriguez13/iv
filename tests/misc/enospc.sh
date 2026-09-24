#!/bin/sh
# ENOSPC during a real commit leaves the original intact.
. "${srcdir=.}/tests/init.sh"

unshare --user --mount --map-root-user true >/dev/null 2>&1 ||
  skip_ 'unshare user+mount ns unavailable'

unshare --user --mount --map-root-user /bin/sh -c '
  set -e
  mkdir -p /tmp/ivtiny
  mount -t tmpfs -o size=64k tmpfs /tmp/ivtiny
  printf "a\n" > /tmp/ivtiny/f.txt
  huge=$(awk "BEGIN{for(i=0;i<80000;i++)printf \"b\"}")
  if '"$IV"' -s /tmp/ivtiny/f.txt a "$huge" -q 2>/dev/null; then
    echo "ENOSPC should fail" >&2
    exit 1
  fi
  test "$(cat /tmp/ivtiny/f.txt)" = a
' || fail=1

Exit $fail
