#!/usr/bin/env python3
"""Render a scene with each render profile and report GPU/wall time, optionally against golden images.

Exports with a static disk (`disk.speed=0`), so a profile's image is byte-identical from run to run on one machine.

  tools/bench.py                                   # all built-in profiles, 960x540, templates/dev.yaml
  tools/bench.py --profiles Flat Lensed --runs 3 --json out.json
  tools/bench.py --save-golden .gpp/golden         # store the images
  tools/bench.py --compare .gpp/golden --min-psnr 60   # exit 1 when an image drifted
  tools/bench.py --runs 3 --json .gpp/bench-baseline.json    # record a baseline
  tools/bench.py --runs 3 --check .gpp/bench-baseline.json   # exit 1 when median GPU ms regressed > 10%

Baselines are machine and build specific: keep them untracked (.gpp/ is gitignored).
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BINARY = os.path.join(ROOT, "build", "MoleHole")
BUILTIN = ["Flat", "Lensed", "Traced", "Reference"]


def export(profile, scene, width, height, extra, out_png, timeout):
    cmd = [BINARY, "--export-image", out_png, "--width", str(width), "--height", str(height), "--scene", scene,
           "--profile", profile, "--set", "disk.speed=0"]
    for pair in extra:
        cmd += ["--set", pair]
    subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=timeout, check=True)
    with open(os.path.splitext(out_png)[0] + ".render.yaml") as f:
        text = f.read()
    number = lambda key: float(re.search(rf"^{key}: ([0-9.eE+-]+)", text, re.M).group(1))
    return {"gpu_ms": number("GpuMs"), "wall_s": number("WallSeconds")}


def psnr(a, b):
    out = subprocess.run(["ffmpeg", "-i", a, "-i", b, "-lavfi", "psnr", "-f", "null", "-"], capture_output=True,
                         text=True).stderr
    match = re.search(r"average:(inf|[0-9.]+)", out)
    return float("inf") if match and match.group(1) == "inf" else float(match.group(1)) if match else 0.0


def check_regressions(results, baseline, max_regression, min_delta_ms):
    index = {(r["profile"], r["scene"], r["width"], r["height"]): r for r in baseline}
    rows, failed = [], False
    for row in results:
        ref = index.get((row["profile"], row["scene"], row["width"], row["height"]))
        if ref is None:
            rows.append((row["profile"], None, row["gpu_ms"], None, "NO BASELINE"))
            failed = True
            continue
        delta = row["gpu_ms"] - ref["gpu_ms"]
        change = delta / ref["gpu_ms"] if ref["gpu_ms"] > 0 else 0.0
        regressed = change > max_regression and delta > min_delta_ms
        failed |= regressed
        rows.append((row["profile"], ref["gpu_ms"], row["gpu_ms"], change, "REGRESSED" if regressed else "ok"))
    return rows, failed


def print_check(rows):
    print(f"{'profile':<10} {'base ms':>9} {'now ms':>9} {'change':>8}  status")
    for profile, base, now, change, status in rows:
        print(f"{profile:<10} {'-' if base is None else format(base, '9.2f'):>9} {now:9.2f} "
              f"{'-' if change is None else format(change * 100, '+7.1f') + '%':>8}  {status}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--scene", default="templates/dev.yaml")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=540)
    parser.add_argument("--profiles", nargs="+", default=BUILTIN)
    parser.add_argument("--set", action="append", default=[], metavar="KEY=VALUE", help="extra setting, repeatable")
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=600)
    parser.add_argument("--json", help="write the results here")
    parser.add_argument("--save-golden", metavar="DIR")
    parser.add_argument("--compare", metavar="DIR")
    parser.add_argument("--min-psnr", type=float, default=60.0)
    parser.add_argument("--check", metavar="BASELINE", help="fail when median GPU ms regressed against a --json file")
    parser.add_argument("--max-regression", type=float, default=0.10, help="allowed GPU ms increase, fraction")
    parser.add_argument("--min-delta-ms", type=float, default=0.2, help="ignore increases below this many ms")
    args = parser.parse_args()

    results, failed = [], False
    with tempfile.TemporaryDirectory() as tmp:
        for profile in args.profiles:
            runs = []
            image = os.path.join(tmp, f"{profile}.png")
            for _ in range(args.runs):
                runs.append(export(profile, args.scene, args.width, args.height, args.set, image, args.timeout))
            row = {"profile": profile, "width": args.width, "height": args.height, "scene": args.scene,
                   "gpu_ms": statistics.median(r["gpu_ms"] for r in runs),
                   "wall_s": statistics.median(r["wall_s"] for r in runs)}
            golden = os.path.join(args.save_golden or args.compare or "", f"{profile}_{args.width}x{args.height}.png")
            if args.save_golden:
                os.makedirs(args.save_golden, exist_ok=True)
                os.replace(image, golden)
            elif args.compare:
                row["psnr"] = psnr(golden, image) if os.path.exists(golden) else None
                failed |= row["psnr"] is None or row["psnr"] < args.min_psnr
            results.append(row)
            print(f"{profile:<10} {row['gpu_ms']:9.1f} ms GPU  {row['wall_s']:7.2f} s wall"
                  + (f"  PSNR {row['psnr']:.1f} dB" if row.get("psnr") is not None else
                     "  no golden image" if args.compare else ""))
    if args.json:
        with open(args.json, "w") as f:
            json.dump(results, f, indent=2)
    if args.check:
        with open(args.check) as f:
            rows, regressed = check_regressions(results, json.load(f), args.max_regression, args.min_delta_ms)
        print_check(rows)
        failed |= regressed
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
