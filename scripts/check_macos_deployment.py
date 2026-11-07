#!/usr/bin/env python3
"""Verify macOS bundle declarations and every shipped Mach-O deployment target.

Reads load commands directly so this check also runs on Windows/Linux and can
inspect unpacked release artifacts without launching them or using Apple's tools.
"""

import argparse
from pathlib import Path
import plistlib
import struct


THIN = {b"\xcf\xfa\xed\xfe": ("<", 32), b"\xfe\xed\xfa\xcf": (">", 32),
        b"\xce\xfa\xed\xfe": ("<", 28), b"\xfe\xed\xfa\xce": (">", 28)}
FAT = {b"\xca\xfe\xba\xbe": (">", 20), b"\xbe\xba\xfe\xca": ("<", 20),
       b"\xca\xfe\xba\xbf": (">", 32), b"\xbf\xba\xfe\xca": ("<", 32)}
ARM64 = 0x0100000C


def version(text):
    parts = str(text).split(".")
    if not 1 <= len(parts) <= 3 or not all(p.isdecimal() for p in parts):
        raise ValueError(f"Invalid macOS version: {text!r}")
    return tuple(map(int, parts)) + (0,) * (3 - len(parts))


def unpack_version(value):
    return value >> 16, (value >> 8) & 255, value & 255


def read_exact(stream, count):
    data = stream.read(count)
    if len(data) != count:
        raise ValueError("Truncated Mach-O header or load commands")
    return data


def read_slice(stream, offset, size):
    stream.seek(offset)
    magic = read_exact(stream, 4)
    if magic not in THIN:
        raise ValueError("Invalid Mach-O slice")
    order, header_size = THIN[magic]
    if size < header_size:
        raise ValueError("Truncated Mach-O header")
    header = magic + read_exact(stream, header_size - 4)
    cpu, = struct.unpack_from(order + "I", header, 4)
    count, command_size = struct.unpack_from(order + "II", header, 16)
    if command_size > size - header_size or count > command_size // 8:
        raise ValueError("Invalid Mach-O load command bounds")
    end = offset + header_size + command_size
    minimums = []
    for _ in range(count):
        start = stream.tell()
        if end - start < 8:
            raise ValueError("Truncated Mach-O load command")
        command, length = struct.unpack(order + "II", read_exact(stream, 8))
        if length < 8 or length > end - start:
            raise ValueError("Invalid Mach-O load command length")
        if command == 0x32:  # LC_BUILD_VERSION: platform, minOS, SDK, tool count
            if length < 24:
                raise ValueError("Truncated LC_BUILD_VERSION")
            platform, minimum = struct.unpack(order + "II", read_exact(stream, 8))
            if platform != 1:  # PLATFORM_MACOS; Catalyst/iOS cannot run as this app
                raise ValueError(f"Expected macOS platform, got {platform}")
            minimums.append(unpack_version(minimum))
        elif command == 0x24:  # LC_VERSION_MIN_MACOSX: version, SDK
            if length < 16:
                raise ValueError("Truncated LC_VERSION_MIN_MACOSX")
            minimum, = struct.unpack(order + "I", read_exact(stream, 4))
            minimums.append(unpack_version(minimum))
        stream.seek(start + length)
    if stream.tell() != end or len(minimums) != 1:
        raise ValueError("Expected exactly one macOS deployment load command")
    return cpu, minimums[0]


def macho_versions(path):
    """Return (CPU type, minOS) per slice, or [] for a non-Mach-O resource."""
    with path.open("rb") as stream:
        size = path.stat().st_size
        magic = stream.read(4)
        if magic in THIN:
            return [read_slice(stream, 0, size)]
        if magic not in FAT:
            return []
        order, entry_size = FAT[magic]
        count, = struct.unpack(order + "I", read_exact(stream, 4))
        if count == 0 or count > (size - 8) // entry_size:
            raise ValueError("Invalid universal Mach-O architecture table")
        slices = []
        for _ in range(count):
            entry = read_exact(stream, entry_size)
            cpu, = struct.unpack_from(order + "I", entry)
            offset, length = struct.unpack_from(order + ("QQ" if entry_size == 32 else "II"), entry, 8)
            if offset < 8 + count * entry_size or length > size - offset:
                raise ValueError("Invalid universal Mach-O slice bounds")
            slices.append((cpu, offset, length))
        result = []
        for cpu, offset, length in slices:
            actual_cpu, minimum = read_slice(stream, offset, length)
            if actual_cpu != cpu:
                raise ValueError("Universal Mach-O CPU does not match slice header")
            result.append((cpu, minimum))
        return result


def check(root, minimum_os):
    root = root.resolve(strict=True)
    target = version(minimum_os)
    bundle = root if root.suffix == ".app" else root / "Muffin.app"
    host = bundle / "Contents"
    plists = [host / "Info.plist", *host.glob("PlugIns/*.appex/Contents/Info.plist")]
    required = {host / "MacOS/Muffin"}
    for path in plists:
        with path.open("rb") as stream:
            info = plistlib.load(stream)
        declared = info.get("LSMinimumSystemVersion")
        if declared is None or version(declared) != target:
            raise ValueError(f"{path}: LSMinimumSystemVersion must be {minimum_os}, got {declared!r}")
        for architecture, declared in info.get("LSMinimumSystemVersionByArchitecture", {}).items():
            if version(declared) > target:
                raise ValueError(f"{path}: {architecture} requires macOS {declared}")
        required.add(path.parent / "MacOS" / info["CFBundleExecutable"])
    if root != bundle:
        required.add(root / "muffin-mmdc")
    checked = set()
    for path in sorted(root.rglob("*")):
        # Framework aliases refer to the same file. Never leave the artifact.
        if not path.is_file():
            continue
        resolved = path.resolve(strict=True)
        if not resolved.is_relative_to(root):
            raise ValueError(f"{path}: file symlink points outside the distribution")
        if resolved in checked:
            continue
        try:
            slices = macho_versions(path)
        except ValueError as error:
            raise ValueError(f"{path}: {error}") from error
        if not slices:
            continue
        arm = [minimum for cpu, minimum in slices if cpu == ARM64]
        if not arm:
            raise ValueError(f"{path}: missing arm64 executable slice")
        if any(minimum > target for minimum in arm):
            raise ValueError(f"{path}: arm64 minOS {arm[0]} exceeds macOS {minimum_os}")
        checked.add(resolved)
    if not required or not all(path.resolve() in checked for path in required):
        raise ValueError(f"Missing Mach-O executable: {sorted(str(p) for p in required if p.resolve() not in checked)}")
    return len(checked)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", type=Path, help="Muffin.app or the complete dist directory")
    parser.add_argument("--minimum-os", required=True)
    args = parser.parse_args()
    try:
        count = check(args.path, args.minimum_os)
    except (OSError, ValueError, KeyError, plistlib.InvalidFileException) as error:
        parser.exit(1, f"macOS deployment check failed: {error}\n")
    print(f"macOS {args.minimum_os} arm64 deployment verified: {count} Mach-O files")


if __name__ == "__main__":
    main()
