"""The mod's image: a screenshot of the editor with the title over it, in the
style of Seasons of Pywel's (Palatino, warm gold, a dark halo).

  python make_banner.py <screenshot> <out.png> [width]

The screenshot's top strip (the game's HUD counters over the panel) and as
much of its left edge are cropped off, keeping 16:9."""
import sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

FONTS = r'C:\Windows\Fonts'
TITLE = 'Character Creator'
SUBTITLE = 'AN IN-GAME APPEARANCE EDITOR FOR CRIMSON DESERT'
TOP_CROP = 140 / 2160        # the HUD row above the panel (of the height)
TITLE_CENTRE = 0.29          # of the width: over the mountains, left of the panel
SHADE_UNTIL = 0.36           # of the height
SHADE_FADE = (0.50, 0.60)    # of the width: the shade gives way before the panel


def spaced(draw, xy, text, font, spacing, fill):
    """Text with letter spacing, centred on xy."""
    widths = [draw.textlength(c, font=font) for c in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x = xy[0] - total / 2
    for c, w in zip(text, widths):
        draw.text((x, xy[1]), c, font=font, fill=fill)
        x += w + spacing
    return total


def text_layer(size, draw_fn):
    layer = Image.new('L', size, 0)
    draw_fn(ImageDraw.Draw(layer))
    return layer


def main():
    src, out = sys.argv[1], sys.argv[2]
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 1920
    shot = Image.open(src).convert('RGB')
    top = round(shot.height * TOP_CROP)
    left = round(top * shot.width / shot.height)
    shot = shot.crop((left, top, shot.width, shot.height))
    height = round(width * shot.height / shot.width)
    size = (width, height)
    canvas = shot.resize(size, Image.LANCZOS)
    s = width / 1500.0

    # Shade at the top left, behind the title.
    shade = Image.new('L', size, 0)
    px = shade.load()
    f0, f1 = SHADE_FADE[0] * width, SHADE_FADE[1] * width
    for y in range(height):
        t = y / height
        if t >= SHADE_UNTIL:
            break
        a = 190 * (1 - t / SHADE_UNTIL) ** 1.5
        for x in range(width):
            k = 1 if x <= f0 else 0 if x >= f1 else 1 - (x - f0) / (f1 - f0)
            px[x, y] = int(a * k)
    canvas = Image.composite(Image.new('RGB', size, (8, 6, 10)), canvas, shade.filter(ImageFilter.GaussianBlur(30 * s)))

    # The title: warm gold to pale, a dark halo and a soft glow behind.
    title_font = ImageFont.truetype(FONTS + r'\palab.ttf', round(84 * s))
    sub_font = ImageFont.truetype(FONTS + r'\pala.ttf', round(15 * s))
    cx = width * TITLE_CENTRE
    ty = round(50 * s)

    title_mask = text_layer(size, lambda d: spaced(d, (cx, ty), TITLE, title_font, round(2 * s), 255))
    bbox = title_mask.getbbox()
    halo = title_mask.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(10 * s))
    canvas = Image.composite(Image.new('RGB', size, (0, 0, 0)), canvas, halo.point(lambda v: min(255, int(v * 1.25))))
    warm = title_mask.filter(ImageFilter.GaussianBlur(26 * s))
    canvas = Image.composite(Image.new('RGB', size, (255, 196, 110)), canvas, warm.point(lambda v: int(v * 0.35)))

    fill = Image.new('RGB', size)
    fd = ImageDraw.Draw(fill)
    y0, y1 = bbox[1], bbox[3]
    for y in range(height):
        t = 0 if y <= y0 else 1 if y >= y1 else (y - y0) / (y1 - y0)
        top_c, bottom_c = (255, 246, 222), (232, 172, 82)
        fd.line([(0, y), (width, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(top_c, bottom_c)))
    edge = title_mask.filter(ImageFilter.MaxFilter(3))
    canvas = Image.composite(Image.new('RGB', size, (54, 30, 12)), canvas, edge)
    canvas = Image.composite(fill, canvas, title_mask)

    # A thin rule on each side of the subtitle under the title.
    sub_y = bbox[3] + round(18 * s)
    sub_mask = text_layer(size, lambda dd: spaced(dd, (cx, sub_y), SUBTITLE, sub_font, round(4 * s), 255))
    sb = sub_mask.getbbox()
    canvas = Image.composite(Image.new('RGB', size, (0, 0, 0)), canvas, sub_mask.filter(ImageFilter.GaussianBlur(4 * s)))
    canvas = Image.composite(Image.new('RGB', size, (238, 226, 204)), canvas, sub_mask)
    d = ImageDraw.Draw(canvas)
    mid = (sb[1] + sb[3]) / 2
    gap, reach = round(18 * s), round(90 * s)
    for side in (-1, 1):
        x_in = sb[0] - gap if side < 0 else sb[2] + gap
        for i in range(reach):
            a = 1 - i / reach
            d.point((x_in + side * i, mid), fill=tuple(int(c * a + 30 * (1 - a)) for c in (232, 190, 120)))
        d.ellipse([x_in - 3 * s * side - 3 * s, mid - 3 * s, x_in - 3 * s * side + 3 * s, mid + 3 * s], fill=(240, 200, 130))

    canvas.save(out, quality=95)
    print(out, canvas.size)


if __name__ == '__main__':
    main()
