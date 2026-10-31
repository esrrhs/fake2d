#!/usr/bin/env python3
"""Key out a baked-in background speckle layer from a sprite that should be cut out.

Some of the AI-generated art shipped with a mottled wash painted over the whole
canvas instead of transparency, which reads as a grey square behind the
character in game. The wash is not a flat layer — it is scattered speckles of
low alpha (per-sprite, the shipped ones run from ~20 to ~80 out of 255) — so it
cannot be un-composited away with a single matte. Two passes get rid of it: cut
everything at or below the alpha the border actually reaches, then drop whatever
isolated dots survive, since a sprite is one blob.

    ./tools/clean_sprite_matte.py assets/art/ab_pig.png [...]

Rewrites each file in place; git holds the original.

Only safe on art whose subject is opaque. assets/art/goal_flag.png is not: the
flag and its pole are themselves painted at alpha 44, inside the wash's own
alpha range, so the key erases the subject along with the background. That one
needs redrawing, not keying.
"""

import sys

from PIL import Image

# The cut is measured from the art itself: the 99th percentile of the border
# band's alpha, plus headroom, capped so a sprite that is mostly translucent is
# never gutted.
WASH_HEADROOM = 12
WASH_CEILING_CAP = 170
BORDER_BAND = 8
# The key leaves the denser speckles standing as isolated dots. A sprite is one
# subject, so anything that is not part of a blob of at least this many pixels
# is dirt.
MIN_AREA = 48


def wash_ceiling(img):
    w, h = img.size
    a = img.getchannel("A")
    px = a.load()
    vals = []
    for y in range(h):
        edge = y < BORDER_BAND or y >= h - BORDER_BAND
        for x in range(w):
            if edge or x < BORDER_BAND or x >= w - BORDER_BAND:
                vals.append(px[x, y])
    vals.sort()
    p99 = vals[int(len(vals) * 0.99)] if vals else 0
    return min(p99 + WASH_HEADROOM, WASH_CEILING_CAP), p99


def clean(img, ceiling):
    px = img.load()
    removed = 0
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            if px[x, y][3] < ceiling:
                px[x, y] = (0, 0, 0, 0)
                removed += 1
    return removed


def drop_islands(img):
    """Clear every connected blob smaller than MIN_AREA pixels."""
    px = img.load()
    w, h = img.size
    seen = bytearray(w * h)
    dropped = 0
    stack = []
    for start in range(w * h):
        if seen[start] or px[start % w, start // w][3] == 0:
            seen[start] = 1
            continue
        blob = []
        stack.append(start)
        seen[start] = 1
        while stack:
            i = stack.pop()
            blob.append(i)
            x, y = i % w, i // w
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if nx < 0 or ny < 0 or nx >= w or ny >= h:
                    continue
                j = ny * w + nx
                if seen[j]:
                    continue
                seen[j] = 1
                if px[nx, ny][3] != 0:
                    stack.append(j)
        if len(blob) < MIN_AREA:
            for i in blob:
                px[i % w, i // w] = (0, 0, 0, 0)
            dropped += len(blob)
    return dropped


def main(paths):
    for path in paths:
        img = Image.open(path).convert("RGBA")
        before = img.getchannel("A").histogram()[0]
        ceiling, p99 = wash_ceiling(img)
        if p99 < 6:
            print("%s: border is already clear (alpha p99 %d), left alone" %
                  (path, p99))
            continue
        keyed = clean(img, ceiling)
        islands = drop_islands(img)
        after = img.getchannel("A").histogram()[0]
        img.save(path)
        print("%s: cut below alpha %d (border p99 %d), keyed %d px, dropped %d "
              "island px, transparent %d -> %d of %d" %
              (path, ceiling, p99, keyed, islands, before, after,
               img.size[0] * img.size[1]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
