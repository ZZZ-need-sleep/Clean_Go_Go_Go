"""Generate the application icon (multi-size .ico).

If a ``logo.png`` is present next to the project root it is used as the source
artwork; otherwise a small original placeholder icon is drawn.  The bundled
logo (if any) is supplied by the project owner; the MIT license covers the
source code, not necessarily third-party artwork.
"""
from pathlib import Path

from PIL import Image, ImageDraw

sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
logo_path = Path(__file__).resolve().parent.parent / "logo.png"
if logo_path.exists():
    source = Image.open(logo_path).convert("RGBA")
    source.resize((256, 256), Image.LANCZOS).save(
        "src/app.ico", format="ICO", sizes=[(s, s) for s in sizes]
    )
    print(f"wrote src/app.ico from {logo_path.name}")
    raise SystemExit(0)

S = 1024
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

# Rounded blue tile with a subtle vertical gradient.
tile = Image.new("RGBA", (S, S), (0, 0, 0, 0))
td = ImageDraw.Draw(tile)
for y in range(S):
    t = y / (S - 1)
    r = int(37 + (29 - 37) * t)
    g = int(99 + (78 - 99) * t)
    b = int(235 + (216 - 235) * t)
    td.line([(0, y), (S, y)], fill=(r, g, b, 255))
mask = Image.new("L", (S, S), 0)
md = ImageDraw.Draw(mask)
md.rounded_rectangle([36, 36, S - 36, S - 36], radius=210, fill=255)
img.paste(tile, (0, 0), mask)

# Document sheet.
d.polygon([(288, 210), (646, 210), (754, 318), (754, 800), (288, 800)], fill=(255, 255, 255, 255))
d.polygon([(646, 210), (646, 318), (754, 318)], fill=(219, 234, 254, 255))
for i, y in enumerate((390, 470, 550)):
    d.rounded_rectangle([360, y, 690 - i * 58, y + 28], radius=14, fill=(96, 165, 250, 255))
d.rounded_rectangle([360, 630, 570, 658], radius=14, fill=(147, 197, 253, 255))

# Red delete badge.
d.ellipse([570, 570, 910, 910], fill=(239, 68, 68, 255))
d.rounded_rectangle([650, 725, 830, 760], radius=17, fill=(255, 255, 255, 255))

icon = img.resize((256, 256), Image.LANCZOS)
icon.save("src/app.ico", format="ICO", sizes=[(s, s) for s in sizes])
print("wrote src/app.ico")
