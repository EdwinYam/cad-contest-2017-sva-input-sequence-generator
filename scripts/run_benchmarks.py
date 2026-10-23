#!/usr/bin/env python3
"""Run sva_gen with methods A, B and C on benchmark directories and print a
Markdown table. Times are medians over several runs; "build" is parsing plus
Mealy/Moore construction, "generate" is the method itself.

    python3 scripts/run_benchmarks.py                    # every dir in benchmarks/
    python3 scripts/run_benchmarks.py path/to/bench ...  # selected dirs
"""
import os
import re
import statistics
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINARY = os.path.join(ROOT, "build", "sva_gen")
REPEATS = 5


def run(bench_dir, method, out_path):
    cmd = [BINARY, os.path.join(bench_dir, "fsm.v")]
    sva = os.path.join(bench_dir, "assertions.sva")
    if os.path.exists(sva):
        cmd.append(sva)
    cmd += ["-o", out_path, "--method", method]
    text = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    fsm = re.search(r"(\d+) Mealy states.*-> (\d+) Moore nodes", text)
    res = re.search(r"(\d+)/(\d+) assertions failed, sequence length (\d+), (\d+) pieces generated, "
                    r"(\d+) proven unfailable", text)
    times = re.search(r"parse\+build ([\d.]+) ms, generate ([\d.]+) ms", text)
    return {
        "mealy": int(fsm.group(1)), "moore": int(fsm.group(2)),
        "failed": int(res.group(1)), "rules": int(res.group(2)), "length": int(res.group(3)),
        "pieces": int(res.group(4)), "unfailable": int(res.group(5)),
        "build_ms": float(times.group(1)), "gen_ms": float(times.group(2)),
    }


def main():
    dirs = sys.argv[1:] or sorted(
        os.path.join(ROOT, "benchmarks", d) for d in os.listdir(os.path.join(ROOT, "benchmarks")))
    print("| Benchmark | Mealy / Moore states | Assertions | Method | Failed | Proven unfailable | Sequence length "
          "| Pieces generated | Build (ms) | Generate (ms) |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    with tempfile.TemporaryDirectory() as tmp:
        out_path = os.path.join(tmp, "sequence.txt")
        for d in dirs:
            for method in "ABC":
                runs = [run(d, method, out_path) for _ in range(REPEATS)]
                r = runs[0]
                build = statistics.median(x["build_ms"] for x in runs)
                gen = statistics.median(x["gen_ms"] for x in runs)
                print(f"| {os.path.basename(d.rstrip('/'))} | {r['mealy']} / {r['moore']} | {r['rules']} "
                      f"| {method} | {r['failed']} | {r['unfailable']} | {r['length']} | {r['pieces']} | {build:.2f} | {gen:.3f} |")


if __name__ == "__main__":
    main()
