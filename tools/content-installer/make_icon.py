"""Draws icon.ico (16-256 px) for the Content Installer: a green tile, an open box with an arrow
going in (content being installed) and a blue ring (the PS5). Own drawing, no outside artwork.

    python tools/content-installer/make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
S = 1024  # drawn large, scaled down for each icon size


def tile():
    image = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    # Background: a rounded square, dark green at the bottom to a lighter green at the top.
    gradient = Image.new('RGBA', (S, S))
    pixels = gradient.load()
    for y in range(S):
        t = y / (S - 1)
        colour = (int(30 + (8 - 30) * t), int(160 + (70 - 160) * t), int(60 + (30 - 60) * t), 255)
        for x in range(S):
            pixels[x, y] = colour
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((40, 40, S - 40, S - 40), radius=210, fill=255)
    image.paste(gradient, (0, 0), mask)
    draw = ImageDraw.Draw(image)

    # The PS5's blue ring around the box.
    draw.ellipse((150, 150, S - 150, S - 150), outline=(0, 112, 209, 255), width=46)

    # The open box: front face, side flaps, a darker inside.
    box = [(300, 520), (724, 520), (724, 790), (300, 790)]
    shadow = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).polygon([(x + 18, y + 22) for x, y in box], fill=(0, 0, 0, 110))
    image.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(18)))
    draw = ImageDraw.Draw(image)
    draw.polygon([(300, 520), (724, 520), (690, 470), (334, 470)], fill=(35, 60, 40, 255))  # inside
    draw.polygon(box, fill=(250, 250, 250, 255))
    draw.polygon([(300, 520), (334, 470), (250, 430), (220, 500)], fill=(225, 230, 225, 255))  # left flap
    draw.polygon([(724, 520), (690, 470), (774, 430), (804, 500)], fill=(225, 230, 225, 255))  # right flap
    draw.line([(300, 600), (724, 600)], fill=(200, 210, 200, 255), width=10)

    # The arrow going into the box.
    green = (16, 124, 16, 255)
    draw.rectangle((462, 220, 562, 500), fill=(255, 255, 255, 255))
    draw.rectangle((478, 236, 546, 500), fill=green)
    draw.polygon([(392, 440), (632, 440), (512, 600)], fill=(255, 255, 255, 255))
    draw.polygon([(420, 456), (604, 456), (512, 578)], fill=green)
    return image


def main():
    image = tile()
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    image.resize((256, 256), Image.LANCZOS).save(HERE / 'icon.ico', sizes=sizes)
    image.resize((512, 512), Image.LANCZOS).save(HERE / 'icon.png')
    print('wrote', HERE / 'icon.ico', HERE / 'icon.png')


if __name__ == '__main__':
    main()
