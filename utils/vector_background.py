#!/usr/bin/env python3
"""Build a small IB background CDF from sampled native vector scores."""
import argparse
import math
import sys
import textwrap


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", default="-", help="one score per line; '-' reads stdin")
    parser.add_argument("--field", default="", help="field for a [Hybrid:FIELD] override")
    parser.add_argument("--bins", type=int, default=256)
    parser.add_argument("--minimum", type=float, default=0.0)
    parser.add_argument("--maximum", type=float, default=1.0)
    args = parser.parse_args()
    if (args.bins < 2 or args.bins > 4096 or
            not math.isfinite(args.minimum) or not math.isfinite(args.maximum) or
            args.maximum <= args.minimum or not math.isfinite(args.maximum - args.minimum)):
        parser.error("use 2..4096 bins and finite, increasing score bounds")
    if any(c in args.field for c in "[]\r\n"):
        parser.error("field must not contain brackets or newlines")

    histogram = [0] * args.bins
    stream = sys.stdin if args.input == "-" else open(args.input, encoding="utf-8")
    try:
        for line_number, line in enumerate(stream, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                value = float(line)
            except ValueError:
                parser.error(f"line {line_number}: expected one numeric score")
            if not math.isfinite(value) or not args.minimum <= value <= args.maximum:
                parser.error(f"line {line_number}: score outside the configured finite bounds")
            position = (value - args.minimum) / (args.maximum - args.minimum)
            histogram[min(int(position * args.bins), args.bins - 1)] += 1
    finally:
        if stream is not sys.stdin:
            stream.close()

    cumulative = [0]
    for count in histogram:
        cumulative.append(cumulative[-1] + count)
    if not cumulative[-1]:
        parser.error("no background samples")
    section = f"Hybrid:{args.field}" if args.field else "Hybrid"
    print(f"[{section}]")
    print(f"VectorCDFMin={args.minimum:.17g}")
    print(f"VectorCDFMax={args.maximum:.17g}")
    # IB's profile reader supports backslash continuations; keep physical
    # lines short even with large sample counts or 512-bin tables.
    lines = textwrap.wrap(" ".join(map(str, cumulative)), width=220,
                          break_long_words=False, break_on_hyphens=False)
    for index, line in enumerate(lines):
        prefix = "VectorCDF=" if index == 0 else "  "
        suffix = " \\" if index + 1 < len(lines) else ""
        print(prefix + line + suffix)
    print(f"{cumulative[-1]} samples, {args.bins} bins", file=sys.stderr)


if __name__ == "__main__":
    main()
