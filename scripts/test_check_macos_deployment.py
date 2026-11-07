"""Offline regression tests for the macOS release compatibility gate."""

from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tempfile
import unittest

import check_macos_deployment as deployment


def packed_version(value):
    a, b, c = deployment.version(value)
    return (a << 16) | (b << 8) | c


def thin(minimum="13.0", cpu=deployment.ARM64, legacy=False, order="<", platform=1):
    if legacy:
        command = struct.pack(order + "IIII", 0x24, 16, packed_version(minimum), packed_version("26.0"))
    else:
        command = struct.pack(order + "IIIIII", 0x32, 24, platform, packed_version(minimum), packed_version("26.0"), 0)
    return struct.pack(order + "IIIIIIII", 0xFEEDFACF, cpu, 0, 2, 1, len(command), 0, 0) + command


def universal(order=">", fat64=False):
    intel = thin(cpu=0x01000007, legacy=True)
    arm = thin()
    offset = 72 if fat64 else 48
    table = struct.pack(order + "II", 0xCAFEBABF if fat64 else 0xCAFEBABE, 2)
    for cpu, data in ((0x01000007, intel), (deployment.ARM64, arm)):
        entry = (cpu, 0, offset, len(data), 0)
        table += struct.pack(order + ("IIQQII" if fat64 else "IIIII"), *(entry + (0,) if fat64 else entry))
        offset += len(data)
    return table + intel + arm


class DeploymentTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.bundle = self.root / "Muffin.app"
        self.host = self.bundle / "Contents"
        self.extension = self.host / "PlugIns/MuffinQuickLook.appex/Contents"
        for contents, name in ((self.host, "Muffin"), (self.extension, "MuffinQuickLook")):
            (contents / "MacOS").mkdir(parents=True)
            with (contents / "Info.plist").open("wb") as stream:
                plistlib.dump({"CFBundleExecutable": name, "LSMinimumSystemVersion": "13.0"}, stream)
            (contents / "MacOS" / name).write_bytes(thin())
        (self.host / "Frameworks").mkdir()
        self.library = self.host / "Frameworks/libMuffinCore.dylib"
        self.library.write_bytes(thin())
        (self.root / "muffin-mmdc").write_bytes(thin())

    def test_new_sdk_with_older_deployment_target(self):
        # SDK 26 is fine; the minOS field is the compatibility promise.
        self.assertEqual(deployment.check(self.root, "13.0"), 4)
        self.assertEqual(deployment.check(self.bundle, "13.0"), 3)

    def test_old_release_host_cannot_be_fixed_by_plist_alone(self):
        (self.host / "MacOS/Muffin").write_bytes(thin("26.0"))
        with self.assertRaisesRegex(ValueError, "Muffin.*minOS"):
            deployment.check(self.root, "13.0")

    def test_library_and_plugin_targets_are_checked(self):
        for relative in ("Frameworks/libMuffinCore.dylib", "PlugIns/platforms/libqcocoa.dylib"):
            with self.subTest(relative=relative):
                path = self.host / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(thin("15.0"))
                with self.assertRaisesRegex(ValueError, "minOS"):
                    deployment.check(self.root, "13.0")
                path.write_bytes(thin())

    def test_quicklook_target_is_checked(self):
        (self.extension / "MacOS/MuffinQuickLook").write_bytes(thin("26.0"))
        with self.assertRaisesRegex(ValueError, "MuffinQuickLook.*minOS"):
            deployment.check(self.root, "13.0")

    def test_sibling_cli_target_is_checked(self):
        (self.root / "muffin-mmdc").write_bytes(thin("26.0"))
        with self.assertRaisesRegex(ValueError, "muffin-mmdc.*minOS"):
            deployment.check(self.root, "13.0")

    def test_plists_use_launch_services_key_and_match_target(self):
        for contents in (self.host, self.extension):
            with self.subTest(contents=contents):
                path = contents / "Info.plist"
                original = path.read_bytes()
                for declaration in ({"CFBundleMinimumSystemVersion": "10.15"}, {"LSMinimumSystemVersion": "26.0"}):
                    path.write_bytes(plistlib.dumps({"CFBundleExecutable": "Muffin", **declaration}))
                    with self.assertRaisesRegex(ValueError, "LSMinimumSystemVersion"):
                        deployment.check(self.root, "13.0")
                path.write_bytes(original)

    def test_universal_legacy_and_big_endian(self):
        for data in (universal(), universal("<"), universal(fat64=True), universal("<", True), thin(legacy=True), thin(order=">")):
            with self.subTest(header=data[:4]):
                self.library.write_bytes(data)
                self.assertEqual(deployment.check(self.root, "13.0"), 4)

    def test_wrong_architecture(self):
        self.library.write_bytes(thin(cpu=0x01000007))
        with self.assertRaisesRegex(ValueError, "arm64"):
            deployment.check(self.root, "13.0")

    def test_wrong_platform(self):
        self.library.write_bytes(thin(platform=2))
        with self.assertRaisesRegex(ValueError, "platform"):
            deployment.check(self.root, "13.0")

    def test_missing_executable_and_empty_bundle(self):
        for path in (self.host / "MacOS/Muffin", self.root / "muffin-mmdc"):
            with self.subTest(path=path):
                path.unlink()
                with self.assertRaisesRegex(ValueError, "Missing Mach-O"):
                    deployment.check(self.root, "13.0")
                path.write_bytes(thin())

    def test_cli_failure_is_a_blocking_exit_with_file_diagnostic(self):
        self.library.write_bytes(thin("26.0"))
        result = subprocess.run([sys.executable, deployment.__file__, str(self.root), "--minimum-os", "13.0"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("libMuffinCore.dylib", result.stderr)
        self.assertIn("minOS", result.stderr)

    def test_truncated_or_invalid_commands_fail_closed(self):
        bad_size = bytearray(thin())
        struct.pack_into("<I", bad_size, 36, 0)
        no_minimum = struct.pack("<IIIIIIII", 0xFEEDFACF, deployment.ARM64, 0, 2, 0, 0, 0, 0)
        for data in (thin()[:4], thin()[:-1], universal()[:-1], bad_size, no_minimum):
            with self.subTest(size=len(data)):
                self.library.write_bytes(data)
                with self.assertRaises(ValueError):
                    deployment.check(self.root, "13.0")


if __name__ == "__main__":
    unittest.main()
