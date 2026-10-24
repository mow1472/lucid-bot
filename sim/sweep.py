"""Paired parameter sweep over the offline simulator.

Every config plays the same seeds, so each is compared with the first
("reference") config seed by seed. The paired difference cancels map luck and
needs far fewer seeds than comparing raw means.

    python -I sim/sweep.py --configs configs.json --seeds 1:40 [--opponents prospector:15] [--jobs 8]

configs.json maps a name to AH_PARAMS overrides, e.g.
    {"ref": {}, "explore02": {"exploreWeight": 0.2}}
A config may also set "variant" (baseline, early2, center, bases, full).
"""

import argparse
import concurrent.futures
import csv
import json
import math
import os
import statistics
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "build", "bin", "lpc-sim.exe" if os.name == "nt" else "lpc-sim")


def run(name, params, seeds, opponents, turns, out_dir):
    params = dict(params)
    variant = params.pop("variant", "full")
    path = os.path.join(out_dir, f"{name}.csv")
    cmd = [SIM, "--seeds", seeds, "--variants", variant, "--opponents", opponents,
           "--turns", str(turns), "--params", json.dumps(params), "--csv", path]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    with open(path, newline="") as f:
        return {int(r["seed"]): r for r in csv.DictReader(f)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--configs", required=True)
    ap.add_argument("--seeds", default="1:40")
    ap.add_argument("--opponents", default="prospector:15")
    ap.add_argument("--turns", type=int, default=480)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--out", default=os.path.join(HERE, "build", "sweep"))
    args = ap.parse_args()

    with open(args.configs) as f:
        configs = json.load(f)
    os.makedirs(args.out, exist_ok=True)
    # Split seeds into chunks so all cores stay busy with few configs.
    lo, hi = (int(x) for x in args.seeds.split(":"))
    chunk = max(1, math.ceil((hi - lo + 1) * len(configs) / args.jobs / len(configs)))
    jobs = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        for name, params in configs.items():
            for start in range(lo, hi + 1, chunk):
                end = min(hi, start + chunk - 1)
                jobs.append((name, pool.submit(run, f"{name}-{start}", params,
                                               f"{start}:{end}", args.opponents,
                                               args.turns, args.out)))
        results = {name: {} for name in configs}
        for name, future in jobs:
            results[name].update(future.result())

    ref_name = next(iter(configs))
    ref = results[ref_name]
    header = f"{'config':<14}{'mean':>7}{'sd':>7}{'p10':>7}{'min':>7}{'win%':>6}{'place':>6}{'wrk':>5}{'base':>5}{'diff':>8}{'se':>6}{'t':>6}"
    print(f"seeds {args.seeds}, opponents {args.opponents}; diff = paired vs '{ref_name}'")
    print(header)
    for name in configs:
        rows = results[name]
        seeds = sorted(set(rows) & set(ref))
        gold = [float(rows[s]["gold"]) for s in seeds]
        diffs = [gold[i] - float(ref[s]["gold"]) for i, s in enumerate(seeds)]
        q = sorted(gold)
        p10 = q[int(0.1 * (len(q) - 1))]
        se = statistics.stdev(diffs) / math.sqrt(len(diffs)) if len(diffs) > 1 else 0
        t = statistics.mean(diffs) / se if se > 0 else 0
        mean = lambda key: statistics.mean(float(rows[s][key]) for s in seeds)
        win = 100 * sum(rows[s]["place"] == "1" for s in seeds) / len(seeds)
        print(f"{name:<14}{statistics.mean(gold):7.1f}{statistics.stdev(gold):7.1f}{p10:7.0f}{q[0]:7.0f}"
              f"{win:6.0f}{mean('place'):6.2f}{mean('workers'):5.1f}{mean('bases'):5.1f}"
              f"{statistics.mean(diffs):8.1f}{se:6.1f}{t:6.1f}")


if __name__ == "__main__":
    main()
