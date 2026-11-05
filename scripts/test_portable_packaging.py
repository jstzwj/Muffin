"""Check portable staging preserves personal data and rejects unsafe destinations."""
import subprocess
import tempfile
import os
import sys
from pathlib import Path


def run(build, source, output, succeeds=True):
    script = Path(__file__).resolve().parents[1] / "cmake/BuildPortable.cmake"
    result = subprocess.run(["cmake", f"-DBINARY_DIR={build}", f"-DDIST_DIR={source}",
                             f"-DOUTPUT_DIR={output}", "-P", str(script)], capture_output=True, text=True)
    assert (result.returncode == 0) == succeeds, (
        f"portable staging {source} -> {output}: expected success={succeeds}, exit={result.returncode}\n"
        + result.stdout + result.stderr)


with tempfile.TemporaryDirectory(prefix="Muffin-portable-package-") as directory:
    root = Path(directory)
    build = root / "build"
    build.mkdir()
    source = build / "dist"
    source.mkdir()
    (source / "Muffin.exe").write_bytes(b"fixture app")
    icu = source / "resources" / "icu"
    icu.mkdir(parents=True)
    (icu / "icudt74l.dat").write_bytes(b"fixture ICU data")
    output = build / "portable"
    run(build, source, output)
    assert (output / "Muffin.portable").is_file()
    assert (output / "Muffin.exe").read_bytes() == b"fixture app"
    assert (output / "resources" / "icu" / "icudt74l.dat").read_bytes() == b"fixture ICU data"
    assert not (source / "Muffin.portable").exists()
    run(build, output, output, False)
    assert (output / "Muffin.exe").read_bytes() == b"fixture app"
    profile = output / "Data"
    profile.mkdir()
    sentinel = profile / "precious.ini"
    sentinel.write_text("keep", encoding="utf-8")
    run(build, source, output, False)
    assert sentinel.read_text(encoding="utf-8") == "keep"
    run(build, source, root / "portable", False)
    assert not (root / "portable").exists()
    sentinel.unlink()
    profile.rmdir()
    (source / "Data").mkdir()
    run(build, source, output, False)
    assert (output / "Muffin.exe").read_bytes() == b"fixture app"
    (source / "Data").rmdir()
    run(build, source, output)
    # A parent alias must not bypass input/output overlap detection. CI temp
    # paths use aliases on macOS and Windows even when the staging leaf isn't one.
    alias = root / "build-alias"
    if sys.platform == "win32":
        # Directory junctions work without the elevated symlink privilege.
        env = dict(os.environ, MUFFIN_TEST_LINK=str(alias), MUFFIN_TEST_TARGET=str(build))
        subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command",
                        "$ErrorActionPreference='Stop'; New-Item -ItemType Junction "
                        "-Path $env:MUFFIN_TEST_LINK -Target $env:MUFFIN_TEST_TARGET | Out-Null"],
                       env=env, check=True, capture_output=True, text=True)
    else:
        alias.symlink_to(build, target_is_directory=True)
    try:
        run(alias, alias / "dist", alias / "portable")
        run(alias, alias / "portable", alias / "portable", False)
        run(build, output, alias / "portable", False)
        run(alias, alias / "portable", output, False)
        assert (output / "Muffin.exe").read_bytes() == b"fixture app"
    finally:
        if sys.platform == "win32":
            alias.rmdir()  # Remove the junction itself, never recurse into its target.
        else:
            alias.unlink()
print("Portable packaging: independent marker, protected profile and confined staging passed")
