#!/usr/bin/env bash
# Run every usertests quick test by name in one xv6 boot and report
# which pass and which fail.
#
# usertests -q stops at the first failure. Running the tests one by one
# means a failure does not hide the results of the tests after it.
#
# Usage: scripts/run-usertests.sh [extra run-xv6.py options]
# Exit status: 0 if every test passes (other than any in KNOWN_FAILS),
# 1 otherwise.

set -u
cd "$(dirname "$0")/.."

# Tests expected to fail (none since the Makefile puts README on the
# disk image; copyout and lazy_copy used to fail without it).
KNOWN_FAILS=""

tests=$(awk '/quicktests\[\] = \{/,/^};/' user/usertests.c |
        grep -oE '\{[a-z0-9_]+, *"[a-z0-9_]+"\}' |
        sed -E 's/.*"([a-z0-9_]+)".*/\1/')

cmds=()
for t in $tests; do
  cmds+=("usertests $t")
done

out=$(mktemp)
scripts/run-xv6.py --timeout 300 "$@" "${cmds[@]}" > "$out" 2>&1
runner=$?

# Each test's own output ends with ALL TESTS PASSED or SOME TESTS FAILED.
failed=$(awk '/^\$ usertests /{name=$3} /SOME TESTS FAILED/{print name}' "$out")
passed=$(grep -c 'ALL TESTS PASSED' "$out")
total=$(echo "$tests" | wc -w | tr -d ' ')

echo "usertests: $passed/$total passed"
unexpected=""
for f in $failed; do
  case " $KNOWN_FAILS " in
    *" $f "*) echo "  $f: failed (expected failure)";;
    *) echo "  $f: FAILED"; unexpected="$unexpected $f";;
  esac
done

status=0
if [ $runner -ne 0 ]; then
  echo "run-xv6.py exited with $runner (boot failure or timeout); output kept in $out"
  status=1
elif [ -n "$unexpected" ]; then
  echo "unexpected failures:$unexpected; output kept in $out"
  status=1
elif [ $((passed + $(echo "$failed" | wc -w))) -ne "$total" ]; then
  echo "some tests did not report a result; output kept in $out"
  status=1
else
  rm -f "$out"
fi
exit $status
