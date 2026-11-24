#!/usr/bin/env python3
"""Add green shading (\\cellcolor{ours}) to the best cells of the generated evaluation tables.

tables/c3_vsot.tex -> tables/c3_vsot_hl.tex : per row, the smallest CPU time among otFPSI and our four columns.
tables/c3_real.tex -> tables/c3_real_hl.tex : per data set, the smallest CPU, wall and communication.
Verification: removing every '\\cellcolor{ours}' from the output gives the input byte for byte.
Artifact note: the tables directory is the first argument (default: ./tables); the paper build used
<paper>/tables. Nothing else differs from the version used for the paper.
"""
import re
import sys
from pathlib import Path

T = Path(sys.argv[1] if len(sys.argv) > 1 else "tables").resolve()
MARK = r"\cellcolor{ours}"


def val(cell):
    s = re.sub(r"\$\^\\dagger\$", "", cell).replace("{,}", "").strip()
    try:
        return float(s)
    except ValueError:
        return None


def shade(cells, idxs):
    vals = [(val(cells[i]), i) for i in idxs]
    vals = [(v, i) for v, i in vals if v is not None]
    if not vals:
        return
    best = min(v for v, _ in vals)
    for v, i in vals:
        if v == best:
            cells[i] = " " + MARK + cells[i].lstrip()


def split_row(line):
    body = line.rstrip()
    assert body.endswith("\\\\"), line
    return body[:-2].split("&")


def join_row(cells):
    return "&".join(cells) + "\\\\\n"


def vsot():
    out = []
    for line in (T / "c3_vsot.tex").read_text().splitlines(keepends=True):
        if "&" not in line:
            out.append(line)
            continue
        cells = split_row(line)
        shade(cells, [2, 4, 6, 8, 10])
        out.append(join_row(cells))
    return "".join(out)


def real():
    lines = (T / "c3_real.tex").read_text().splitlines(keepends=True)
    rows = [split_row(l) if "&" in l else None for l in lines]
    groups = {}
    for k, r in enumerate(rows):
        if r is not None:
            groups.setdefault(r[0].strip(), []).append(k)
    for ks in groups.values():
        for col in (2, 3, 4):
            vals = [(val(rows[k][col]), k) for k in ks]
            vals = [(v, k) for v, k in vals if v is not None]
            best = min(v for v, _ in vals)
            for v, k in vals:
                if v == best:
                    rows[k][col] = " " + MARK + rows[k][col].lstrip()
    return "".join(join_row(r) if r is not None else l for r, l in zip(rows, lines))


def check(src, new):
    a = (T / src).read_text()
    b = new.replace(" " + MARK, " ").replace(MARK, "")
    norm = lambda s: re.sub(r"\s+", " ", s)
    if norm(a) != norm(b):
        sys.exit(f"MISMATCH after removing shading: {src}")


for src, fn in (("c3_vsot.tex", vsot), ("c3_real.tex", real)):
    new = fn()
    check(src, new)
    (T / src.replace(".tex", "_hl.tex")).write_text(new)
    print(src, "->", src.replace(".tex", "_hl.tex"), "shaded:", new.count(MARK))
