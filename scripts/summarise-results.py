#!/usr/bin/env python3
"""Summarise docs/experiments/raw.txt (from scripts/run-experiments.sh)
as Markdown tables: the mean of each measurement over the runs, with
the range (min-max) in brackets when the runs differ.

For each configuration, only its most recent set of runs is used.

Usage: scripts/summarise-results.py [raw.txt]
"""

import re
import sys
from collections import OrderedDict

path = sys.argv[1] if len(sys.argv) > 1 else "docs/experiments/raw.txt"

# name -> {"config": str, "rows": {key: {field: [values]}}}, latest block only
data = OrderedDict()
block = 0
seen_in_block = {}
for line in open(path):
    line = line.rstrip("\n")
    if line.startswith("#"):
        block += 1
        continue
    m = re.match(r"(\S+) run=(\d+) (CONFIG|RESULT|ERROR) (.*)", line)
    if not m:
        continue
    name, _run, kind, rest = m.groups()
    if seen_in_block.get(name) != block:   # newer block: start again
        seen_in_block[name] = block
        data[name] = {"config": "", "rows": OrderedDict()}
        data.move_to_end(name)
    if kind == "CONFIG":
        data[name]["config"] = rest
        continue
    if kind == "ERROR":
        data[name].setdefault("errors", []).append(rest)
        continue
    # "RESULT kind=cpu n=4 ..." or "RESULT starve window=300 ...":
    # the workload is the kind= value or the leading bare word.
    words = rest.split()
    key = words[0] if "=" not in words[0] else None
    fields = OrderedDict(w.split("=", 1) for w in words if "=" in w)
    if "kind" in fields:
        kind_ = fields.pop("kind")
        key = fields.pop("workload", None)
        key = f"{key}: {kind_}" if key else kind_
    row = data[name]["rows"].setdefault(key, OrderedDict())
    for k, v in fields.items():
        try:
            row.setdefault(k, []).append(float(v))
        except ValueError:
            pass


def fmt(vals):
    mean = sum(vals) / len(vals)
    s = f"{mean:.1f}"
    if min(vals) != max(vals):
        s += f" ({min(vals):g}-{max(vals):g})"
    return s


def table(names, title):
    names = [n for n in names if n in data]
    if not names:
        return
    print(f"### {title}\n")
    cols = []
    for n in names:
        for key, row in data[n]["rows"].items():
            for f in row:
                if f not in cols:
                    cols.append(f)
    print("| Config | Workload | Runs | " + " | ".join(cols) + " |")
    print("|---|---|---:|" + "---:|" * len(cols))
    for n in names:
        for key, row in data[n]["rows"].items():
            runs = max(len(v) for v in row.values())
            cells = [fmt(row[c]) if c in row else "" for c in cols]
            print(f"| {n} | {key} | {runs} | " + " | ".join(cells) + " |")
    print()
    for n in names:
        errs = data[n].get("errors", [])
        note = f" ({len(errs)} run(s) failed: {errs[0]})" if errs else ""
        print(f"- `{n}`: {data[n]['config']}{note}")
    print()


for prefix, title in [("e1", "E1: round-robin vs MLFQ"),
                      ("e2", "E2: quantum length (mixed workload)"),
                      ("e3", "E3: boost interval (starvation workload)"),
                      ("e4", "E4: time accounting (gaming workload)")]:
    table([n for n in data if n.startswith(prefix + "-")], title)
