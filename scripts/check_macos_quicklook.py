#!/usr/bin/env python3
"""Check Quick Look registration, sandboxing, and optionally a deployed app."""

import argparse
from pathlib import Path
import plistlib
import subprocess


MARKDOWN = "net.daringfireball.markdown"


def read_plist(path):
    with path.open("rb") as stream:
        return plistlib.load(stream)


def check(host, extension, entitlements):
    assert host["LSMinimumSystemVersion"] == extension["LSMinimumSystemVersion"]
    assert extension["CFBundleIdentifier"].startswith(host["CFBundleIdentifier"] + ".")
    assert extension["CFBundleVersion"] == host["CFBundleVersion"]
    assert extension["CFBundlePackageType"] == "XPC!"
    point = extension["NSExtension"]
    assert point["NSExtensionPointIdentifier"] == "com.apple.quicklook.preview"
    assert point["NSExtensionPrincipalClass"] == "MuffinPreviewController"
    attributes = point["NSExtensionAttributes"]
    assert attributes["QLSupportedContentTypes"] == [MARKDOWN]
    assert attributes["QLIsDataBasedPreview"] is False
    types = host["UTImportedTypeDeclarations"]
    markdown = next(item for item in types if item["UTTypeIdentifier"] == MARKDOWN)
    assert "public.plain-text" in markdown["UTTypeConformsTo"]
    assert set(markdown["UTTypeTagSpecification"]["public.filename-extension"]) == {"md", "markdown"}
    assert any(MARKDOWN in item["LSItemContentTypes"] for item in host["CFBundleDocumentTypes"])
    assert entitlements["com.apple.security.app-sandbox"] is True
    assert entitlements["com.apple.security.files.user-selected.read-only"] is True
    assert not entitlements.get("com.apple.security.network.client", False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--bundle", type=Path)
    args = parser.parse_args()
    entitlements = read_plist(args.source / "cmake/QuickLook.entitlements")
    if args.bundle:
        contents = args.bundle / "Contents"
        extension = contents / "PlugIns/MuffinQuickLook.appex"
        check(read_plist(contents / "Info.plist"), read_plist(extension / "Contents/Info.plist"), entitlements)
        subprocess.run(["/usr/bin/codesign", "--verify", "--strict", str(extension)], check=True)
        # Without --xml codesign includes its binary entitlement blob header,
        # which is not a plist document and cannot be parsed by plistlib.
        signed = subprocess.check_output(
            ["/usr/bin/codesign", "--display", "--entitlements", "-", "--xml", str(extension)], stderr=subprocess.DEVNULL)
        check(read_plist(contents / "Info.plist"), read_plist(extension / "Contents/Info.plist"), plistlib.loads(signed))
        dependencies = subprocess.check_output(
            ["/usr/bin/otool", "-L", str(extension / "Contents/MacOS/MuffinQuickLook")], text=True)
        assert "Qt" not in dependencies and "MuffinCore" not in dependencies and "MuffinUi" not in dependencies
    else:
        check(read_plist(args.source / "cmake/Info.plist.in"),
              read_plist(args.source / "cmake/QuickLook-Info.plist.in"), entitlements)
    print("Quick Look registration and sandbox checks passed.")


if __name__ == "__main__":
    main()
