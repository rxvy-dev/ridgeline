#!/usr/bin/env python3
"""Renders assets/signs/signs.png: the Japanese signboard atlas used by the towns, Tokyo,
the festival and the shrines (object shader material 22).

Layout (2048 x 3072, RGBA):
  vertical cells   128 x 512, 16 per row, rows at y = 0 and 512   -> V0..V31
  horizontal cells 512 x 128,  4 per row, 8 rows from y = 1024    -> H0..H31
  road sign cells  512 x 256,  4 per row, 4 rows from y = 2048    -> G0..G15
  (G cells with a transparent background are road markings painted on the asphalt)
The cell groups are mirrored in src/signs.h; keep them in step.

Fonts: Noto Sans CJK JP Black / Noto Serif CJK JP Bold (SIL Open Font License 1.1).
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

SANS = "/usr/share/fonts/opentype/noto/NotoSansCJK-Black.ttc"
SERIF = "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc"

WHITE, BLACK = (246, 244, 236), (22, 22, 24)
RED, NAVY, YELLOW = (200, 30, 26), (24, 46, 112), (250, 210, 60)
GREEN, BROWN, GOLD = (24, 110, 60), (92, 54, 30), (226, 180, 70)
CREAM, INDIGO = (238, 226, 196), (34, 40, 78)
NEON_BG = (14, 12, 22)

# (text, background, text colour, serif?)
VERTICAL = [
    ("ラーメン", WHITE, RED, False), ("寿司", WHITE, BLACK, True), ("居酒屋", RED, WHITE, False),
    ("そば処", CREAM, BLACK, True), ("うどん", WHITE, NAVY, False), ("喫茶", BROWN, CREAM, True),
    ("旅館", INDIGO, WHITE, True), ("焼肉", BLACK, YELLOW, False), ("天ぷら", WHITE, RED, True),
    ("呉服", CREAM, INDIGO, True), ("和菓子", WHITE, GREEN, True), ("薬局", GREEN, WHITE, False),
    ("本屋", YELLOW, BLACK, False), ("写真館", WHITE, NAVY, False), ("時計店", NAVY, WHITE, False),
    ("甘味処", CREAM, RED, True), ("民宿", WHITE, BROWN, True), ("食堂", YELLOW, RED, False),
    ("焼鳥", RED, YELLOW, False), ("歯科", WHITE, GREEN, False), ("美容室", WHITE, BLACK, False),
    ("酒店", INDIGO, GOLD, True), ("餃子", YELLOW, RED, False), ("骨董", BLACK, GOLD, True),
    # neon (Tokyo): dark boards, bright lettering
    ("カラオケ", NEON_BG, (255, 70, 170), False), ("ゲーム", NEON_BG, (80, 220, 255), False),
    ("ホテル", NEON_BG, (255, 210, 80), False), ("居酒屋", NEON_BG, (255, 90, 70), False),
    ("電気街", NEON_BG, (120, 255, 140), False), ("焼肉", NEON_BG, (255, 140, 40), False),
    # shrine plaques
    ("浅間神社", BLACK, GOLD, True), ("稲荷大明神", BLACK, GOLD, True),
]

# (text, background, text colour, serif?, small romaji or None)
HORIZONTAL = [
    ("本町通り商店街", NAVY, WHITE, False, "HONCHO STREET"), ("ラーメン 富士屋", RED, WHITE, False, "RAMEN"),
    ("寿司 まつや", WHITE, BLACK, True, None), ("コンビニ", WHITE, GREEN, False, "24h"),
    ("居酒屋 さくら", BLACK, (255, 170, 190), True, None), ("ドラッグストア", YELLOW, NAVY, False, None),
    ("喫茶 富士見", BROWN, CREAM, True, "COFFEE"), ("中華そば", WHITE, RED, False, None),
    ("酒 吉田屋", INDIGO, WHITE, True, None), ("不動産", GREEN, WHITE, False, None),
    ("郵便局", WHITE, RED, False, "〒 POST"), ("吉田のうどん", CREAM, BROWN, True, None),
    ("旅館 富士屋", INDIGO, GOLD, True, "RYOKAN"), ("洋菓子 ふじ", WHITE, (190, 80, 120), False, None),
    ("理容 さとう", WHITE, NAVY, False, None), ("食堂 はなや", YELLOW, BLACK, False, None),
    ("カメラのタカノ", RED, YELLOW, False, None), ("古着", BLACK, WHITE, False, "VINTAGE"),
    ("八百屋", GREEN, YELLOW, False, None), ("豆腐 みやた", WHITE, INDIGO, True, None),
    # festival stalls
    ("たこ焼き", RED, WHITE, False, None), ("焼きそば", YELLOW, RED, False, None),
    ("かき氷", WHITE, (30, 120, 220), False, "KAKIGORI"), ("わたあめ", (255, 200, 220), RED, False, None),
    ("金魚すくい", (30, 120, 220), WHITE, False, None), ("りんご飴", RED, YELLOW, False, None),
    # neon (Tokyo)
    ("東京", NEON_BG, (255, 60, 90), False, "TOKYO"), ("パチンコ", NEON_BG, (255, 220, 60), False, None),
    ("カラオケ", NEON_BG, (90, 230, 255), False, "KARAOKE"), ("ゲームセンター", NEON_BG, (180, 120, 255), False, None),
    # banners
    ("富士山まつり", RED, WHITE, False, "FUJI FESTIVAL"), ("ようこそ富士吉田へ", WHITE, NAVY, False, "WELCOME"),
]

# road signs: (lines [(text, size)], background or None for a road marking, text colour, kind)
GUIDE_BLUE, EXPRESS_GREEN, WARN_YELLOW = (20, 70, 160), (0, 110, 70), (250, 200, 30)
GUIDE = [
    ([("↑ 富士山", 0.30), ("Mt. Fuji", 0.13), ("← 河口湖   東京 →", 0.22), ("Kawaguchiko      Tokyo", 0.10)], GUIDE_BLUE, WHITE, "guide"),
    ([("富士吉田", 0.42), ("Fujiyoshida", 0.18)], GUIDE_BLUE, WHITE, "guide"),
    ([("東京  Tokyo", 0.30), ("横浜  Yokohama", 0.24)], GUIDE_BLUE, WHITE, "guide"),
    ([("富士スカイライン", 0.32), ("Fuji Skyline", 0.17)], EXPRESS_GREEN, WHITE, "guide"),
    ([("いろは坂", 0.42), ("Irohazaka", 0.18)], GUIDE_BLUE, WHITE, "guide"),
    ([("日本海", 0.42), ("Sea of Japan", 0.18)], GUIDE_BLUE, WHITE, "guide"),
    ([("富士山 五合目", 0.34), ("Mt. Fuji 5th Station", 0.15)], EXPRESS_GREEN, WHITE, "guide"),
    ([("大黒ふ頭", 0.40), ("Daikoku Wharf", 0.17)], EXPRESS_GREEN, WHITE, "guide"),
    ([("止まれ", 0.80)], None, WHITE, "mark"),
    ([("速度落とせ", 0.62)], None, WHITE, "mark"),
    ([("落石注意", 0.46)], WARN_YELLOW, BLACK, "warn"),
    ([("急カーブ", 0.46)], WARN_YELLOW, BLACK, "warn"),
    ([("動物注意", 0.46)], WARN_YELLOW, BLACK, "warn"),
    ([("峠", 0.5), ("Touge Pass", 0.16)], GUIDE_BLUE, WHITE, "guide"),
    ([("秩父  Chichibu", 0.28), ("榛名  Haruna", 0.24)], GUIDE_BLUE, WHITE, "guide"),
    ([("ようこそ富士山へ", 0.32), ("Welcome to Mt. Fuji", 0.15)], (120, 30, 30), WHITE, "guide"),
]

VW, VH, HW, HH = 128, 512, 512, 128
GW, GH = 512, 256


def font(serif, size):
    return ImageFont.truetype(SERIF if serif else SANS, size)


def frame(d, x0, y0, x1, y1, bg, fg, neon):
    d.rectangle([x0, y0, x1 - 1, y1 - 1], fill=bg)
    if neon:
        d.rectangle([x0 + 5, y0 + 5, x1 - 6, y1 - 6], outline=fg, width=3)
    else:
        edge = tuple(int(c * 0.55) for c in bg)
        d.rectangle([x0, y0, x1 - 1, y1 - 1], outline=edge, width=6)
        d.rectangle([x0 + 9, y0 + 9, x1 - 10, y1 - 10], outline=fg, width=2)


def glow(img, box, fg):
    """Soft halo behind neon lettering."""
    from PIL import ImageFilter
    x0, y0, x1, y1 = box
    crop = img.crop(box)
    mask = Image.new("L", crop.size, 0)
    px, m = crop.load(), mask.load()
    for y in range(crop.size[1]):
        for x in range(crop.size[0]):
            r, g, b, _ = px[x, y]
            if abs(r - fg[0]) + abs(g - fg[1]) + abs(b - fg[2]) < 90:
                m[x, y] = 255
    halo = mask.filter(ImageFilter.GaussianBlur(7))
    tint = Image.new("RGBA", crop.size, fg + (255,))
    base = Image.new("RGBA", crop.size, NEON_BG + (255,))
    lit = Image.composite(tint, base, halo.point(lambda v: int(v * 0.55)))
    out = Image.composite(crop, lit, mask)
    img.paste(out, (x0, y0))


def vertical(img, d, i, text, bg, fg, serif):
    x0, y0 = (i % 16) * VW, (i // 16) * VH
    neon = bg == NEON_BG
    frame(d, x0, y0, x0 + VW, y0 + VH, bg, fg, neon)
    n = len(text)
    size = int(min(VW * 0.74, (VH - 44) / n * 0.92))
    f = font(serif, size)
    step = min((VH - 40) / n, size * 1.12)
    top = y0 + VH / 2 - step * n / 2
    for k, ch in enumerate(text):
        cy = top + step * (k + 0.5)
        if ch in "ー〜":  # long vowel mark turns upright in vertical writing
            g = Image.new("RGBA", (size * 2, size * 2), (0, 0, 0, 0))
            ImageDraw.Draw(g).text((size, size), ch, font=f, fill=fg, anchor="mm")
            g = g.rotate(-90)
            img.alpha_composite(g, (int(x0 + VW / 2 - size), int(cy - size)))
        else:
            d.text((x0 + VW / 2, cy), ch, font=f, fill=fg, anchor="mm")
    if neon:
        glow(img, (x0, y0, x0 + VW, y0 + VH), fg)


def horizontal(img, d, i, text, bg, fg, serif, sub):
    x0, y0 = (i % 4) * HW, 1024 + (i // 4) * HH
    neon = bg == NEON_BG
    frame(d, x0, y0, x0 + HW, y0 + HH, bg, fg, neon)
    main_h = HH * (0.56 if sub else 0.66)
    size = int(main_h)
    f = font(serif, size)
    while size > 10:
        f = font(serif, size)
        bb = d.textbbox((0, 0), text, font=f)
        if bb[2] - bb[0] <= HW * 0.88:
            break
        size -= 2
    cy = y0 + (HH * 0.42 if sub else HH * 0.5)
    d.text((x0 + HW / 2, cy), text, font=f, fill=fg, anchor="mm")
    if sub:
        fs = font(False, int(HH * 0.17))
        d.text((x0 + HW / 2, y0 + HH * 0.82), sub, font=fs, fill=fg, anchor="mm")
    if neon:
        glow(img, (x0, y0, x0 + HW, y0 + HH), fg)


def road_sign(img, d, i, lines, bg, fg, kind):
    x0, y0 = (i % 4) * GW, 2048 + (i // 4) * GH
    if kind == "mark":
        # painted on the road: white letters, stretched long so they read at a glance from a car
        tmp = Image.new("RGBA", (GW, GH // 2), (255, 255, 255, 0))
        td = ImageDraw.Draw(tmp)
        text, size = lines[0]
        sz = int(GH // 2 * size)
        f = font(False, sz)
        while td.textbbox((0, 0), text, font=f)[2] > GW * 0.92 and sz > 10:
            sz -= 2
            f = font(False, sz)
        td.text((GW / 2, GH / 4), text, font=f, fill=fg + (255,), anchor="mm")
        img.paste(tmp.resize((GW, GH)), (x0, y0))
        return
    d.rounded_rectangle([x0 + 2, y0 + 2, x0 + GW - 3, y0 + GH - 3], radius=18, fill=bg)
    d.rounded_rectangle([x0 + 9, y0 + 9, x0 + GW - 10, y0 + GH - 10], radius=13, outline=fg, width=4)
    total = sum(sz for _, sz in lines)
    y = y0 + GH * (1 - total) / 2
    for text, size in lines:
        sz = int(GH * size * 0.86)
        f = font(False, sz)
        while d.textbbox((0, 0), text, font=f)[2] > GW * 0.88 and sz > 10:
            sz -= 2
            f = font(False, sz)
        d.text((x0 + GW / 2, y + GH * size / 2), text, font=f, fill=fg, anchor="mm")
        y += GH * size


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "signs", "signs.png")
    assert len(VERTICAL) == 32 and len(HORIZONTAL) == 32
    assert len(GUIDE) == 16
    img = Image.new("RGBA", (2048, 3072), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)
    for i, (t, bg, fg, serif) in enumerate(VERTICAL):
        vertical(img, d, i, t, bg, fg, serif)
    for i, (t, bg, fg, serif, sub) in enumerate(HORIZONTAL):
        horizontal(img, d, i, t, bg, fg, serif, sub)
    for i, (lines, bg, fg, kind) in enumerate(GUIDE):
        road_sign(img, d, i, lines, bg, fg, kind)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    img.save(out, optimize=True)
    print("wrote", out)
    # board colours for the C++ side, so a signboard wider than its lettering can be filled to match
    hdr = os.path.join(os.path.dirname(__file__), "..", "src", "signs_gen.h")
    with open(hdr, "w") as h:
        h.write("// Generated by tools/make_signs.py - do not edit. Board colour of each atlas cell.\n#pragma once\n")
        for name, rows in (("kSignBgV", VERTICAL), ("kSignBgH", HORIZONTAL)):
            h.write("static const float %s[32][3] = {\n" % name)
            for r in rows:
                h.write("    {%.3ff, %.3ff, %.3ff},  // %s\n" % (r[1][0] / 255, r[1][1] / 255, r[1][2] / 255, r[0]))
            h.write("};\n")
        h.write("static const float kSignBgG[16][3] = {\n")
        for lines, bg, fg, kind in GUIDE:
            c = bg if bg else (0.5 * 255, 0.5 * 255, 0.5 * 255)
            h.write("    {%.3ff, %.3ff, %.3ff},  // %s\n" % (c[0] / 255, c[1] / 255, c[2] / 255, lines[0][0]))
        h.write("};\n")
    print("wrote", hdr)


if __name__ == "__main__":
    main()
