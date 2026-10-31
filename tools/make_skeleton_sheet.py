#!/usr/bin/env python3
"""Paint the soldier part sheet that assets/skeleton/soldier.json rigs.

A skeleton needs its art cut into the pieces the bones move, so unlike the
rest of the art tree — which is AI-illustrated and keyed by tools/art_key.py —
this sheet is drawn here, because a rig cares exactly where each part's joint
sits and a generated image cannot promise that.

Layout is a 4x2 grid of 128x128 cells. Head, torso and backpack are centred in
their cell. Every limb and the rifle are drawn horizontally pointing right with
the round joint 48px left of the cell centre, so the JSON can hang them off a
bone with a single offset and the part pivots where a real shoulder, elbow, hip
or knee would.

  python3 tools/make_skeleton_sheet.py [out.png]
"""

import sys

from PIL import Image, ImageDraw

CELL = 128
COLS, ROWS = 4, 2
W, H = CELL * COLS, CELL * ROWS

# One palette for the whole rig, so the parts read as one soldier.
OUTLINE = (24, 22, 28, 255)
ARMOUR = (72, 92, 74, 255)      # helmet and vest
ARMOUR_HI = (98, 122, 98, 255)
CLOTH = (58, 62, 78, 255)       # sleeves and trousers
CLOTH_HI = (80, 86, 106, 255)
SKIN = (206, 156, 116, 255)
SKIN_SHADE = (176, 126, 90, 255)
LEATHER = (60, 44, 34, 255)
STEEL = (52, 54, 60, 255)
STEEL_HI = (96, 100, 110, 255)
BRASS = (176, 138, 62, 255)

JOINT_X = CELL // 2 - 48  # where a limb's pivot sits inside its cell
MID_Y = CELL // 2


def cell_origin(col, row):
    return col * CELL, row * CELL


def outlined(draw, points, fill, width=3):
    """Flat fill with a dark outline, the cel-shaded look of the other demos."""
    draw.polygon(points, fill=fill)
    draw.line(list(points) + [points[0]], fill=OUTLINE, width=width)


