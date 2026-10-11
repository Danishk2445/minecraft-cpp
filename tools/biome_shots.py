#!/usr/bin/env python3
"""Screenshots of every biome for one seed, as labelled contact sheets.

Runs the biome map tool to find an interior location of each biome, takes a
headless screenshot 12 blocks above the terrain there, and tiles them into
sheets of 4x4. Needs xvfb-run and Pillow.

    tools/biome_shots.py --exe build/release/bin/minecraft --out /tmp/shots [--seed 12345]
"""
import argparse
import os
import re
import subprocess
import sys

from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", default="12345")
    ap.add_argument("--gamedir", default="build/release/test-run")
    ap.add_argument("--size", default="640x360")
    ap.add_argument("--render-distance", default="6")
    ap.add_argument("--only", default="", help="comma-separated biome names")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    listing = subprocess.run(
        [args.exe, "--gamedir", args.gamedir, "--seed", args.seed, "--biome-map", os.path.join(args.out, "map.png"),
         "--map-size", "16384", "--map-step", "16"],
        capture_output=True, text=True, check=True).stdout
    biomes = []
    for line in listing.splitlines():
        m = re.match(r"\s+(\w+)\s+[\d.]+%\s+nearest (-?\d+),(-?\d+)\s+inside (-?\d+),(-?\d+)", line)
        if m:
            biomes.append((m.group(1), int(m.group(4)), int(m.group(5))))
    if args.only:
        wanted = set(args.only.split(","))
        biomes = [b for b in biomes if b[0] in wanted]
    biomes.sort()

    env = dict(os.environ)
    env.pop("WAYLAND_DISPLAY", None)
    env["__GLX_VENDOR_LIBRARY_NAME"] = "mesa"
    shots = []
    for name, x, z in biomes:
        path = os.path.join(args.out, name + ".png")
        cmd = ["xvfb-run", "-a", "-s", "-screen 0 1280x720x24", args.exe, "--x11", "--gamedir", args.gamedir,
               "--size", args.size, "--render-distance", args.render_distance, "--seed", args.seed,
               "--pos", f"{x},~,{z}", "--look", "-30,30", "--time", "6000", "--freeze-time", "--wait-chunks",
               "--screenshot", path, "--exit", "--log-level", "warn"]
        r = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(path):
            print(f"{name}: screenshot failed", file=sys.stderr)
            continue
        shots.append((name, path))
        print(f"{name} at {x},{z}")

    w, h = (int(v) for v in args.size.split("x"))
    for sheet in range(0, len(shots), 16):
        page = shots[sheet:sheet + 16]
        img = Image.new("RGB", (w * 4, h * ((len(page) + 3) // 4)))
        for i, (name, path) in enumerate(page):
            tile = Image.open(path).convert("RGB").resize((w, h))
            d = ImageDraw.Draw(tile)
            d.rectangle((0, 0, 8 * len(name) + 8, 16), fill=(0, 0, 0))
            d.text((4, 2), name, fill=(255, 255, 255))
            img.paste(tile, ((i % 4) * w, (i // 4) * h))
        out = os.path.join(args.out, f"sheet{sheet // 16 + 1}.png")
        img.save(out)
        print("wrote", out)


if __name__ == "__main__":
    main()
