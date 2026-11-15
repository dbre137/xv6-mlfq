#!/usr/bin/env bash
# Run the proposal's step 6 experiments: build each scheduler
# configuration, boot xv6 and run mlfqbench workloads in it, three
# times each. Raw CONFIG/RESULT lines go to docs/experiments/raw.txt;
# summarise them with scripts/summarise-results.py.
#
# Usage: scripts/run-experiments.sh [e1|e2|e3|e4 ...]   (default: all)
# Takes about 25 minutes for all four. Leaves a default build behind.

set -u
cd "$(dirname "$0")/.."
OUT=docs/experiments/raw.txt
RUNS=3
mkdir -p docs/experiments

# run NAME DEFS COMMAND...: build with MLFQ_DEFS=DEFS, then RUNS boots.
run() {
  local name=$1 defs=$2; shift 2
  echo "== $name ($defs)" >&2
  make clean >/dev/null
  if ! make kernel/kernel fs.img MLFQ_DEFS="$defs" >/dev/null 2>&1; then
    echo "build failed for $name" >&2; exit 1
  fi
  for r in $(seq 1 $RUNS); do
    scripts/run-xv6.py --no-build --timeout 300 "$@" 2>&1 |
      grep -E '^(CONFIG|RESULT)' | sed "s/^/$name run=$r /" | tee -a "$OUT" >&2
  done
}

e1() {  # round-robin vs default MLFQ on CPU, I/O and mixed workloads
  for cfg in "rr:-DSCHED_MLFQ=0" "mlfq:"; do
    run "e1-${cfg%%:*}" "${cfg#*:}" 'mlfqbench cpu 4' 'mlfqbench io 4' 'mlfqbench mixed 4'
  done
}

e2() {  # quantum length (allotment = 2 quanta per level), mixed workload
  for q in "1,2,4:2,4,0" "2,4,8:4,8,0" "4,8,16:8,16,0"; do
    run "e2-q${q%%:*}" "-DMLFQ_QUANTA='{${q%%:*}}' -DMLFQ_ALLOTMENTS='{${q#*:}}'" 'mlfqbench mixed 4'
  done
}

e3() {  # boost interval, under both accounting rules, starvation workload
  for across in 0 1; do
    for b in 10 50 200 0; do
      run "e3-across$across-boost$b" "-DMLFQ_BOOST_INTERVAL=$b -DMLFQ_ACCOUNT_ACROSS_YIELDS=$across" 'mlfqbench starve'
    done
  done
}

e4() {  # naive accounting vs accounting across yields, gaming workload
  for across in 0 1; do
    run "e4-across$across" "-DMLFQ_ACCOUNT_ACROSS_YIELDS=$across" 'mlfqbench game'
  done
}

if [ $# -eq 0 ]; then set -- e1 e2 e3 e4; fi
echo "# $(date '+%Y-%m-%d %H:%M') $(git rev-parse --short HEAD) $*" >> "$OUT"
for e in "$@"; do "$e"; done
make clean >/dev/null && make kernel/kernel fs.img >/dev/null 2>&1
