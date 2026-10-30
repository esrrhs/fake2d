#!/usr/bin/env python3
"""Bake AI-generated key art into the engine's sprite sheets.

The image model cannot emit alpha, so sprite art is generated on a flat
magenta field and keyed out here. Every sprite PNG the demos load from
assets/art or an assets/<game>/ folder was produced with this tool, which
keeps the committed art reproducible from the raw generations.

  python3 tools/art_key.py sprite   in.png out.png 86 26
  python3 tools/art_key.py grid     in.png out.png 2 2 32 32
  python3 tools/art_key.py spin     in.png out.png 32
  python3 tools/art_key.py texture  in.png out.png 960 540
"""

import sys

import numpy as np
from PIL import Image

KEY = np.array([255.0, 0.0, 255.0], dtype=np.float32)
# Coverage ramp on the normalised distance to the key colour: inside INNER the
# pixel is background, outside OUTER it is fully kept, and the band between is
# linear so antialiased sprite edges keep their softness.
INNER, OUTER = 0.20, 0.55
# Magenta-dominance key. The model paints a soft pink bloom around lit sprites,
# which a distance test alone cannot strip without eating the sprite. That bloom
# is always red+blue heavy and green poor, so min(r,b) - g detects the
# background while leaving white, gold, cyan and black artwork untouched.
MAGENTA_GAIN = 2.6


def fail(msg):
    print(msg, file=sys.stderr)
    raise SystemExit(1)


def key_out(im, gain=None):
    """Remove the magenta field, un-premultiplying it out of the sprite."""
    gain = MAGENTA_GAIN if gain is None else gain
    arr = np.asarray(im.convert("RGB"), dtype=np.float32)
    r, g, b = arr[..., 0], arr[..., 1], arr[..., 2]
    dist = np.sqrt(((arr - KEY) ** 2).sum(-1)) / 441.67
    a = np.clip((dist - INNER) / (OUTER - INNER), 0.0, 1.0)
    dom = np.clip(np.minimum(r, b) - g, 0.0, None) * (gain / 255.0)
    a = np.minimum(a, np.clip(1.0 - dom, 0.0, 1.0))
    # observed = a*fg + (1-a)*key, so recover the true foreground colour.
    fg = (arr - (1.0 - a[..., None]) * KEY) / np.maximum(a[..., None], 1e-3)
    out = np.dstack([np.clip(fg, 0, 255), (a * 255.0)[..., None]]).astype(np.uint8)
    return Image.fromarray(out)


def trim(im, pad=0):
    box = im.getbbox()
    if box is None:
        fail("nothing survived the key: was the background magenta?")
    return im.crop((max(0, box[0] - pad), max(0, box[1] - pad),
                    min(im.width, box[2] + pad), min(im.height, box[3] + pad)))


def cover(im, w, h):
    """Scale to fill w x h, then centre-crop, so cells keep their aspect."""
    s = max(w / im.width, h / im.height)
    im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))),
                   Image.LANCZOS)
    x, y = (im.width - w) // 2, (im.height - h) // 2
    return im.crop((x, y, x + w, y + h))


def sprite(argv):
    # Optional trailing gain relaxes the magenta-dominance term, which dark
    # purple-ish artwork (a charcoal rope) would otherwise key itself away.
    gain = float(argv[4]) if len(argv) > 4 else None
    im = cover(trim(key_out(Image.open(argv[0]), gain)), int(argv[2]), int(argv[3]))
    im.save(argv[1])
    print(f"{argv[1]}: {im.size}")


def grid(argv):
    """Slice an NxM layout of cells, key and trim each, emit one sheet."""
    src, dst = argv[0], argv[1]
    cols, rows, cw, ch = (int(argv[2]), int(argv[3]), int(argv[4]), int(argv[5]))
    im = key_out(Image.open(src))
    out = Image.new("RGBA", (cols * cw, rows * ch), (0, 0, 0, 0))
    gw, gh = im.width // cols, im.height // rows
    for r in range(rows):
        for c in range(cols):
            cell = im.crop((c * gw, r * gh, (c + 1) * gw, (r + 1) * gh))
            piece = cover(trim(cell), cw, ch)
            out.paste(piece, (c * cw, r * ch), piece)
    out.save(dst)
    print(f"{dst}: {out.size} sheet, {cols}x{rows} cells of {cw}x{ch}")


