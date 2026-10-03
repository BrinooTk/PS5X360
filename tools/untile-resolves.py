"""Turns resolve dumps (XBOX360PS5_RESOLVE_DIR) into PNG files and a contact sheet.

Each dump is a tiled 32 bits per pixel Xenos texture; width comes from its size
(1280x736, 640x384 or 1024x1024 in Dragon Ball Z Burst Limit)."""
import sys
from pathlib import Path
import numpy as np
from PIL import Image


def tiled_index(x, y, width, log_bpp=2):
    aligned = (width + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7)
    micro = ((x & 7) + ((y & 6) << 2)) << log_bpp
    offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    return ((((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
             (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp)


def convert(path, folder):
    data = np.frombuffer(path.read_bytes(), dtype=np.uint8).reshape(-1, 4)
    texels = data.shape[0]
    width = {1280 * 736: 1280, 640 * 384: 640, 1024 * 1024: 1024}.get(texels)
    if not width:
        return None
    height = texels // width
    ys, xs = np.mgrid[0:height, 0:width]
    index = tiled_index(xs, ys, width)
    picture = data[index.clip(0, texels - 1)]
    # k_8_8_8_8 in memory with the 8-in-32 swap: bytes A R G B -> keep RGB.
    rgb = picture[..., [2, 1, 0]] if "swap" in sys.argv else picture[..., [1, 2, 3]]
    image = Image.fromarray(np.ascontiguousarray(rgb), "RGB")
    out = folder / (path.stem + ".png")
    image.save(out)
    return image


folder = Path(sys.argv[1])
thumbs = []
for path in sorted(folder.glob("resolve-*.bin")):
    image = convert(path, folder)
    if image:
        image.thumbnail((320, 320))
        thumbs.append((path.stem, image))
sheet = Image.new("RGB", (4 * 330, ((len(thumbs) + 3) // 4) * 210), "gray")
for n, (name, image) in enumerate(thumbs):
    sheet.paste(image, ((n % 4) * 330, (n // 4) * 210))
sheet.save(folder / "sheet.png")
print(len(thumbs), "pictures")
