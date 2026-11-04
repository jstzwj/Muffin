"""Account for files and PE sections without installing a binary-analysis tool."""
import argparse
import json
import struct
from pathlib import Path


def pe_sections(path):
    with path.open("rb") as stream:
        header = stream.read(64)
        if len(header) < 64 or header[:2] != b"MZ":
            return []
        stream.seek(struct.unpack_from("<I", header, 60)[0])
        header = stream.read(24)
        if len(header) != 24 or header[:4] != b"PE\0\0":
            return []
        count = struct.unpack_from("<H", header, 6)[0]
        optional = struct.unpack_from("<H", header, 20)[0]
        if count > 96:
            return []
        stream.seek(optional, 1)
        result = []
        for _ in range(count):
            section = stream.read(40)
            if len(section) != 40:
                return []
            result.append({"name": section[:8].rstrip(b"\0").decode("ascii", errors="replace"),
                           "bytes": struct.unpack_from("<I", section, 16)[0]})
        return result


def analyze(root):
    files = []
    for path in root.rglob("*"):
        if path.is_file() and not path.is_symlink():
            files.append({"path": path.relative_to(root).as_posix(), "bytes": path.stat().st_size,
                          "sections": pe_sections(path) if path.suffix.lower() in (".exe", ".dll") else []})
    files.sort(key=lambda item: item["bytes"], reverse=True)
    return {"total_bytes": sum(item["bytes"] for item in files), "files": files}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.directory.is_dir():
        parser.error(f"Not a distribution directory: {args.directory}")
    report = analyze(args.directory)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"Total: {report['total_bytes'] / 1048576:.2f} MiB")
    for item in report["files"][:15]:
        detail = ", ".join(f"{section['name']} {section['bytes'] / 1048576:.1f}"
                           for section in sorted(item["sections"], key=lambda section: section["bytes"], reverse=True)[:3])
        print(f"{item['bytes'] / 1048576:7.2f} MiB  {item['path']}  {detail}")
