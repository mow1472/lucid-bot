"""Paired parameter sweep over the offline simulator.

Every config plays the same seeds against the same opponent fields, so each
is compared with the first ("reference") config seed by seed. Several fields
(separated by ';') test robustness against different kinds of opponents: a
change should help on the pooled average without hurting any single field.

    python -I sim/sweep.py --configs configs.json --seeds 1:40 \
        --opponents "prospector:15;greed:5,wander:5,prospector:5;ah:4,prospector:11"

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

HERE = os.path.dirname(os.path.abspath(__file__))
SIM = os.path.join(HERE, "build", "bin", "lpc-sim.exe" if os.name == "nt" else "lpc-sim")


def run(tag, params, seeds, opponents, turns, out_dir):
    params = dict(params)
    variant = params.pop("variant", "full")
    path = os.path.join(out_dir, f"{tag}.csv")
    cmd = [SIM, "--seeds", seeds, "--variants", variant, "--opponents", opponents,
           "--turns", str(turns), "--params", json.dumps(params), "--csv", path]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    with open(path, newline="") as f:
        return {int(r["seed"]): r for r in csv.DictReader(f)}


def stats(diffs):
    if len(diffs) < 2:
        return (diffs[0] if diffs else 0.0), 0.0
    return statistics.mean(diffs), statistics.stdev(diffs) / math.sqrt(len(diffs))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--configs", required=True)
    ap.add_argument("--seeds", default="1:40")
    ap.add_argument("--opponents", default="prospector:15",
                    help="one or more opponent fields separated by ';'")
    ap.add_argument("--turns", type=int, default=480)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--chunk", type=int, default=4, help="seeds per simulator process")
    ap.add_argument("--out", default=os.path.join(HERE, "build", "sweep"))
    args = ap.parse_args()

    with open(args.configs) as f:
        configs = json.load(f)
    fields = [f for f in args.opponents.split(";") if f]
    os.makedirs(args.out, exist_ok=True)
    lo, hi = (int(x) for x in args.seeds.split(":"))
    results = {(name, field): {} for name in configs for field in fields}
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = []
        for fi, field in enumerate(fields):
            for name, params in configs.items():
                for start in range(lo, hi + 1, args.chunk):
                    end = min(hi, start + args.chunk - 1)
                    tag = f"{name}-f{fi}-{start}"
                    futures.append(((name, field), pool.submit(
                        run, tag, params, f"{start}:{end}", field, args.turns, args.out)))
        for key, future in futures:
            results[key].update(future.result())

    ref_name = next(iter(configs))
    print(f"seeds {args.seeds}; diff = paired gold vs '{ref_name}' (mean +- standard error)")
    for fi, field in enumerate(fields):
        ref = results[(ref_name, field)]
        print(f"\nfield {fi}: {field}")
        print(f"{'config':<16}{'mean':>7}{'sd':>7}{'p10':>6}{'min':>6}{'win%':>6}{'place':>6}"
              f"{'wrk':>5}{'base':>5}{'diff':>8}{'se':>6}")
        for name in configs:
            rows = results[(name, field)]
            seeds = sorted(set(rows) & set(ref))
            gold = [float(rows[s]["gold"]) for s in seeds]
            q = sorted(gold)
            diff, se = stats([g - float(ref[s]["gold"]) for g, s in zip(gold, seeds)])
            avg = lambda key: statistics.mean(float(rows[s][key]) for s in seeds)
            win = 100 * sum(rows[s]["place"] == "1" for s in seeds) / len(seeds)
            print(f"{name:<16}{statistics.mean(gold):7.1f}{statistics.stdev(gold):7.1f}"
                  f"{q[int(0.1 * (len(q) - 1))]:6.0f}{q[0]:6.0f}{win:6.0f}{avg('place'):6.2f}"
                  f"{avg('workers'):5.1f}{avg('bases'):5.1f}{diff:8.1f}{se:6.1f}")
    if len(fields) > 1:
        print(f"\npooled over {len(fields)} fields (relative diff, % of reference gold)")
        print(f"{'config':<16}{'diff%':>8}{'se':>6}{'worst field':>13}")
        for name in configs:
            rel, per_field = [], []
            for field in fields:
                ref, rows = results[(ref_name, field)], results[(name, field)]
                seeds = sorted(set(rows) & set(ref))
                base = statistics.mean(float(ref[s]["gold"]) for s in seeds)
                d = [100 * (float(rows[s]["gold"]) - float(ref[s]["gold"])) / base for s in seeds]
                rel += d
                per_field.append(statistics.mean(d))
            mean, se = stats(rel)
            print(f"{name:<16}{mean:8.1f}{se:6.1f}{min(per_field):13.1f}")


if __name__ == "__main__":
    main()
