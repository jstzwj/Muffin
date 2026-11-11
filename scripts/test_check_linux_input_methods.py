"""Offline tests of the Linux release dependency gate (also run on Windows)."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import check_linux_input_methods as deployment


class DependencyGateTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.bundle = Path(self.temporary.name).resolve()
        (self.bundle / "platforminputcontexts").mkdir()
        (self.bundle / "platforminputcontexts/libfcitx5platforminputcontextplugin.so").touch()

    def check(self, dependencies, rpath="$ORIGIN"):
        def invoke(command, env):
            if command[0] == "ldd":
                return dependencies
            return rpath
        with patch.object(deployment, "run", side_effect=invoke):
            deployment.check_libraries(self.bundle, {})

    def test_accepts_relocated_qt(self):
        self.check(f"libQt6Core.so.6 => {self.bundle.as_posix()}/libQt6Core.so.6 (0x1234)")

    def test_rejects_private_abi_error(self):
        with self.assertRaisesRegex(RuntimeError, "Unresolved symbols"):
            self.check("undefined symbol: _ZN14QObjectPrivateC2E16QtPrivate_6_10_2")

    def test_rejects_missing_dbus(self):
        with self.assertRaisesRegex(RuntimeError, "Unresolved symbols"):
            self.check("libQt6DBus.so.6 => not found")

    def test_rejects_system_qt(self):
        with self.assertRaisesRegex(RuntimeError, "Qt escaped"):
            self.check("libQt6Core.so.6 => /usr/lib/libQt6Core.so.6 (0x1234)")

    def test_rejects_build_rpath(self):
        with self.assertRaisesRegex(RuntimeError, "Non-relocatable"):
            self.check("", "$ORIGIN:/home/runner/.conan2/p/qt/lib")


if __name__ == "__main__":
    unittest.main()
