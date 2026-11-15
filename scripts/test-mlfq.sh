#!/usr/bin/env bash
# Run the MLFQ tests in one xv6 boot: mlfqtest, plus a check of the
# mlfqps output. Exit status 0 only if every check passes.
#
# Usage: scripts/test-mlfq.sh [extra run-xv6.py options]

set -u
cd "$(dirname "$0")/.."

scripts/run-xv6.py --timeout 150 "$@" \
  --expect '^mlfqtest: OK$' \
  --expect '^  PID STATE  LVL  TICKS/LEVEL      NAME$' \
  --expect '^ +1 sleep +0 .* init$' \
  --expect '^ +[0-9]+ run +0 .* mlfqps$' \
  mlfqtest mlfqps
