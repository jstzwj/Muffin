"""Reproduce the small licensed font specimens; native SFNTs are build outputs only."""

import argparse
import hashlib
from pathlib import Path

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont


def specimen(source, name, text, destination, style=None):
    font = TTFont(source, recalcTimestamp=False)
    source_sha = hashlib.sha256(Path(source).read_bytes()).hexdigest()
    if "fvar" in font:
        font = instantiateVariableFont(font, {"wght": 400}, inplace=True)
    options = subset.Options()
    options.name_IDs = [0, 1, 2, 3, 4, 5, 6, 13, 14, 16, 17]
    options.name_legacy = True
    options.name_languages = [0x409]
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(text=text)
    subsetter.subset(font)
    names = {name_id: name for name_id in [1, 3, 4, 6, 16]}
    if style:
        names.update({3: f"{name}-{style}", 4: f"{name} {style}", 6: f"{name}-{style}"})
    for name_id, value in names.items():
        font["name"].setName(value, name_id, 3, 1, 0x409)
        font["name"].setName(value, name_id, 1, 0, 0)
    if "CFF " in font:
        cff = font["CFF "].cff
        cff.fontNames = [names[6]]
        cff.topDictIndex[0].FamilyName = name
        cff.topDictIndex[0].FullName = names[4]
    font.flavor = "woff2"
    font.save(destination)
    print(f"{name}: source SHA256={source_sha}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--han", required=True, type=Path)
    parser.add_argument("--abyss", required=True, type=Path)
    parser.add_argument("--latex", type=Path,
                        help="Directory with Latin Modern regular/bold/italic/bolditalic.otf")
    args = parser.parse_args()
    destination = Path(__file__).resolve().parent.parent / "tests/fixtures/theme/fonts"
    destination.mkdir(exist_ok=True)
    han = "中文混排测试字体回退にほんかん"
    specimen(args.han, "MuffinFixtureHan", han, destination / "han.woff2")
    specimen(args.abyss, "MuffinFixtureAbyss", "".join(chr(i) for i in range(32, 127)) + han,
             destination / "abyss.woff2")
    if args.latex:
        for face, style in [("regular", "Regular"), ("bold", "Bold"),
                            ("italic", "Italic"), ("bolditalic", "BoldItalic")]:
            specimen(args.latex / f"{face}.otf", "MuffinFixtureLatex",
                     "".join(chr(i) for i in range(32, 127)),
                     destination / f"latex-{face}.woff2", style)


if __name__ == "__main__":
    main()
