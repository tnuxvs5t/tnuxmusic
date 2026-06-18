#!/usr/bin/env python3
import os
import sys
from pathlib import Path

from PIL import Image
from PyQt6.QtCore import QSize, Qt
from PyQt6.QtGui import QColor, QImage, QPainter
from PyQt6.QtSvg import QSvgRenderer


ROOT = Path(__file__).resolve().parents[1]
SVG = ROOT / "assets" / "icons" / "tnuxmusic.svg"
ICON_DIR = ROOT / "assets" / "icons"


def render_svg(size: int) -> QImage:
    renderer = QSvgRenderer(str(SVG))
    if not renderer.isValid():
        raise RuntimeError(f"Invalid SVG: {SVG}")

    image = QImage(QSize(size, size), QImage.Format.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)
    painter = QPainter(image)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
    renderer.render(painter)
    painter.end()
    return image


def write_png(size: int) -> Path:
    out = ICON_DIR / f"tnuxmusic-{size}.png"
    image = render_svg(size)
    if not image.save(str(out)):
        raise RuntimeError(f"Could not write {out}")
    return out


def write_ico(png_paths: list[Path]) -> Path:
    out = ICON_DIR / "tnuxmusic.ico"
    images = [Image.open(path).convert("RGBA") for path in png_paths]
    images[-1].save(out, format="ICO", sizes=[(img.width, img.height) for img in images])
    return out


def main() -> int:
    ICON_DIR.mkdir(parents=True, exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256, 512]
    pngs = [write_png(size) for size in sizes]
    write_ico(pngs)
    print("Generated:")
    for path in pngs + [ICON_DIR / "tnuxmusic.ico"]:
        print(f"  {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
    sys.exit(main())
