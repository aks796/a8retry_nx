#!/usr/bin/env python3
"""make_icon.py -- the launcher NRO's icon (launcher/icon.jpg, 256 x 256).

Built from the user's own copy of the game, like everything else of the
game's in this port: the McLaren P1 from one of the game's loading screens
(textures_android/loading_01_final.tga in the OBB: a PVR v3, uncompressed
RGBA atlas of three 915 x 339 banners) and the mod's "ASPHALT 8 AIRBORNE
RETRY" title (res/drawable/saveload_title.png in A8R.apk), laid out as a
Switch title's icon: full-bleed art, the logo at the bottom over a shade.

  tools/icon/make_icon.py --apk A8R.apk --obb main.sa2.Asphalt8.obb [--out launcher/icon.jpg]

The icon then carries Gameloft's art and the mod's logo: fine for a build
made for oneself, not for sharing the NRO. MIT.
"""
import argparse
import io
import struct
import zipfile

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

BANNERS = [(1, 340), (340, 679), (679, 1018)]  # rows of the atlas's three banners
ATLAS_W = 915                                   # their width


def loading_atlas(obb):
    with zipfile.ZipFile(obb) as z:
        d = z.read("textures_android/loading_01_final.tga")
    if d[:4] != b"PVR\x03" or d[8:12] != b"rgba":
        raise SystemExit("loading_01_final.tga is not the PVR v3 RGBA texture this expects")
    h, w = struct.unpack_from("<II", d, 24)
    meta = struct.unpack_from("<I", d, 48)[0]
    off = 52 + meta
    return Image.frombytes("RGBA", (w, h), d[off:off + w * h * 4]).convert("RGB")


def title(apk):
    with zipfile.ZipFile(apk) as z:
        return Image.open(io.BytesIO(z.read("res/drawable/saveload_title.png"))).convert("RGBA")


def make(atlas, logo, band=2, cx=330, size=256):
    y0, y1 = BANNERS[band]
    h = y1 - y0
    x0 = max(1, min(ATLAS_W - h, cx - h // 2))
    art = atlas.crop((x0, y0, x0 + h, y0 + h)).resize((size, size), Image.LANCZOS)
    art = ImageEnhance.Contrast(art).enhance(1.08).convert("RGBA")
    # a shade under the logo, growing towards the bottom
    shade = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(shade)
    top = int(size * 0.58)
    for y in range(top, size):
        d.line([(0, y), (size - 1, y)], fill=int((y - top) / (size - top) * 235))
    art = Image.composite(Image.new("RGBA", (size, size), (6, 6, 10, 255)), art, shade)
    lw = int(size * 0.92)
    lh = int(logo.height * lw / logo.width)
    lg = logo.resize((lw, lh), Image.LANCZOS)
    glow = Image.new("RGBA", lg.size, (0, 0, 0, 0))
    glow.putalpha(lg.split()[3].point(lambda v: int(v * 0.8)))
    glow = glow.filter(ImageFilter.GaussianBlur(3))
    x, y = (size - lw) // 2, size - lh - int(size * 0.04)
    art.alpha_composite(glow, (x + 1, y + 2))
    art.alpha_composite(lg, (x, y))
    return art.convert("RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", required=True)
    ap.add_argument("--obb", required=True)
    ap.add_argument("--out", default="launcher/icon.jpg")
    a = ap.parse_args()
    make(loading_atlas(a.obb), title(a.apk)).save(a.out, "JPEG", quality=95)
    print("wrote", a.out)


if __name__ == "__main__":
    main()
