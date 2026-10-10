#!/usr/bin/env python3
"""Benchmarks the four prime-search variants and writes the results as CSV.

Builds every variant in Release mode, then runs each one over a grid of
thread counts (x) and search limits (y), several times per config. Program
output goes to a file, not the terminal, so console speed doesn't dominate
the timings. The timings are the ones each program prints itself.

Usage (from the repository root):
    python3 analysis/run_benchmarks.py           # full grid, takes ~10 minutes
    python3 analysis/run_benchmarks.py --quick   # small grid for a smoke test

Writes, under analysis/results/:
    timings.csv       one row per run: variant, threads, limit, rep, times
    thread_finish.csv per-thread start/finish times from variant 2 runs
    interleaving.csv  order in which variant 1's threads printed primes
    system.json       CPU, compiler and grid used
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "analysis" / "build"
RESULTS_DIR = ROOT / "analysis" / "results"

VARIANTS = {
    1: "variant1_immediate_range",
    2: "variant2_deferred_range",
    3: "variant3_immediate_divisibility",
    4: "variant4_deferred_divisibility",
}

FULL_THREADS = [1, 2, 4, 8, 16, 32, 64, 128]
FULL_LIMITS = [10_000, 100_000, 1_000_000, 10_000_000]
# Divisibility testing syncs every thread once per number, so 10 million
# numbers at 128 threads would take far too long. It stops at 1 million.
MAX_DIVISIBILITY_LIMIT = 1_000_000

QUICK_THREADS = [1, 4, 16]
QUICK_LIMITS = [10_000, 100_000]

TIME_PATTERNS = {
    "total_ms": re.compile(r"Total elapsed: ([\d.]+) ms"),
    "search_ms": re.compile(r"Search time \(until all threads joined\): ([\d.]+) ms"),
    "primes": re.compile(r"Primes found: (\d+)"),
}
TIMESTAMP = r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{6})"
V2_THREAD_LINE = re.compile(
    rf"Thread (\d+) assigned: (\d+) - (\d+) \| started \[{TIMESTAMP}\] finished \[{TIMESTAMP}\]"
)
V1_PRIME_LINE = re.compile(rf"^\[{TIMESTAMP}\] Thread (\d+) found prime (\d+)$")


def build() -> None:
    subprocess.run(
        ["cmake", "-S", str(ROOT), "-B", str(BUILD_DIR), "-DCMAKE_BUILD_TYPE=Release"],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cmake", "--build", str(BUILD_DIR), "-j"], check=True, stdout=subprocess.DEVNULL
    )


def executable(variant: int) -> Path:
    name = f"variant{variant}" + (".exe" if os.name == "nt" else "")
    return BUILD_DIR / VARIANTS[variant] / name


def run_once(variant: int, threads: int, limit: int, workdir: Path) -> str:
    """Runs one config and returns everything the program printed."""
    config = workdir / "config.txt"
    config.write_text(f"threads={threads}\nlimit={limit}\n")
    output = workdir / "out.txt"
    with output.open("w") as out:
        subprocess.run([str(executable(variant)), str(config)], stdout=out, check=True)
    return output.read_text()


def parse_times(text: str) -> dict[str, float | None]:
    parsed: dict[str, float | None] = {}
    for key, pattern in TIME_PATTERNS.items():
        match = pattern.search(text)
        parsed[key] = float(match.group(1)) if match else None
    return parsed


def to_seconds(stamp: str) -> float:
    return datetime.strptime(stamp, "%Y-%m-%d %H:%M:%S.%f").timestamp()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--quick", action="store_true", help="run a small grid only")
    parser.add_argument("--reps", type=int, default=None, help="runs per config")
    args = parser.parse_args()

    thread_counts = QUICK_THREADS if args.quick else FULL_THREADS
    limits = QUICK_LIMITS if args.quick else FULL_LIMITS

    print("Building variants...", flush=True)
    build()
    RESULTS_DIR.mkdir(parents=True, exist_ok=True)

    timing_rows = []
    finish_rows = []
    interleave_rows = []

    with tempfile.TemporaryDirectory() as tmp:
        workdir = Path(tmp)
        for limit in limits:
            # Fast configs get more repetitions to smooth out noise.
            reps = args.reps or (5 if limit <= 100_000 else 3)
            for variant in VARIANTS:
                if variant in (3, 4) and limit > MAX_DIVISIBILITY_LIMIT:
                    continue
                for threads in thread_counts:
                    for rep in range(1, reps + 1):
                        text = run_once(variant, threads, limit, workdir)
                        times = parse_times(text)
                        timing_rows.append(
                            {"variant": variant, "threads": threads, "limit": limit,
                             "rep": rep, **times}
                        )
                        print(f"  v{variant} x={threads:<4} y={limit:<9} rep {rep}: "
                              f"{times['total_ms']:.1f} ms", flush=True)

                        # Keep per-thread detail from the first repetition only.
                        if rep != 1:
                            continue
                        if variant == 2:
                            matches = V2_THREAD_LINE.findall(text)
                            run_start = min(to_seconds(m[3]) for m in matches)
                            for tid, first, last, started, finished in matches:
                                finish_rows.append({
                                    "threads": threads, "limit": limit, "thread": int(tid),
                                    "first": int(first), "last": int(last),
                                    "start_ms": (to_seconds(started) - run_start) * 1000,
                                    "finish_ms": (to_seconds(finished) - run_start) * 1000,
                                })
                        if variant == 1 and limit <= 100_000:
                            for line_no, line in enumerate(
                                    l for l in text.splitlines() if V1_PRIME_LINE.match(l)):
                                stamp, tid, prime = V1_PRIME_LINE.match(line).groups()
                                interleave_rows.append({
                                    "threads": threads, "limit": limit, "line": line_no,
                                    "thread": int(tid), "prime": int(prime),
                                    "time_s": to_seconds(stamp),
                                })

    def write_csv(name: str, rows: list[dict]) -> None:
        with (RESULTS_DIR / name).open("w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

    write_csv("timings.csv", timing_rows)
    write_csv("thread_finish.csv", finish_rows)
    write_csv("interleaving.csv", interleave_rows)

    compiler = subprocess.run(["c++", "--version"], capture_output=True, text=True).stdout
    cpu = platform.processor()
    if sys.platform.startswith("linux"):
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                cpu = line.split(":", 1)[1].strip()
                break
    (RESULTS_DIR / "system.json").write_text(json.dumps({
        "cpu": cpu,
        "logical_cpus": os.cpu_count(),
        "os": platform.platform(),
        "compiler": compiler.splitlines()[0] if compiler else "unknown",
        "thread_counts": thread_counts,
        "limits": limits,
        "max_divisibility_limit": MAX_DIVISIBILITY_LIMIT,
        "date": datetime.now().isoformat(timespec="seconds"),
    }, indent=2))

    print(f"Wrote results to {RESULTS_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
