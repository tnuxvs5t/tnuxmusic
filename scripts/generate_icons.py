#!/usr/bin/env python3
import sys
from pathlib import Path

from PIL import Image
from PIL import ImageMath


ROOT = Path(__file__).resolve().parents[1]
ICON_DIR = ROOT / "assets" / "icons"
SOURCE = ICON_DIR / "tnuxmusic.png"


def premultiply_alpha(image: Image.Image) -> Image.Image:
    r, g, b, a = image.split()
    return Image.merge("RGBA", (
        ImageMath.eval("convert(r * a / 255, 'L')", r=r, a=a),
        ImageMath.eval("convert(g * a / 255, 'L')", g=g, a=a),
        ImageMath.eval("convert(b * a / 255, 'L')", b=b, a=a),
        a,
    ))


def unpremultiply_alpha(image: Image.Image) -> Image.Image:
    out = Image.new("RGBA", image.size)
    src = image.load()
    dst = out.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = src[x, y]
            if a <= 1:
                dst[x, y] = (0, 0, 0, 0)
            else:
                dst[x, y] = (
                    min(255, round(r * 255 / a)),
                    min(255, round(g * 255 / a)),
                    min(255, round(b * 255 / a)),
                    a,
                )
    return out


def resize_rgba_clean(source: Image.Image, size: int) -> Image.Image:
    inner = max(1, round(size * 0.9))
    premultiplied = premultiply_alpha(source)
    resized = premultiplied.resize((inner, inner), Image.Resampling.LANCZOS)
    image = unpremultiply_alpha(resized)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    offset = ((size - inner) // 2, (size - inner) // 2)
    canvas.alpha_composite(image, offset)
    pixels = canvas.load()
    for y in range(size):
        for x in range(size):
            r, g, b, a = pixels[x, y]
            if a < 48:
                pixels[x, y] = (0, 0, 0, 0)
    return canvas


def write_png(source: Image.Image, size: int) -> Path:
    out = ICON_DIR / f"tnuxmusic-{size}.png"
    image = resize_rgba_clean(source, size)
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
