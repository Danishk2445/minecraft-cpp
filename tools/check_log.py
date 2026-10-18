#!/usr/bin/env python3
"""Checks a number in a game log, for automated tests.

Finds the last line matching PATTERN (a regex with one capture group holding a number)
and checks it against --min and --max. Exit status 0 when it is found and in range.

Example:
  check_log.py run/cave/logs/latest.log "\\((\\d+) occluded\\)" --min 300
"""
import argparse
import re
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("pattern")
    ap.add_argument("--min", type=float)
    ap.add_argument("--max", type=float)
    args = ap.parse_args()
    value = None
    with open(args.log, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(args.pattern, line)
            if m:
                value = float(m.group(1))
    if value is None:
        print(f"{args.log}: no line matches {args.pattern!r}")
        return 1
    if args.min is not None and value < args.min:
        print(f"{args.log}: {value:g} < {args.min:g}")
        return 1
    if args.max is not None and value > args.max:
        print(f"{args.log}: {value:g} > {args.max:g}")
        return 1
    print(f"{args.log}: {value:g} ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