def capsule(draw, x0, x1, cy, half, fill, hi=None):
    """A limb: a rounded bar from x0 to x1, `half` thick above and below cy."""
    draw.rounded_rectangle((x0, cy - half, x1, cy + half), radius=half, fill=fill)
    draw.rounded_rectangle((x0, cy - half, x1, cy + half), radius=half, outline=OUTLINE,
                           width=3)
    if hi is not None:
        # A single top highlight band reads as the light falling from above.
        draw.rounded_rectangle((x0 + 6, cy - half + 4, x1 - 6, cy - half + 4 + half // 2),
                               radius=half // 4, fill=hi)


def joint(draw, cx, cy, r=9):
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(38, 38, 46, 255), outline=OUTLINE,
                 width=3)
    draw.ellipse((cx - r // 2, cy - r // 2, cx + r // 2, cy + r // 2),
                 fill=STEEL_HI)


def head(d, ox, oy):
    c = (ox + CELL // 2, oy + MID_Y)
    # neck, then the face, then the helmet over the top
    d.rectangle((c[0] - 8, c[1] + 18, c[0] + 10, c[1] + 34), fill=SKIN_SHADE, outline=OUTLINE,
                width=3)
    outlined(d, [(c[0] - 16, c[1] - 12), (c[0] + 18, c[1] - 14), (c[0] + 20, c[1] + 12),
                 (c[0] + 6, c[1] + 24), (c[0] - 16, c[1] + 20)], SKIN)
    d.rectangle((c[0] + 8, c[1] - 2, c[0] + 17, c[1] + 4), fill=OUTLINE)  # eye slit
    outlined(d, [(c[0] - 22, c[1] - 8), (c[0] - 18, c[1] - 26), (c[0] + 8, c[1] - 30),
                 (c[0] + 22, c[1] - 16), (c[0] + 24, c[1] - 4), (c[0] + 10, c[1] - 6),
                 (c[0] - 20, c[1] - 2)], ARMOUR)
    d.line((c[0] - 14, c[1] - 20, c[0] + 14, c[1] - 16), fill=ARMOUR_HI, width=4)
    d.rectangle((c[0] - 24, c[1] - 6, c[0] - 12, c[1] + 14), fill=ARMOUR, outline=OUTLINE,
                width=3)  # ear flap


def torso(d, ox, oy):
    c = (ox + CELL // 2, oy + MID_Y)
    outlined(d, [(c[0] - 24, c[1] - 30), (c[0] + 24, c[1] - 30), (c[0] + 28, c[1] + 4),
                 (c[0] + 20, c[1] + 34), (c[0] - 20, c[1] + 34), (c[0] - 28, c[1] + 4)],
             ARMOUR)
    d.rectangle((c[0] - 22, c[1] - 6, c[0] + 22, c[1] + 10), fill=(58, 74, 60, 255),
                outline=OUTLINE, width=2)  # vest belt pouch
    d.line((c[0] - 20, c[1] - 24, c[0] + 16, c[1] - 2), fill=ARMOUR_HI, width=5)
    d.rectangle((c[0] - 6, c[1] - 30, c[0] + 8, c[1] + 34), fill=CLOTH, outline=OUTLINE,
                width=2)  # webbing strap
    joint(d, c[0] - 20, c[1] - 20, r=8)  # shoulder socket the arm bone sits in


def upper_arm(d, ox, oy):
    capsule(d, ox + JOINT_X, ox + CELL - 22, oy + MID_Y, 13, CLOTH, CLOTH_HI)
    joint(d, ox + JOINT_X, oy + MID_Y)


def forearm(d, ox, oy):
    capsule(d, ox + JOINT_X, ox + CELL - 34, oy + MID_Y, 11, CLOTH, CLOTH_HI)
    # glove
    outlined(d, [(ox + CELL - 40, oy + MID_Y - 14), (ox + CELL - 12, oy + MID_Y - 16),
                 (ox + CELL - 8, oy + MID_Y + 2), (ox + CELL - 16, oy + MID_Y + 16),
                 (ox + CELL - 40, oy + MID_Y + 14)], LEATHER)
    joint(d, ox + JOINT_X, oy + MID_Y)


def thigh(d, ox, oy):
    capsule(d, ox + JOINT_X, ox + CELL - 20, oy + MID_Y, 17, CLOTH, CLOTH_HI)
    d.rectangle((ox + CELL - 44, oy + MID_Y - 4, ox + CELL - 22, oy + MID_Y + 12),
                fill=(48, 52, 66, 255), outline=OUTLINE, width=2)  # cargo pocket
    joint(d, ox + JOINT_X, oy + MID_Y, r=11)


def shin(d, ox, oy):
    capsule(d, ox + JOINT_X, ox + CELL - 46, oy + MID_Y, 12, CLOTH, CLOTH_HI)
    outlined(d, [(ox + CELL - 52, oy + MID_Y - 12), (ox + CELL - 6, oy + MID_Y - 10),
                 (ox + CELL - 2, oy + MID_Y + 10), (ox + CELL - 52, oy + MID_Y + 12)],
             (40, 34, 30, 255))  # boot
    d.rectangle((ox + CELL - 52, oy + MID_Y + 8, ox + CELL - 2, oy + MID_Y + 16),
                fill=OUTLINE)  # sole
    joint(d, ox + JOINT_X, oy + MID_Y)


def rifle(d, ox, oy):
    y = oy + MID_Y - 4
    d.rectangle((ox + 18, y - 7, ox + CELL - 34, y + 7), fill=STEEL, outline=OUTLINE,
                width=3)  # receiver
    d.rectangle((ox + CELL - 36, y - 3, ox + CELL - 8, y + 3), fill=(38, 40, 46, 255),
                outline=OUTLINE, width=2)  # barrel
    outlined(d, [(ox + 44, y + 6), (ox + 66, y + 6), (ox + 62, y + 30), (ox + 46, y + 30)],
             (44, 46, 54, 255))  # magazine
    outlined(d, [(ox + 18, y + 6), (ox + 34, y + 6), (ox + 30, y + 26), (ox + 18, y + 22)],
             LEATHER)  # grip
    d.rectangle((ox + 74, y - 13, ox + 100, y - 7), fill=STEEL_HI, outline=OUTLINE,
                width=2)  # sight rail
    d.ellipse((ox + CELL - 14, y - 4, ox + CELL - 6, y + 4), fill=BRASS, outline=OUTLINE,
              width=2)  # muzzle


def backpack(d, ox, oy):
    c = (ox + CELL // 2, oy + MID_Y)
    d.rounded_rectangle((c[0] - 22, c[1] - 24, c[0] + 22, c[1] + 24), radius=8,
                        fill=(74, 62, 46, 255), outline=OUTLINE, width=3)
    d.line((c[0] - 22, c[1] - 6, c[0] + 22, c[1] - 6), fill=OUTLINE, width=3)
    d.rounded_rectangle((c[0] - 26, c[1] - 30, c[0] + 26, c[1] - 18), radius=6,
                        fill=(120, 104, 72, 255), outline=OUTLINE, width=3)  # bedroll
    d.rectangle((c[0] - 6, c[1] + 2, c[0] + 6, c[1] + 16), fill=BRASS, outline=OUTLINE,
                width=2)  # buckle


PARTS = [
    ("head", head), ("torso", torso), ("arm_upper", upper_arm), ("arm_lower", forearm),
    ("leg_upper", thigh), ("leg_lower", shin), ("rifle", rifle), ("pack", backpack),
]


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else "assets/skeleton/soldier.png"
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for index, (name, paint) in enumerate(PARTS):
        col, row = index % COLS, index // COLS
        paint(d, *cell_origin(col, row))
    img.save(dst)
    cells = ", ".join(f"{name}@{i % COLS},{i // COLS}" for i, (name, _) in enumerate(PARTS))
    print(f"{dst}: {W}x{H} sheet, {COLS}x{ROWS} cells of {CELL}, joints at x={JOINT_X}")
    print(f"  parts: {cells}")


if __name__ == "__main__":
    main()