def spin(argv):
    """One face-on disc -> horizontal spin strip by squashing it per frame.

    The model cannot lay out four aligned rotation frames, so the spin is
    derived from a single coin: narrowing the disc is what a rotating coin
    looks like, and the frames line up by construction.
    """
    src, dst, cell = argv[0], argv[1], int(argv[2])
    ratios = [1.0, 0.62, 0.16, 0.62]
    disc = cover(trim(key_out(Image.open(src))), cell, cell)
    out = Image.new("RGBA", (cell * len(ratios), cell), (0, 0, 0, 0))
    for i, s in enumerate(ratios):
        w = max(1, round(cell * s))
        frame = disc.resize((w, cell), Image.LANCZOS)
        out.paste(frame, (i * cell + (cell - w) // 2, 0), frame)
    out.save(dst)
    print(f"{dst}: {out.size} spin strip, {len(ratios)} frames of {cell}px")


def texture(argv):
    """Opaque, edge-to-edge surface: no keying, just an exact-size crop."""
    im = cover(Image.open(argv[0]).convert("RGBA"), int(argv[2]), int(argv[3]))
    im.save(argv[1])
    print(f"{argv[1]}: {im.size} texture")


def relight(argv):
    """Apply real surface detail to an existing sheet layout.

    Tile sheets are laid out by the maps the demos load, so the geometry has to
    stay put. Each painted tile's hue is kept and used as a modulation mask over
    an AI-generated material texture, which is what turns a flat block of colour
    into stone, dirt or wood without moving a single cell.

      art_key.py relight sheet.png texture.png out.png [strength]
    """
    sheet = np.asarray(Image.open(argv[0]).convert("RGBA"), dtype=np.float32)
    tex = np.asarray(Image.open(argv[1]).convert("RGB"), dtype=np.float32)
    strength = float(argv[3]) if len(argv) > 3 else 0.75
    rgb, a = sheet[..., :3], sheet[..., 3]
    # Normalised multiply blend. The texture is levelled to the same mean as the
    # painted sheet first, so the painted tile keeps its hue AND its brightness
    # while the texture's grain, seams and chips come through it. Without the
    # level, a dark texture (or transparent margins dragging a mean down)
    # crushes every tile into near-black.
    # Stretch, never centre-crop: a sheet is far wider than it is tall, and a
    # crop would throw away most of the material (a grass texture would lose its
    # green top band and relight everything with soil). Grain survives the
    # distortion; the layout cannot survive losing cells.
    big = Image.fromarray(tex.astype(np.uint8)).resize(
        (sheet.shape[1], sheet.shape[0]), Image.LANCZOS)
    raw = np.asarray(big, dtype=np.float32)
    tv = raw * (160.0 / max(raw.mean(), 1.0))
    # Detail, not hue: the painted sheet already decides that a tile is grass,
    # brick or water, so the material is mostly desaturated before the multiply.
    # Keeping a little of its colour lets a mossy stone sheet warm the tiles that
    # touch it, while a fully saturated source used to override the palette.
    grey = tv @ np.array([0.299, 0.587, 0.114], dtype=np.float32)
    tv = tv * 0.25 + grey[..., None] * 0.75
    tv = np.clip(tv, 0.0, 255.0)
    lit = np.clip(tv * rgb / 160.0, 0.0, 255.0)
    out = np.clip(rgb * (1.0 - strength) + lit * strength, 0.0, 255.0)
    merged = np.dstack([out, a]).astype(np.uint8)
    Image.fromarray(merged).save(argv[2])
    print(f"{argv[2]}: {sheet.shape[1]}x{sheet.shape[0]} relit at {strength}")


COMMANDS = {"sprite": sprite, "grid": grid, "texture": texture, "flat": texture,
            "spin": spin, "relight": relight}


def main():
    if len(sys.argv) < 2 or sys.argv[1] not in COMMANDS:
        print(__doc__)
        raise SystemExit(2)
    argv = sys.argv[2:]
    if len(argv) < (3 if sys.argv[1] in ("spin", "relight") else 4):
        fail("usage: art_key.py <command> in.png out.png <w> [h]")
    COMMANDS[sys.argv[1]](argv)


if __name__ == "__main__":
    main()
