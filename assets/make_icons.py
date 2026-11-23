#!/usr/bin/env python3
"""Generate AI-Space app icon assets from a single vector-ish drawing.

Outputs (into this directory):
  icon_*.png            plain sizes used by Linux / packaging
  icon.iconset/*.png    Apple iconset, convert to icon.icns with iconutil
  icon.icns             macOS bundle icon (needs macOS iconutil)
  app.ico               Windows resource icon (multi-resolution)

Only needs Pillow:  python3 -c "import PIL"  (pip install pillow)
Re-run after any design tweak:  python3 assets/make_icons.py
"""

import math
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
S = 2048  # master canvas, supersampled for crisp downscale


def rounded_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def gradient_base(size):
    """Rounded square, diagonal indigo -> violet -> deep blue gradient."""
    radius = int(size * 0.224)  # macOS Big Sur corner ratio
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    grad = Image.new("RGB", (size, size))
    px = grad.load()
    top, mid, bot = (99, 102, 241), (139, 92, 246), (67, 56, 202)  # indigo/violet/indigo-700
    for y in range(size):
        t = y / (size - 1)
        if t < 0.5:
            k = t / 0.5
            c = tuple(int(top[i] + (mid[i] - top[i]) * k) for i in range(3))
        else:
            k = (t - 0.5) / 0.5
            c = tuple(int(mid[i] + (bot[i] - mid[i]) * k) for i in range(3))
        for x in range(size):
            px[x, y] = c
    # subtle top sheen that fades out smoothly
    sheen = Image.new("L", (size, size), 0)
    spx = sheen.load()
    fade_h = int(size * 0.45)
    for y in range(fade_h):
        v = int(30 * (1 - y / fade_h) ** 2)
        for x in range(size):
            spx[x, y] = v
    white = Image.new("RGB", (size, size), (255, 255, 255))
    grad = Image.composite(white, grad, sheen)
    img.paste(grad, (0, 0), rounded_mask(size, radius))
    # faint inner border for depth
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([1, 1, size - 2, size - 2], radius=radius - 1,
                        outline=(255, 255, 255, 28), width=max(2, size // 512))
    return img


def spark(img, cx, cy, r, color, alpha=255):
    """Four-point star (concave diamond) drawn as a polygon."""
    pts = []
    for i in range(8):
        ang = math.pi / 2 * (i / 2.0) - math.pi / 2
        # alternate between the tips and the pinched waist of the star
        k = 1.0 if i % 2 == 0 else 0.18
        rr = r * k
        pts.append((cx + rr * math.cos(ang), cy + rr * math.sin(ang)))
    overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(overlay).polygon(pts, fill=color + (alpha,))
    return Image.alpha_composite(img, overlay)


def draw_glyph(size):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    cx, cy = size * 0.5, size * 0.52
    # soft shadow under the glyph
    sh = spark(Image.new("RGBA", img.size, (0, 0, 0, 0)), cx + size * 0.012, cy + size * 0.02,
               size * 0.245, (20, 16, 60))
    sh = sh.point(lambda v: v)  # keep shape
    a = sh.split()[3].point(lambda v: int(v * 0.35))
    sh.putalpha(a)
    img = Image.alpha_composite(img, sh)
    # main spark + small companion
    img = spark(img, cx, cy, size * 0.24, (255, 255, 255))
    img = spark(img, size * 0.705, size * 0.335, size * 0.095, (255, 255, 255))
    return img


def build_master():
    img = gradient_base(S)
    glyph = draw_glyph(S)
    img = Image.alpha_composite(img, glyph)
    return img


def main():
    master = build_master()
    plain = [16, 24, 32, 48, 64, 128, 256, 512, 1024]
    for s in plain:
        master.resize((s, s), Image.LANCZOS).save(os.path.join(HERE, f"icon_{s}.png"))

    # Apple iconset (downsampled from the 2048 master for quality)
    iset = os.path.join(HERE, "icon.iconset")
    os.makedirs(iset, exist_ok=True)
    for base in (16, 32, 128, 256, 512):
        master.resize((base, base), Image.LANCZOS).save(os.path.join(iset, f"icon_{base}x{base}.png"))
        master.resize((base * 2, base * 2), Image.LANCZOS).save(
            os.path.join(iset, f"icon_{base}x{base}@2x.png"))

    # Windows multi-resolution .ico (Pillow embeds PNG for 256)
    master.resize((256, 256), Image.LANCZOS).save(
        os.path.join(HERE, "app.ico"),
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])

    print("pngs + app.ico written. On macOS run:")
    print(f"  iconutil -c icns {iset} -o {os.path.join(HERE, 'icon.icns')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
