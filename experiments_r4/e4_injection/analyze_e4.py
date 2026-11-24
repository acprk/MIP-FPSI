"""Analysis of E4 (run_e4.sh): what the vendor learns from the released load after injecting k crafted
files. Decision rules: exact -> H1 iff B >= k+1; rounded -> H1 iff B > 2^ceil(log2 k)... i.e. B >= 2k
for k a power of two; DP -> H1 iff B_noisy >= k+1+s (s = 21 at eps=1, delta_dp=2^-30), the midpoint
rule for the shifted geometric mechanism. Advantage = 2 Pr[correct] - 1.
usage: python3 analyze_e4.py <results_dir> <manifest.json>"""
import json
import re
import sys

D, man = sys.argv[1], json.load(open(sys.argv[2]))
k = man["k"]
s = 21
rows = [l.split() for l in open(f"{D}/summary.txt")]
obs = {}
full = []
for r in rows:
    tag = r[0]
    B = int(re.search(r"B=(\d+)", " ".join(r)).group(1))
    obs[tag] = B
    if tag.startswith("A_"):
        full.append((tag, " ".join(r[1:])))


def adv(mode, rule):
    correct = total = 0
    for t in range(len(man["targets"])):
        for h, truth in (("H0", 0), ("H1", 1)):
            for tag, B in obs.items():
                if tag.startswith(f"B_t{t}_{h}_{mode}"):
                    correct += int(rule(B) == truth)
                    total += 1
    return 2 * correct / total - 1, total


print(f"k={k} injected files per target, {len(man['targets'])} targets, competing load C={man['C']}")
print("exact   advantage %.3f over %d runs" % adv("exact", lambda B: int(B >= k + 1)))
print("rounded advantage %.3f over %d runs" % adv("pow2", lambda B: int(B > k)))
print("DP eps=1 advantage %.3f over %d runs (bound tanh(1/2)=0.462)" % adv("dp", lambda B: int(B >= k + 1 + s)))
print("full end-to-end runs (all must be exact):")
for tag, rest in full:
    print(" ", tag, rest)
