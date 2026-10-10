#!/usr/bin/env python3
"""Draws the slide charts from analysis/results/ into presentation/charts/.

Run from the repository root after analysis/run_benchmarks.py:
    python3 presentation/make_charts.py
"""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
RESULTS = ROOT / "analysis" / "results"
OUT = ROOT / "presentation" / "charts"
OUT.mkdir(parents=True, exist_ok=True)

SURFACE, TEXT, TEXT_2, GRID = "#f7f6f2", "#1b2230", "#4a5160", "#dddad2"
VARIANT = {
    1: dict(name="V1  immediate · straight", color="#2a78d6", marker="o"),
    2: dict(name="V2  deferred · straight", color="#eb6834", marker="s"),
    3: dict(name="V3  immediate · divisibility", color="#1baf7a", marker="^"),
    4: dict(name="V4  deferred · divisibility", color="#eda100", marker="D"),
}
LOGICAL_CPUS = 16

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "figure.dpi": 100, "font.size": 20,
    "text.color": TEXT, "axes.labelcolor": TEXT_2, "xtick.color": TEXT_2, "ytick.color": TEXT_2,
    "axes.edgecolor": GRID, "axes.grid": True, "grid.color": GRID, "grid.linewidth": 1,
    "axes.axisbelow": True, "axes.spines.top": False, "axes.spines.right": False,
    "lines.linewidth": 3, "lines.markersize": 11, "legend.frameon": False,
    "xtick.labelsize": 19, "ytick.labelsize": 19, "axes.labelsize": 20,
})

timings = pd.read_csv(RESULTS / "timings.csv")
median = (timings.groupby(["variant", "limit", "threads"], as_index=False)
                 [["total_ms", "search_ms"]].median())


def threads_axis(ax):
    ax.set_xscale("log", base=2)
    ax.set_xticks([1, 2, 4, 8, 16, 32, 64, 128])
    ax.xaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f"{v:g}"))
    ax.xaxis.set_minor_locator(mticker.NullLocator())
    ax.set_xlabel("Threads (x)")


def cpu_line(ax):
    ax.axvline(LOGICAL_CPUS, color=TEXT_2, linewidth=1.2, zorder=0)
    ax.annotate(f"{LOGICAL_CPUS} logical CPUs", (LOGICAL_CPUS, 1), xycoords=("data", "axes fraction"),
                xytext=(8, -6), textcoords="offset points", va="top", fontsize=17, color=TEXT_2)


def plain_log(axis):
    axis.set_major_locator(mticker.LogLocator(base=10, subs=(1, 2, 5)))
    axis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f"{v:,.10g}"))
    axis.set_minor_formatter(mticker.NullFormatter())


def line(ax, frame, v, y="total_ms", label=True):
    s = VARIANT[v]
    ax.plot(frame["threads"], frame[y], color=s["color"], marker=s["marker"],
            markeredgecolor=SURFACE, markeredgewidth=2, label=s["name"] if label else None)


def save(fig, name):
    fig.savefig(OUT / name, dpi=200, bbox_inches="tight", pad_inches=0.15)
    plt.close(fig)


# 1. Run time vs threads, all four variants, y = 1,000,000
fig, ax = plt.subplots(figsize=(16.6, 6.6))
for v in VARIANT:
    line(ax, median[(median.variant == v) & (median.limit == 1_000_000)], v)
threads_axis(ax); cpu_line(ax)
ax.set_yscale("log"); plain_log(ax.yaxis)
ax.set_ylabel("Run time (ms, log scale)")
ax.legend(loc="upper left", fontsize=18)
save(fig, "time_vs_threads.png")

# 2. Speedup of straight division, y = 10,000,000
fig, ax = plt.subplots(figsize=(10.4, 6.6))
xs = [1, 2, 4, 8, 16, 32, 64, 128]
ax.plot(xs, xs, color=TEXT_2, linewidth=1.5, label="Perfect scaling", zorder=0)
for v in (1, 2):
    frame = median[(median.variant == v) & (median.limit == 10_000_000)].copy()
    frame["speedup"] = frame["total_ms"].iloc[0] / frame["total_ms"]
    line(ax, frame, v, y="speedup")
    last = frame.iloc[-1]
    ax.annotate(f"{last.speedup:.1f}×", (last.threads, last.speedup), xytext=(12, 0),
                textcoords="offset points", va="center", fontsize=20, color=TEXT, fontweight="bold")
threads_axis(ax); cpu_line(ax)
ax.set_yscale("log", base=2)
ax.set_yticks([1, 2, 4, 8, 16, 32, 64, 128])
ax.yaxis.set_major_formatter(mticker.FuncFormatter(lambda v, _: f"{v:g}×"))
ax.yaxis.set_minor_locator(mticker.NullLocator())
ax.set_xlim(0.8, 260)
ax.set_ylabel("Speedup over 1 thread")
ax.legend(loc="upper left", fontsize=18)
save(fig, "speedup.png")

# 3. V2 thread timeline, x = 8, y = 10,000,000
finish = pd.read_csv(RESULTS / "thread_finish.csv")
run = finish[(finish.limit == 10_000_000) & (finish.threads == 8)].sort_values("thread")
join = run["finish_ms"].max()
fig, ax = plt.subplots(figsize=(10.4, 6.6))
ax.barh(run["thread"], join - run["finish_ms"], left=run["finish_ms"], color=GRID, height=0.72,
        label="Finished, waiting for join")
ax.barh(run["thread"], run["finish_ms"] - run["start_ms"], left=run["start_ms"],
        color=VARIANT[2]["color"], height=0.72, label="Searching its block")
ax.axvline(join, color=TEXT, linewidth=2)
ax.annotate(f"join() returns at {join:,.0f} ms", (join, 1), xytext=(-14, 0), textcoords="offset points",
            ha="right", va="center", fontsize=18, color=TEXT)
ax.invert_yaxis()
ax.set_yticks(run["thread"])
ax.set_ylabel("Thread"); ax.set_xlabel("Time since start (ms)")
ax.grid(axis="y", visible=False)
ax.legend(loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=2, fontsize=18)
save(fig, "join_timeline.png")

# 4. Interleaving: which thread printed each of the first 120 lines (V1, x = 4)
inter = pd.read_csv(RESULTS / "interleaving.csv")
run = inter[(inter.threads == 4) & (inter.limit == 100_000)].sort_values("line").head(120)
fig, ax = plt.subplots(figsize=(16.6, 4.4))
ax.scatter(run["line"], run["thread"], s=110, color=VARIANT[1]["color"], edgecolor=SURFACE, linewidth=1.5)
ax.set_yticks([1, 2, 3, 4]); ax.set_ylim(4.6, 0.4)
ax.set_ylabel("Thread"); ax.set_xlabel("Output line")
ax.grid(axis="x", visible=False)
save(fig, "interleaving.png")

print(f"Wrote charts to {OUT}")
