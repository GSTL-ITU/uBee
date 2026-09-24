#!/usr/bin/env python3
"""Render the application icon.

One geometry, two outputs: the SVG the desktop entry points at when a theme
wants something scalable, and the PNGs Qt compiles into the binary so the
window has an icon before anything is installed at all.

The mark is what the program is: a 32-bit instruction split into its fields,
in the widths the encoding actually has. Below about 32px the fields stop
being separable, so the small sizes drop the lettering and keep the bar.
"""
from PIL import Image, ImageDraw, ImageFont
import pathlib

S = 256                      # design canvas; every size is scaled from this
MARGIN, RADIUS = 8, 52
BG, EDGE, TEXT = "#24272e", "#3d597e", "#d6d6d6"

# R-type: funct7 rs2 rs1 funct3 rd opcode -- 32 bits, drawn to width.
FIELDS = [(7, "#6a9955"), (5, "#569cd6"), (5, "#4ec9b0"),
          (3, "#c586c0"), (5, "#dcdcaa"), (7, "#ce9178")]
BAR_X0, BAR_X1, BAR_Y0, BAR_Y1 = 34, 222, 62, 116
GAP = 5
TEXT_MID = 178   # centre of the lettering, not its top

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"


def bar_segments():
    total = sum(bits for bits, _ in FIELDS)
    span = BAR_X1 - BAR_X0
    x, out = BAR_X0, []
    for i, (bits, colour) in enumerate(FIELDS):
        w = span * bits / total
        x1 = x + w - (GAP if i < len(FIELDS) - 1 else 0)
        out.append((x, x1, colour))
        x += w
    return out


def png(size: int, lettered: bool) -> Image.Image:
    ss = 4                                   # supersample, then reduce
    img = Image.new("RGBA", (S * ss, S * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([MARGIN * ss, MARGIN * ss, (S - MARGIN) * ss, (S - MARGIN) * ss],
                        radius=RADIUS * ss, fill=BG, outline=EDGE, width=3 * ss)

    y0, y1 = (BAR_Y0, BAR_Y1) if lettered else (96, 160)   # thicker when alone
    for x0, x1, colour in bar_segments():
        d.rounded_rectangle([x0 * ss, y0 * ss, x1 * ss, y1 * ss], radius=4 * ss, fill=colour)

    if lettered:
        font = ImageFont.truetype(FONT, 78 * ss)
        d.text((S / 2 * ss, TEXT_MID * ss), "RV32", font=font, fill=TEXT, anchor="mm")

    return img.resize((size, size), Image.LANCZOS)


def svg() -> str:
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{S}" height="{S}" '
             f'viewBox="0 0 {S} {S}">',
             f'<rect x="{MARGIN}" y="{MARGIN}" width="{S-2*MARGIN}" height="{S-2*MARGIN}" '
             f'rx="{RADIUS}" fill="{BG}" stroke="{EDGE}" stroke-width="3"/>']
    for x0, x1, colour in bar_segments():
        parts.append(f'<rect x="{x0:.1f}" y="{BAR_Y0}" width="{x1-x0:.1f}" '
                     f'height="{BAR_Y1-BAR_Y0}" rx="4" fill="{colour}"/>')
    parts.append(f'<text x="{S/2}" y="{TEXT_MID}" text-anchor="middle" '
                 f'dominant-baseline="central" fill="{TEXT}" '
                 f'font-family="DejaVu Sans Mono, monospace" font-weight="bold" '
                 f'font-size="78">RV32</text>')
    parts.append("</svg>")
    return "\n".join(parts)


here = pathlib.Path(__file__).parent
icons = here / "icons"
icons.mkdir(exist_ok=True)
for size in (16, 24, 32, 48, 64, 128, 256):
    png(size, lettered=size >= 32).save(icons / f"rv32-{size}.png")
(here / "rv32.svg").write_text(svg())
print("wrote", ", ".join(sorted(p.name for p in icons.iterdir())), "and rv32.svg")
