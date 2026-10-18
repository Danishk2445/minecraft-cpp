#!/usr/bin/env python3
"""Assertions on a screenshot, for automated GL tests.

Exit status is 0 when every requested check passes and 1 otherwise; each failure is
printed on its own line.

Examples:
  check_image.py shot.png --size 320x240 --no-magenta
  check_image.py shot.png --region 0,0,40,40 --mean-rgb 120,167,255 --tol 10
  check_image.py shot.png --pixel 160,120 --rgb 255,0,0 --tol 5 --min-colors 50
  check_image.py culled.png --same-as unculled.png --max-diff-pixels 50
"""
import argparse
import sys

import numpy as np
from PIL import Image


def parse_ints(text, n):
    vals = [int(v) for v in text.split(",")]
    if len(vals) != n:
        raise argparse.ArgumentTypeError(f"expected {n} comma-separated integers, got {text!r}")
    return vals


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--size", help="expected WxH")
    ap.add_argument("--no-magenta", action="store_true",
                    help="fail if missing-texture magenta (#F800F8 / #FF00FF-ish) covers more than --magenta-max pixels")
    ap.add_argument("--magenta-max", type=int, default=0)
    ap.add_argument("--region", type=lambda s: parse_ints(s, 4), help="x0,y0,x1,y1 for --mean-rgb (exclusive end)")
    ap.add_argument("--mean-rgb", type=lambda s: parse_ints(s, 3))
    ap.add_argument("--pixel", type=lambda s: parse_ints(s, 2), help="x,y for --rgb")
    ap.add_argument("--rgb", type=lambda s: parse_ints(s, 3))
    ap.add_argument("--tol", type=float, default=8.0, help="per-channel tolerance")
    ap.add_argument("--min-colors", type=int, help="minimum distinct colors (detects blank frames)")
    ap.add_argument("--max-colors", type=int)
    ap.add_argument("--luma-vs", help="another image: compare mean luma over --region (ratio this/other)")
    ap.add_argument("--min-ratio", type=float, default=1.0)
    ap.add_argument("--max-ratio", type=float)
    ap.add_argument("--min-luma", type=float)
    ap.add_argument("--max-luma", type=float)
    ap.add_argument("--count-rgb", type=lambda s: parse_ints(s, 3),
                    help="count pixels within --tol of this color inside --region")
    ap.add_argument("--min-count", type=int, default=1)
    ap.add_argument("--max-count", type=int, help="with --count-rgb: at most this many")
    ap.add_argument("--same-as", help="another image of the same view: at most --max-diff-pixels may differ by more than --tol")
    ap.add_argument("--max-diff-pixels", type=int, help="with --same-as (default 0 unless --min-diff-pixels is given)")
    ap.add_argument("--min-diff-pixels", type=int, help="with --same-as: at least this many pixels must differ")
    args = ap.parse_args()

    img = np.asarray(Image.open(args.image).convert("RGB")).astype(np.int32)
    h, w, _ = img.shape
    failures = []

    if args.size:
        ew, eh = (int(v) for v in args.size.lower().split("x"))
        if (w, h) != (ew, eh):
            failures.append(f"size {w}x{h}, expected {ew}x{eh}")

    if args.no_magenta:
        r, g, b = img[..., 0], img[..., 1], img[..., 2]
        magenta = (r > 200) & (g < 40) & (b > 200)
        count = int(magenta.sum())
        if count > args.magenta_max:
            ys, xs = np.nonzero(magenta)
            failures.append(f"{count} magenta pixels (first at {xs[0]},{ys[0]})")

    region = img
    if args.region:
        x0, y0, x1, y1 = args.region
        region = img[y0:y1, x0:x1]
    if args.mean_rgb:
        mean = region.reshape(-1, 3).mean(axis=0)
        diff = np.abs(mean - np.array(args.mean_rgb))
        if (diff > args.tol).any():
            failures.append(f"mean rgb {mean.round(1).tolist()} not within {args.tol} of {args.mean_rgb}")

    if args.pixel and args.rgb:
        x, y = args.pixel
        px = img[y, x]
        if (np.abs(px - np.array(args.rgb)) > args.tol).any():
            failures.append(f"pixel ({x},{y}) = {px.tolist()}, expected {args.rgb} +- {args.tol}")

    if args.min_colors is not None or args.max_colors is not None:
        colors = len(np.unique(img.reshape(-1, 3), axis=0))
        if args.min_colors is not None and colors < args.min_colors:
            failures.append(f"{colors} distinct colors, expected at least {args.min_colors}")
        if args.max_colors is not None and colors > args.max_colors:
            failures.append(f"{colors} distinct colors, expected at most {args.max_colors}")

    if args.min_luma is not None or args.max_luma is not None:
        luma = float((region[..., 0] * 0.299 + region[..., 1] * 0.587 + region[..., 2] * 0.114).mean())
        if args.min_luma is not None and luma < args.min_luma:
            failures.append(f"mean luma {luma:.1f} < {args.min_luma}")
        if args.max_luma is not None and luma > args.max_luma:
            failures.append(f"mean luma {luma:.1f} > {args.max_luma}")

    if args.count_rgb:
        near = (np.abs(region - np.array(args.count_rgb)) <= args.tol).all(axis=2)
        n = int(near.sum())
        if n < args.min_count:
            failures.append(f"{n} pixels near {args.count_rgb}, expected at least {args.min_count}")
        elif args.max_count is not None and n > args.max_count:
            failures.append(f"{n} pixels near {args.count_rgb}, expected at most {args.max_count}")
        else:
            print(f"{n} pixels near {args.count_rgb}")

    if args.luma_vs:
        other = np.asarray(Image.open(args.luma_vs).convert("RGB")).astype(np.int32)
        if args.region:
            x0, y0, x1, y1 = args.region
            other = other[y0:y1, x0:x1]
        luma = lambda a: float((a[..., 0] * 0.299 + a[..., 1] * 0.587 + a[..., 2] * 0.114).mean())
        a, b = luma(region), luma(other)
        ratio = a / max(b, 1e-6)
        if ratio < args.min_ratio:
            failures.append(f"luma ratio {a:.1f}/{b:.1f} = {ratio:.2f} < {args.min_ratio}")
        elif args.max_ratio is not None and ratio > args.max_ratio:
            failures.append(f"luma ratio {a:.1f}/{b:.1f} = {ratio:.2f} > {args.max_ratio}")
        else:
            print(f"luma ratio {a:.1f}/{b:.1f} = {ratio:.2f}")

    if args.same_as:
        other = np.asarray(Image.open(args.same_as).convert("RGB")).astype(np.int32)
        if other.shape != img.shape:
            failures.append(f"size differs from {args.same_as}")
        else:
            differ = int((np.abs(img - other) > args.tol).any(axis=2).sum())
            most = args.max_diff_pixels if args.max_diff_pixels is not None else (None if args.min_diff_pixels else 0)
            if most is not None and differ > most:
                failures.append(f"{differ} pixels differ from {args.same_as}, expected at most {most}")
            elif args.min_diff_pixels is not None and differ < args.min_diff_pixels:
                failures.append(f"{differ} pixels differ from {args.same_as}, expected at least {args.min_diff_pixels}")
            else:
                print(f"{differ} pixels differ from {args.same_as}")

    for f in failures:
        print(f"{args.image}: {f}")
    if not failures:
        print(f"{args.image}: ok ({w}x{h})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
