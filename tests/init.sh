# iv test helpers, named like GNU coreutils tests/init.sh
# (returns_, compare, skip_, framework_failure_, Exit).
# Written for iv; not a copy of gnulib/coreutils.

# Usage from a test script:
#   . "${srcdir=.}/tests/init.sh"
#   returns_ 1 $IV ... || fail=1
#   compare exp out || fail=1
#   Exit $fail

fail=0

if test -z "$IV"; then
  echo "init.sh: IV is not set" >&2
  exit 99
fi

# Work in a private directory; leave the source tree alone.
t_=$(mktemp -d "${TMPDIR:-/tmp}/iv-misc.XXXXXX") || {
  echo "init.sh: mktemp failed" >&2
  exit 99
}
cd "$t_" || exit 99
trap 'cd /; rm -rf "$t_"' EXIT INT HUP TERM

framework_failure_() {
  echo "framework failure${1:+: $1}" >&2
  exit 99
}

skip_() {
  echo "SKIP: $*" >&2
  exit 77
}

# returns_ EXPECTED_STATUS COMMAND...
# Fails if the command exits with any other status (including signals).
returns_() {
  exp_="$1"
  shift
  "$@"
  got_=$?
  test "$got_" -eq "$exp_"
}

# compare EXPECTED ACTUAL
compare() {
  if test "x$1" = x/dev/null; then
    test ! -s "$2" && return 0
    echo "compare: $2 not empty" >&2
    cat "$2" >&2
    return 1
  fi
  if test "x$2" = x/dev/null; then
    test ! -s "$1" && return 0
    echo "compare: $1 not empty" >&2
    cat "$1" >&2
    return 1
  fi
  cmp -s "$1" "$2" && return 0
  echo "compare: $1 vs $2" >&2
  diff -u "$1" "$2" >&2 || true
  return 1
}

print_ver_() {
  "$IV" -V | head -n 1
}

Exit() {
  exit "${1:-0}"
}
