# -*- coding: utf-8 -*-
"""
《水浒传之梁山好汉》存档编辑器 —— Logo 生成器（方案 A：朱印）

设计基调：宋代/明代水浒题材的视觉语汇
  朱印红 (#9E1B1B)  宣纸米 (#F2E8D5)  暗金 (#C9A227)
字形：楷体（simkai），有笔锋而不做作。

输出：
  logo-1024/256/128/64/48/32/16.png   多尺寸 PNG
  logo.ico                            多尺寸图标（16/24/32 用简化版）
  logo-lockup.png                     横向组合标
  logo-sizes.png                      实际像素预览

16/24/32 px 单独绘制简化版（去内环、加粗字形、去斑驳），保证小尺寸可辨。

用法：python design_logo.py
"""
import os
import random
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SS = 4                      # 超采样倍数

CRIMSON = (158, 27, 27)
PAPER   = (242, 232, 213)
GOLD    = (201, 162, 39)

FONT_CANDS = [
    r"C:\Windows\Fonts\simkai.ttf",   # 楷体
    r"C:\Windows\Fonts\simfang.ttf",
    r"C:\Windows\Fonts\simhei.ttf",
]


def font(size):
    for p in FONT_CANDS:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def speckle(img, color, count, rmax, alpha_max):
    """印泥斑驳：随机小点（带透明度），alpha 混合而非覆盖。"""
    layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    w, h = img.size
    for _ in range(count):
        x = random.uniform(0, w)
        y = random.uniform(0, h)
        r = random.uniform(0.5, rmax)
        a = random.randint(alpha_max // 4, alpha_max)
        d.ellipse([x - r, y - r, x + r, y + r], fill=color + (a,))
    return Image.alpha_composite(img, layer)


def draw_centered(d, text, f, cx, cy, fill):
    d.text((cx, cy), text, font=f, fill=fill, anchor="mm")


# ---------------------------------------------------------------- 精细版（48px 以上）
def concept_seal(size=1024, glyph="水"):
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx = cy = S // 2

    r_out = int(S * 0.47)
    d.ellipse([cx - r_out, cy - r_out, cx + r_out, cy + r_out], fill=CRIMSON + (255,))

    # 双细环
    for rr, wd in ((int(S * 0.42), max(2, int(S * 0.012))),
                   (int(S * 0.395), max(1, int(S * 0.006)))):
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr],
                  outline=PAPER + (235,), width=wd)

    # 白文「水」
    f = font(int(S * 0.62))
    draw_centered(d, glyph, f, cx, int(cy * 1.015), PAPER + (255,))

    # 印泥斑驳
    img = speckle(img, PAPER, count=int(S * 0.55), rmax=S * 0.0035, alpha_max=52)

    # 外缘轻微磨损
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse(
        [cx - r_out, cy - r_out, cx + r_out, cy + r_out], fill=255)
    mask = mask.filter(ImageFilter.GaussianBlur(S * 0.004))
    img.putalpha(Image.composite(img.getchannel("A"), Image.new("L", (S, S), 0), mask))

    return img.resize((size, size), Image.LANCZOS)


# ---------------------------------------------------------------- 简化版（16~32px）
def concept_seal_small(size=64, glyph="水"):
    """16~32px 专用：单圈、粗字形、无斑驳。"""
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    cx = cy = S // 2
    r = int(S * 0.49)

    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=CRIMSON + (255,))

    rr = int(S * 0.41)
    d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr],
              outline=PAPER + (235,), width=max(2, int(S * 0.030)))

    f = font(int(S * 0.66))
    draw_centered(d, glyph, f, cx, int(cy * 1.02), PAPER + (255,))
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        draw_centered(d, glyph, f, cx + dx * S * 0.006, int(cy * 1.02) + dy * S * 0.006,
                      PAPER + (200,))
    draw_centered(d, glyph, f, cx, int(cy * 1.02), PAPER + (255,))
    return img.resize((size, size), Image.LANCZOS)


# ---------------------------------------------------------------- 横向组合标
def lockup(seal_img, out_path, w=1600, h=520):
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    s = int(h * 0.86)
    seal = seal_img.resize((s, s), Image.LANCZOS)
    img.alpha_composite(seal, (int(h * 0.07), (h - s) // 2))

    tx = int(h * 0.07) + s + int(h * 0.10)
    avail = w - tx - int(h * 0.06)
    title = "水浒存档编辑器"

    fs = int(h * 0.34)
    while fs > 10:
        bb = d.textbbox((0, 0), title, font=font(fs))
        if (bb[2] - bb[0]) <= avail:
            break
        fs -= 4
    f1 = font(fs)
    f2 = font(int(h * 0.11))
    bb = d.textbbox((0, 0), title, font=f1)
    ty = int(h * 0.34)
    d.text((tx - bb[0], ty), title, font=f1, fill=CRIMSON + (255,), anchor="lm")
    line_y = ty + int(h * 0.20)
    d.rectangle([tx, line_y, tx + int((bb[2] - bb[0]) * 0.62), line_y + max(1, int(h * 0.007))],
                fill=GOLD + (220,))
    d.text((tx, line_y + int(h * 0.10)), "SHUIHU  SAVE  EDITOR", font=f2,
           fill=(90, 78, 62, 255), anchor="lm")
    img.save(out_path)
    print("saved:", out_path)


# ---------------------------------------------------------------- 图标与预览
def make_ico(img, out_path):
    """16/24/32 用简化版，48+ 用精细版。"""
    frames = [concept_seal_small(s) for s in (16, 24, 32)]
    frames += [img.resize((s, s), Image.LANCZOS) for s in (48, 64, 128, 256)]
    frames[-1].save(out_path, format="ICO",
                    sizes=[(f.width, f.height) for f in frames])
    print("saved:", out_path)


def size_strip(seal_img, out_path, sizes=(16, 24, 32, 48, 64)):
    pad = 14
    cell = max(sizes) + pad * 2
    strip = Image.new("RGB", (cell * len(sizes), cell), (246, 244, 239))
    d = ImageDraw.Draw(strip)
    for i, s in enumerate(sizes):
        ic = concept_seal_small(s) if s <= 32 else seal_img.resize((s, s), Image.LANCZOS)
        strip.paste(ic, (i * cell + (cell - s) // 2, (cell - s) // 2), ic)
        d.text((i * cell + 6, cell - 14), "%dpx" % s, font=font(11), fill=(120, 116, 108))
    strip.save(out_path)
    print("saved:", out_path)


def main():
    random.seed(20261110)
    final = concept_seal()

    for s in (1024, 256, 128, 64, 48, 32, 16):
        final.resize((s, s), Image.LANCZOS).save(os.path.join(HERE, "logo-%d.png" % s))

    make_ico(final, os.path.join(HERE, "logo.ico"))
    lockup(final, os.path.join(HERE, "logo-lockup.png"))
    size_strip(final, os.path.join(HERE, "logo-sizes.png"))


if __name__ == "__main__":
    main()
