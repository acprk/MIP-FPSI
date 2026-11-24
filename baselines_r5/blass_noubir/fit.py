#!/usr/bin/env python3
"""Aggregate logs/*/summary.json and fit/extrapolate CPU time.

Models (n = n_S = n_R, L = ELL as passed to the binary, t = threshold):
  M1 (per (variant,L,t) series):  CPU = a * n^2           (least squares through origin)
  M2 (global, per variant):       CPU = a * L*t*n^2 + b * L*t*n + c
  D  (receiver decryption only):  dec = a_d * L*t*n^2     (pure quadratic part)
Writes fit_results.json and prints a table.
"""
import glob
import json
import os
import statistics as st

import numpy as np

ROOT = os.path.dirname(os.path.abspath(__file__))
rows = []
for p in sorted(glob.glob(os.path.join(ROOT, "logs", "*", "summary.json"))):
    s = json.load(open(p))
    tag = s["tag"]
    if tag.startswith("smoke"):
        continue
    var = tag.split("_")[0]
    if s["exit_receiver"] != 0 or s["exit_sender"] != 0 or s["cpu_total_s"] is None:
        print("skip failed", tag)
        continue
    rows.append(dict(var=var, k=s["logN_S"], L=s["ELL"], t=s["t"], tag=tag,
                     cpu=s["cpu_total_s"], wall=s["wall_s"],
                     cpu_s=s["sender"]["user_s"] + s["sender"]["sys_s"],
                     cpu_r=s["receiver"]["user_s"] + s["receiver"]["sys_s"],
                     dec=s["receiver_report"]["decrypt_ms"] / 1000.0,
                     kder=s["sender_report"]["kder_ms"] / 1000.0,
                     enc=s["sender_report"]["encrypt_ms"] / 1000.0,
                     comm=s["receiver_report"]["comm_MB"],
                     rss_r=s["receiver"]["max_rss_kb"], rss_s=s["sender"]["max_rss_kb"],
                     check=s["check_successes"],
                     thr=(s["nr_throttled_post"] - s["nr_throttled_pre"])))

groups = {}
for r in rows:
    groups.setdefault((r["var"], r["L"], r["t"], r["k"]), []).append(r)

table = []
print(f"{'var':4} {'L':>4} {'t':>2} {'k':>2} {'reps':>4} {'wall_med':>9} {'cpu_med':>9} {'cpu_min':>9} {'cpu_max':>9} "
      f"{'dec_med':>8} {'kder_med':>8} {'comm_MB':>8} {'rssR_MB':>8} check thr")
for key in sorted(groups):
    g = groups[key]
    var, L, t, k = key
    e = dict(var=var, L=L, t=t, k=k, n=2 ** k, reps=len(g),
             wall_med=st.median(x["wall"] for x in g),
             cpu_med=st.median(x["cpu"] for x in g),
             cpu_min=min(x["cpu"] for x in g), cpu_max=max(x["cpu"] for x in g),
             cpu_s_med=st.median(x["cpu_s"] for x in g),
             cpu_r_med=st.median(x["cpu_r"] for x in g),
             dec_med=st.median(x["dec"] for x in g),
             kder_med=st.median(x["kder"] for x in g),
             enc_med=st.median(x["enc"] for x in g),
             comm=g[0]["comm"], rss_r_MB=max(x["rss_r"] for x in g) / 1024,
             rss_s_MB=max(x["rss_s"] for x in g) / 1024,
             checks=[x["check"] for x in g], thr=[x["thr"] for x in g])
    table.append(e)
    print(f"{var:4} {L:4d} {t:2d} {k:2d} {len(g):4d} {e['wall_med']:9.1f} {e['cpu_med']:9.1f} {e['cpu_min']:9.1f} "
          f"{e['cpu_max']:9.1f} {e['dec_med']:8.1f} {e['kder_med']:8.1f} {e['comm']:8.2f} {e['rss_r_MB']:8.0f} "
          f"{e['checks']} {e['thr']}")

fits = {}
# M1: per series
series = {}
for e in table:
    series.setdefault((e["var"], e["L"], e["t"]), []).append(e)
for key, es in series.items():
    n2 = np.array([e["n"] ** 2 for e in es], float)
    y = np.array([e["cpu_med"] for e in es], float)
    a = float((n2 @ y) / (n2 @ n2))
    fits[f"M1_{key[0]}_L{key[1]}_t{key[2]}"] = dict(a=a, points=[(e["n"], e["cpu_med"]) for e in es],
                                                   resid_rel=[float((a * e["n"] ** 2 - e["cpu_med"]) / e["cpu_med"]) for e in es])

# M2 and D: global per variant
for var in sorted({e["var"] for e in table}):
    es = [e for e in table if e["var"] == var]
    X = np.array([[e["L"] * e["t"] * e["n"] ** 2, e["L"] * e["t"] * e["n"], 1.0] for e in es])
    y = np.array([e["cpu_med"] for e in es])
    if len(es) >= 3:
        coef, *_ = np.linalg.lstsq(X, y, rcond=None)
        fits[f"M2_{var}"] = dict(a=float(coef[0]), b=float(coef[1]), c=float(coef[2]),
                                 resid_rel=[float((X[i] @ coef - y[i]) / y[i]) for i in range(len(es))],
                                 points=[(e["L"], e["t"], e["n"], e["cpu_med"]) for e in es])
    xd = np.array([e["L"] * e["t"] * e["n"] ** 2 for e in es], float)
    yd = np.array([e["dec_med"] for e in es], float)
    ad = float((xd @ yd) / (xd @ xd))
    fits[f"D_{var}"] = dict(a_d=ad, per_decrypt_ms_at_L129=ad * 129 * 1000,
                            resid_rel=[float((ad * xd[i] - yd[i]) / yd[i]) for i in range(len(es))])

proj = {}
for name, f in fits.items():
    for n in (2 ** 10, 2 ** 12):
        if name.startswith("M1_") and "_L129_t5" in name:
            proj[f"{name}_n{n}"] = f["a"] * n * n
        if name.startswith("M2_"):
            L, t = 129, 5
            proj[f"{name}_n{n}"] = f["a"] * L * t * n * n + f["b"] * L * t * n + f["c"]
        if name.startswith("D_"):
            proj[f"{name}_decrypt_only_n{n}"] = f["a_d"] * 129 * 5 * n * n

print("\nfits:")
print(json.dumps(fits, indent=1))
print("\nprojections at L=129 (128-bit vectors + 1 threshold slot), t=5 [CPU-s]:")
for k_, v in proj.items():
    print(f"  {k_}: {v:,.0f} CPU-s = {v / 3600:,.1f} CPU-h = {v / 86400:,.2f} CPU-days;"
          f" vs ours 3.2 CPU-s: {v / 3.2:,.0f}x; vs otFPSI 368 CPU-s: {v / 368:,.0f}x")
json.dump(dict(table=table, fits=fits, projections=proj), open(os.path.join(ROOT, "fit_results.json"), "w"), indent=1)
