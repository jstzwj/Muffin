#!/usr/bin/env python3
"""Compile Explorer's native string tables from the application's Qt catalogs."""

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET


LANGUAGES = {
    "en": 0x0409, "de": 0x0407, "es": 0x0C0A, "fr": 0x040C,
    "it": 0x0410, "ja": 0x0411, "ko": 0x0412, "nl": 0x0413,
    "pl": 0x0415, "pt_BR": 0x0416, "ru": 0x0419, "tr": 0x041F,
    "vi": 0x042A, "zh_CN": 0x0804, "zh_TW": 0x0404,
}
STRINGS = {
    "MUFFIN_IDS_OPEN_WITH": "Open with Muffin",
    "MUFFIN_IDS_DOCUMENT_TYPE": "Muffin Markdown Document",
    "MUFFIN_IDS_OPEN": "Open",
}


def translations(path):
    result = {}
    for context in ET.parse(path).getroot().findall("context"):
        if context.findtext("name") != "muffin::WindowsIntegration":
            continue
        for message in context.findall("message"):
            translation = message.find("translation")
            if translation is None or translation.get("type") in ("unfinished", "vanished", "obsolete"):
                continue
            result[message.findtext("source")] = translation.text or ""
    for source in STRINGS.values():
        if not result.get(source, "").strip():
            raise ValueError(f"{path}: missing completed shell translation: {source}")
    return result


def rc_literal(text):
    # UTF-16 escapes avoid source-code-page assumptions in rc.exe/llvm-rc,
    # including translated quotation marks, backslashes and surrogate pairs.
    data = text.encode("utf-16-le")
    return 'L"' + "".join(f"\\x{int.from_bytes(data[i:i+2], 'little'):04x}" for i in range(0, len(data), 2)) + '"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    header = (root / "src/app/WindowsShellResources.h").as_posix()
    lines = ["// Generated from muffin_*.ts. Do not edit.", f'#include "{header}"']
    emitted = set()
    for code, language in LANGUAGES.items():
        labels = {source: source for source in STRINGS.values()} if code == "en" else translations(root / f"translations/muffin_{code}.ts")
        # Neutral primary-language resources cover regional OS variants. Chinese
        # keeps distinct Simplified/Traditional tables; English is the fallback.
        variants = [language]
        if code not in ("zh_CN", "zh_TW"):
            variants.append(language & 0x3FF)
        if code == "en":
            variants.append(0)
        for variant in variants:
            if variant in emitted:
                continue
            emitted.add(variant)
            lines.extend([f"LANGUAGE {variant & 0x3FF}, {variant >> 10}", "STRINGTABLE", "BEGIN"])
            lines.extend(f"  {identifier} {rc_literal(labels[source])}" for identifier, source in STRINGS.items())
            lines.append("END")
    output = "\n".join(lines) + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != output:
        args.output.write_text(output, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
