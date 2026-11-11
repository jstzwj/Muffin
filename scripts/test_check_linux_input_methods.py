"""Offline tests of the Linux release dependency gate (also run on Windows)."""

from pathlib import Path
import os
import shutil
import subprocess
import sys
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


@unittest.skipUnless(sys.platform.startswith("linux"), "requires ELF build tools")
class DeploymentIntegrationTest(unittest.TestCase):
    def test_preserves_plugin_dependency_graph_across_relocation(self):
        # Exercise the real CMake deploy script with tiny ELF files. The xcb
        # dependency is reachable only through a plugin's original relative
        # RUNPATH, just like Qt's libqxcb -> libQt6XcbQpa -> libQt6Core chain.
        for tool in ("cc", "cmake", "patchelf", "ldd"):
            self.assertIsNotNone(shutil.which(tool), f"Missing deployment test tool: {tool}")
        with tempfile.TemporaryDirectory(prefix="muffin deploy test ") as temporary:
            root = Path(temporary)
            qt = root / "qt"
            libraries = qt / "lib"
            platforms = qt / "plugins/platforms"
            build = root / "build"
            for directory in (libraries, platforms, build):
                directory.mkdir(parents=True)
            env = os.environ.copy()
            env.pop("LD_LIBRARY_PATH", None)
            env.pop("LD_PRELOAD", None)

            def invoke(command):
                result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result

            def library(name, source, destination, *links):
                code = root / f"{name}.c"
                code.write_text(source, encoding="utf-8")
                binary = destination / f"lib{name}.so.6.11.1"
                invoke(["cc", "-shared", "-fPIC", str(code), "-o", str(binary),
                        f"-Wl,-soname,lib{name}.so.6", *links])
                (destination / f"lib{name}.so.6").symlink_to(binary.name)
                (destination / f"lib{name}.so").symlink_to(f"lib{name}.so.6")
                return binary

            core = library("Qt6Core", "int fixture_core(void) { return 7; }", libraries)
            xcb = library("Qt6XcbQpa", "extern int fixture_core(void);\n"
                          "int fixture_xcb(void) { return fixture_core() + 1; }", libraries,
                          f"-L{libraries}", "-lQt6Core", "-Wl,-rpath,$ORIGIN")
            library("Qt6Unused", "int fixture_unused(void) { return 0; }", libraries)
            plugin = platforms / "libqxcb.so"
            plugin_code = root / "plugin.c"
            plugin_code.write_text("extern int fixture_xcb(void);\n"
                                   "int fixture_plugin(void) { return fixture_xcb(); }", encoding="utf-8")
            invoke(["cc", "-shared", "-fPIC", str(plugin_code), "-o", str(plugin),
                    f"-L{libraries}", "-lQt6XcbQpa", "-Wl,-rpath,$ORIGIN/../../lib"])
            app_code = root / "main.c"
            app_code.write_text("extern int fixture_core(void);\n"
                                "int main(void) { return fixture_core() != 7; }", encoding="utf-8")
            app = build / "Muffin"
            invoke(["cc", str(app_code), "-o", str(app), f"-L{libraries}",
                    "-lQt6Core", f"-Wl,-rpath,{libraries}"])
            # Fcitx is built separately, with the selected Qt's build RPATH.
            fcitx = build / "libfcitx5platforminputcontextplugin.so"
            fcitx_code = root / "fcitx.c"
            fcitx_code.write_text("extern int fixture_core(void);\n"
                                  "int fixture_fcitx(void) { return fixture_core(); }", encoding="utf-8")
            invoke(["cc", "-shared", "-fPIC", str(fcitx_code), "-o", str(fcitx),
                    f"-L{libraries}", "-lQt6Core", f"-Wl,-rpath,{libraries}"])
            cli = build / "muffin-mmdc"
            shutil.copy2(app, cli)
            licenses = root / "LICENSES"
            licenses.mkdir()
            (licenses / "LICENSE").write_text("fixture", encoding="utf-8")
            readme = root / "README.md"
            readme.write_text("fixture", encoding="utf-8")
            icu = root / "icu"
            icu.mkdir()
            (icu / "icudt74l.dat").write_bytes(b"fixture")
            originals = {path: path.read_bytes() for path in (app, plugin, core, xcb, fcitx)}
            dist = root / "dist"
            script = Path(__file__).resolve().parents[1] / "cmake/MuffinDeploy.cmake"
            result = invoke(["cmake", f"-DAPP_FILE={app}", f"-DDIST_DIR={dist}",
                             f"-DMMDC_FILE={cli}", f"-DQT_PLUGIN_DIRS={platforms}",
                             f"-DFCITX_PLUGIN={fcitx}", f"-DFCITX_LICENSE_DIR={licenses}",
                             f"-DFCITX_README={readme}", f"-DICU_DATA_DIR={icu}", "-P", str(script)])
            # DIRECTORIES is a last resort after ldconfig. Falling back there
            # would silently pick a system Qt if one shared the same SONAME.
            self.assertNotIn("was found in:", result.stderr)
            self.assertTrue((dist / "libQt6XcbQpa.so.6").is_file())
            self.assertFalse((dist / "libQt6Unused.so.6").exists())
            for path, content in originals.items():
                self.assertEqual(path.read_bytes(), content, f"Deployment modified source: {path}")
            shutil.copy2(dist / "Muffin", dist / "ime-probe")
            relocated = root / "elsewhere/relocated"
            relocated.parent.mkdir()
            dist.rename(relocated)
            # Removing the source package proves no Conan/build RPATH masks
            # a missing library or a broken symlink in the deployed bundle.
            shutil.rmtree(qt)
            shutil.rmtree(build)
            deployment.check_libraries(relocated, env)
            invoke([str(relocated / "Muffin")])
            invoke([sys.executable, "-c", "import ctypes, sys; "
                    "assert ctypes.CDLL(sys.argv[1]).fixture_plugin() == 8",
                    str(relocated / "platforms/libqxcb.so")])


if __name__ == "__main__":
    unittest.main()
