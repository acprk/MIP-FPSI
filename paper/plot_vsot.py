#!/usr/bin/env python3
"""CPU seconds of both parties against otFPSI (Table tab:vsot), Origin-style.

Data integrity: every plotted number is PARSED from tables/c3_vsot.tex
(the body of Table V, d=128, m=n, one campaign). No number is typed in.
'--' cells (not run) are skipped. A dagger ($^\\dagger$, all-pairs branch
chosen by the cost model) is stripped from the value and drawn as an open
marker. Parsed values are printed and saved to figures/vsot_cpu.csv.

Output: figures/vsot_cpu.pdf (one figure* with three panels, delta = 4, 8, 16).
"""
import csv
import os
import re
import sys

sys.path.insert(0, "<PLOT_STYLE_DIR>")
from plot_style import setup, style_axes, top_legend  # noqa: E402

import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.lines import Line2D  # noqa: E402
from matplotlib.ticker import FixedLocator, LogLocator, NullLocator  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PAPER = os.path.normpath(os.path.join(HERE, "..", ".."))
TABLE = os.path.join(PAPER, "tables", "c3_vsot.tex")
OUT_PDF = os.path.join(PAPER, "figures", "vsot_cpu.pdf")
OUT_PNG = os.path.join(HERE, "vsot_cpu_preview.png")
OUT_CSV = os.path.join(PAPER, "figures", "vsot_cpu.csv")

# Column layout of tables/c3_vsot.tex, read from the header of Table V:
# delta & m=n & otFPSI CPU & B/Q & exact CPU & x & rounded CPU & x
#       & U-free CPU & x & noisy CPU & x
CPU_COLS = {"otFPSI": 2, "exact": 4, "rounded": 6, "U-free": 8}
NCOLS = 12
DELTAS = (4, 8, 16)
LOGN = (8, 10, 12)

# series -> (marker, colour, legend label); ours exact = black squares
SERIES = [
    ("otFPSI", "o", "#D62728", "otFPSI"),
    ("exact", "s", "#000000", "Ours, exact"),
    ("rounded", "^", "#1F4FBF", "Ours, rounded"),
    ("U-free", "D", "#2CA02C", "Ours, U-free"),
]


def parse_cell(cell):
    """Return (value, dagger) or None for '--'."""
    cell = re.sub(r"\\cellcolor\{[^}]*\}", "", cell).strip()  # green shading
    dagger = "dagger" in cell
    cell = re.sub(r"\$\^\\dagger\$", "", cell).strip()
    if cell in ("--", ""):
        return None
    return float(cell.replace("{,}", "").replace(",", "")), dagger


def parse_table():
    rows = []
    with open(TABLE) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("\\midrule"):
                continue
            cells = [c.strip() for c in line.rstrip("\\").strip().split("&")]
            assert len(cells) == NCOLS, (len(cells), line)
            delta = int(cells[0])
            logn = int(re.search(r"2\^\{?(\d+)\}?", cells[1]).group(1))
            for name, idx in CPU_COLS.items():
                p = parse_cell(cells[idx])
                rows.append(dict(delta=delta, logn=logn, series=name,
                                 cpu_s=p[0] if p else "",
                                 all_pairs=int(p[1]) if p else "",
                                 raw=cells[idx]))
    return rows


def main():
    rows = parse_table()
    with open(OUT_CSV, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["delta", "logn", "series", "cpu_s",
                                          "all_pairs", "raw"])
        w.writeheader()
        w.writerows(rows)

    print(f"{'delta':>5}{'logn':>5}  {'series':<9}{'cpu_s':>8}  all_pairs")
    for r in rows:
        print(f"{r['delta']:>5}{r['logn']:>5}  {r['series']:<9}"
              f"{str(r['cpu_s']):>8}  {r['all_pairs']}")

    setup()
    fig, axes = plt.subplots(1, 3, figsize=(7.0, 1.45))
    handles = []
    ys_all = [r["cpu_s"] for r in rows if r["cpu_s"] != ""]
    ylo, yhi = min(ys_all) / 2, max(ys_all) * 2.5
    for ax, delta, tag in zip(axes, DELTAS, "abc"):
        for name, mk, col, lab in SERIES:
            pts = sorted((2 ** r["logn"], r["cpu_s"], r["all_pairs"])
                         for r in rows
                         if r["delta"] == delta and r["series"] == name
                         and r["cpu_s"] != "")
            if not pts:
                continue
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            h, = ax.plot(xs, ys, marker=mk, color=col, lw=1.2, ms=4.8,
                         mfc=col, mec=col, label=lab,
                         zorder=5 if name == "exact" else 3)  # ours on top
            for x, y, dag in pts:
                if dag:  # all-pairs branch: open marker
                    ax.plot([x], [y], marker=mk, ms=4.8, mfc="white",
                            mec=col, mew=1.1, ls="none", zorder=6)
            if ax is axes[0]:
                handles.append(h)
        style_axes(ax, logy=True)
        ax.set_xscale("log", base=2)
        ax.xaxis.set_major_locator(FixedLocator([2 ** k for k in LOGN]))
        ax.xaxis.set_minor_locator(NullLocator())
        ax.set_xticklabels([f"$2^{{{k}}}$" for k in LOGN])
        ax.set_xlim(2 ** (LOGN[0] - 0.5), 2 ** (LOGN[-1] + 0.5))
        ax.set_ylim(ylo, yhi)
        ax.yaxis.set_major_locator(LogLocator(base=10, subs=(1.0,),
                                              numticks=12))
        ax.set_xlabel(r"$m=n$")
        if ax is axes[0]:
            ax.set_ylabel("CPU time (s)")
        ax.text(0.04, 0.95, f"({tag}) δ = {delta}",
                transform=ax.transAxes, ha="left", va="top", fontsize=8.5,
                fontweight="bold")
    handles.append(Line2D([], [], marker="s", color="#777777", ls="none",
                          ms=4.8, mfc="white", mec="#777777",
                          label="open: all-pairs branch"))
    top_legend(fig, handles, ncol=5)
    fig.tight_layout(rect=(0, 0, 1, 0.89), w_pad=1.0)
    fig.savefig(OUT_PDF, bbox_inches="tight", pad_inches=0.02)
    fig.savefig(OUT_PNG, dpi=200, bbox_inches="tight", pad_inches=0.02)
    print("wrote", OUT_PDF)
    print("wrote", OUT_CSV)


if __name__ == "__main__":
    main()
