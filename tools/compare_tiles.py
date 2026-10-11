#!/usr/bin/env python3
"""Compare rendered model-sheet tiles against golden PNGs.

usage: compare_tiles.py RENDERED_DIR GOLDEN_DIR [--mean-tol 2.0] [--outlier-frac 0.02]

For each golden tile, the rendered tile with the same name must exist and match.
"Match" means the mean absolute difference over RGBA is at most --mean-tol, and at
most --outlier-frac of pixels differ by more than 40 in any channel. The tolerance
absorbs rasterization differences between Mesa versions.
"""
import argparse
import pathlib
import sys

import numpy as np
from PIL import Image


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rendered")
    ap.add_argument("golden")
    ap.add_argument("--mean-tol", type=float, default=2.0)
    ap.add_argument("--outlier-frac", type=float, default=0.02)
    args = ap.parse_args()

    golden = sorted(pathlib.Path(args.golden).glob("*.png"))
    if not golden:
        print("no golden tiles found")
        return 1
    bad = 0
    for g in golden:
        r = pathlib.Path(args.rendered) / g.name
        if not r.exists():
            print(f"{g.name}: not rendered")
            bad += 1
            continue
        a = np.asarray(Image.open(g).convert("RGBA")).astype(np.int32)
        b = np.asarray(Image.open(r).convert("RGBA")).astype(np.int32)
        if a.shape != b.shape:
            print(f"{g.name}: size {b.shape} != golden {a.shape}")
            bad += 1
            continue
        diff = np.abs(a - b)
        mean = float(diff.mean())
        outliers = float((diff.max(axis=2) > 40).mean())
        if mean > args.mean_tol or outliers > args.outlier_frac:
            print(f"{g.name}: mean diff {mean:.2f}, {outliers * 100:.1f}% outlier pixels")
            bad += 1
    print(f"{len(golden) - bad}/{len(golden)} tiles match")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
