#!/usr/bin/env python3
"""Render logo.svg into the raster icon assets the build embeds.

Outputs (written next to this repo's resources/app-icons/):
  muffin-{16,24,32,48,64,128,256,512,1024}.png   crisp per-size PNGs
  muffin.ico                                        multi-resolution Windows icon
  muffin.icns                                        Apple icon set (from the 1024 PNG)

Rendering uses PySide6's QSvgRenderer — the *same* Qt SVG engine the application
links against — so the produced icons match what the app renders at runtime.
Pillow assembles the .ico/.icns containers from the per-size PNGs.

Run it whenever logo.svg changes:

    python scripts/generate_icons.py

Requires: PySide6 (for QSvgRenderer) and Pillow (for ICO/ICNS packaging). Both
are already available in CI (PySide6 is a hard translation dependency); on a dev
machine install them with `pip install PySide6 Pillow`.
"""

from __future__ import annotations

import os
import sys

# Force the offscreen Qt platform so the script also runs on headless CI runners
# (no display server). Must be set before any QtGui import.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PIL import Image  # noqa: E402
from PySide6.QtCore import QByteArray, QSize  # noqa: E402
from PySide6.QtGui import QGuiApplication, QImage, QPainter  # noqa: E402
from PySide6.QtSvg import QSvgRenderer  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)
SOURCE_SVG = os.path.join(REPO_ROOT, "logo.svg")
OUT_DIR = os.path.join(REPO_ROOT, "resources", "app-icons")

PNG_SIZES = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
ICNS_SOURCE_SIZE = 1024


def render_png(renderer: QSvgRenderer, size: int, path: str) -> None:
    """Render the SVG to a square `size`x`size` transparent PNG."""
    image = QImage(QSize(size, size), QImage.Format_ARGB32)
    image.fill(0)  # fully transparent background (rounded card sits on it)
    painter = QPainter(image)
    painter.setRenderHints(
        QPainter.RenderHint.Antialiasing
        | QPainter.RenderHint.SmoothPixmapTransform
        | QPainter.RenderHint.TextAntialiasing
    )
    renderer.render(painter)
    painter.end()
    # QImage.save infers PNG from the extension.
    image.save(path)
    print(f"  wrote {os.path.relpath(path, REPO_ROOT)} ({size}x{size})")


def build_ico(png_by_size: dict[int, str], out_path: str) -> None:
    """Assemble a multi-resolution Windows .ico from the per-size PNGs.

    Pillow's ICO writer drops every requested size larger than the *primary*
    image, so the largest frame must be passed as ``im`` and the rest as
    ``append_images``. Each requested size then resolves to its crisp SVG-rendered
    source instead of an upscaled thumbnail.
    """
    largest = max(ICO_SIZES)
    primary = Image.open(png_by_size[largest]).convert("RGBA")
    extras = [
        Image.open(png_by_size[s]).convert("RGBA")
        for s in ICO_SIZES
        if s != largest
    ]
    primary.save(
        out_path,
        format="ICO",
        sizes=[(s, s) for s in ICO_SIZES],
        append_images=extras,
    )
    print(f"  wrote {os.path.relpath(out_path, REPO_ROOT)} (sizes {ICO_SIZES})")


def build_icns(png_by_size: dict[int, str], out_path: str) -> None:
    """Build an Apple .icns set from the 1024 PNG (Pillow derives the rest)."""
    # Pillow's ICNS writer wants at least a 512px source; 1024 gives us the
    # retina entry too. Supply 512 and 1024 explicitly so both are crisp.
    big = Image.open(png_by_size[ICNS_SOURCE_SIZE]).convert("RGBA")
    half = Image.open(png_by_size[512]).convert("RGBA")
    big.save(out_path, format="ICNS", append_images=[half])
    print(f"  wrote {os.path.relpath(out_path, REPO_ROOT)} (from {ICNS_SOURCE_SIZE}px)")


def main() -> int:
    if not os.path.isfile(SOURCE_SVG):
        print(f"error: {os.path.relpath(SOURCE_SVG, REPO_ROOT)} not found", file=sys.stderr)
        return 1

    os.makedirs(OUT_DIR, exist_ok=True)

    # QSvgRenderer needs a QGuiApplication instance to exist.
    app = QGuiApplication([])

    with open(SOURCE_SVG, "rb") as fh:
        svg_bytes = QByteArray(fh.read())
    renderer = QSvgRenderer(svg_bytes)
    if not renderer.isValid():
        print("error: logo.svg failed to parse", file=sys.stderr)
        return 1

    png_by_size: dict[int, str] = {}
    print("Rendering PNGs:")
    for size in PNG_SIZES:
        path = os.path.join(OUT_DIR, f"muffin-{size}.png")
        render_png(renderer, size, path)
        png_by_size[size] = path

    print("Packaging containers:")
    build_ico(png_by_size, os.path.join(OUT_DIR, "muffin.ico"))
    build_icns(png_by_size, os.path.join(OUT_DIR, "muffin.icns"))

    app.quit()
    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
