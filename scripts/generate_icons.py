#!/usr/bin/env python3
import sys
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
ICON_DIR = ROOT / "assets" / "icons"
SOURCE = ICON_DIR / "tnuxmusic.png"


def write_png(source: Image.Image, size: int) -> Path:
    out = ICON_DIR / f"tnuxmusic-{size}.png"
    image = source.resize((size, size), Image.Resampling.LANCZOS)
    image.save(out)
    return out


def write_ico(png_paths: list[Path]) -> Path:
    out = ICON_DIR / "tnuxmusic.ico"
    images = [Image.open(path).convert("RGBA") for path in png_paths]
    images[-1].save(out, format="ICO", sizes=[(img.width, img.height) for img in images])
    return out


def main() -> int:
    if not SOURCE.exists():
        raise RuntimeError(f"Missing source icon: {SOURCE}")

    ICON_DIR.mkdir(parents=True, exist_ok=True)
    sizes = [16, 24, 32, 48, 64, 128, 256, 512]
    source = Image.open(SOURCE).convert("RGBA")
    pngs = [write_png(source, size) for size in sizes]
    write_ico(pngs)
    print("Generated:")
    print(f"  {SOURCE.relative_to(ROOT)}")
    for path in pngs + [ICON_DIR / "tnuxmusic.ico"]:
        print(f"  {path.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
