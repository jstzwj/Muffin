"""Convert one authored webfont to a native SFNT file in the build tree."""

import argparse
from pathlib import Path

from fontTools.ttLib import TTFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Keep the source timestamp and metadata for reproducible native resources.
    with TTFont(args.input, recalcTimestamp=False) as font:
        font.flavor = None
        font.save(args.output)


if __name__ == "__main__":
    main()
