#!/usr/bin/env python3
"""Scripted xv6 runner: boot xv6, type commands into its shell, capture
the output, then quit QEMU. Unlike `make qemu`, it always exits.

Examples:
  scripts/run-xv6.py uoa_cpubound
  scripts/run-xv6.py --expect '^mlfqtest: OK' mlfqtest
  scripts/run-xv6.py --timeout 600 --out usertests.out 'usertests -q'

Each command waits for the shell prompt to come back before the next one
is typed. Exit status: 0 on success, 1 if an --expect pattern is missing,
2 if xv6 did not boot or a command timed out.
"""

import argparse
import os
import re
import signal
import subprocess
import sys
import threading
import time

PROMPT = "$ "


class Xv6:
    def __init__(self, cpus):
        cmd = ["make", "qemu"]
        if cpus is not None:
            cmd.append(f"CPUS={cpus}")
        # Own process group, so QEMU (a child of make) can be killed too.
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, start_new_session=True)
        self.buf = bytearray()
        self.lock = threading.Lock()
        threading.Thread(target=self._reader, daemon=True).start()

    def _reader(self):
        while True:
            chunk = os.read(self.proc.stdout.fileno(), 4096)
            if not chunk:
                return
            with self.lock:
                self.buf.extend(chunk)

    def text(self):
        with self.lock:
            return self.buf.decode("utf-8", "replace")

    def wait_for_prompt(self, start, timeout):
        """Wait until the output after offset `start` ends with the prompt."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            out = self.text()
            if len(out) > start and out.endswith(PROMPT):
                return True
            if self.proc.poll() is not None:
                return False
            time.sleep(0.1)
        return False

    def send(self, line):
        self.proc.stdin.write(line.encode() + b"\n")
        self.proc.stdin.flush()

    def quit(self):
        try:
            self.proc.stdin.write(b"\x01x")  # Ctrl-a x quits QEMU
            self.proc.stdin.flush()
            self.proc.wait(timeout=5)
        except (BrokenPipeError, subprocess.TimeoutExpired):
            pass
        if self.proc.poll() is None:
            os.killpg(self.proc.pid, signal.SIGKILL)
            self.proc.wait()


def build(fresh_fs):
    if fresh_fs and os.path.exists("fs.img"):
        os.remove("fs.img")
    r = subprocess.run(["make", "kernel/kernel", "fs.img"],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        sys.stdout.write(r.stdout.decode("utf-8", "replace"))
        sys.exit("run-xv6: build failed")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument("commands", nargs="+", help="shell commands to type")
    ap.add_argument("--cpus", type=int, help="override CPUS from the Makefile")
    ap.add_argument("--timeout", type=float, default=120,
                    help="seconds allowed per command (default 120)")
    ap.add_argument("--boot-timeout", type=float, default=30)
    ap.add_argument("--expect", action="append", default=[],
                    help="regex that must match a line of output (repeatable)")
    ap.add_argument("--out", help="also save the console output to this file")
    ap.add_argument("--no-build", action="store_true",
                    help="skip make; reuse the existing kernel and fs.img")
    ap.add_argument("--keep-fs", action="store_true",
                    help="keep fs.img from the last run instead of a fresh one")
    args = ap.parse_args()

    # Run from the repository root, wherever the script is called from.
    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    if not args.no_build:
        build(fresh_fs=not args.keep_fs)

    vm = Xv6(args.cpus)
    status = 0
    try:
        if not vm.wait_for_prompt(0, args.boot_timeout):
            print("run-xv6: xv6 did not reach the shell prompt", file=sys.stderr)
            status = 2
        else:
            for c in args.commands:
                start = len(vm.text())
                vm.send(c)
                if not vm.wait_for_prompt(start + len(c), args.timeout):
                    print(f"run-xv6: timed out running {c!r}", file=sys.stderr)
                    status = 2
                    break
    finally:
        vm.quit()

    out = vm.text()
    sys.stdout.write(out if out.endswith("\n") else out + "\n")
    if args.out:
        with open(args.out, "w") as f:
            f.write(out)

    lines = out.splitlines()
    for pat in args.expect:
        if not any(re.search(pat, l) for l in lines):
            print(f"run-xv6: expected pattern not found: {pat}", file=sys.stderr)
            status = status or 1
    sys.exit(status)


if __name__ == "__main__":
    main()
