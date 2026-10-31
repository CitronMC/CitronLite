#!/usr/bin/env python3
"""Rasterise the interface icons into include/citron/icons_data.h, one alpha atlas.

    python tools/make_icons.py path/to/lucide-react/dist/esm/icons

The icons are Lucide (ISC licence, assets/icons/LICENSE-lucide.txt): a 24 unit box, 2 unit strokes
with round caps and joins. Each stroke is swept as a disc on a canvas eight times larger, filtered
down to a cell, and packed with gutters wide enough that mipmapped sampling never bleeds between
icons. The window tints the white coverage with any colour and draws it at any size.

Build time only: needs Pillow and numpy, and the generated header is committed. The order of ICONS
is the order of ui::Icon in src/ui/draw.h.
"""
import math
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "include", "citron", "icons_data.h")

# (name, Lucide file, stroke width in box units)
ICONS = [
    ("Close", "x", 2.0),
    ("Minimize", "minus", 2.0),
    ("Keyboard", "keyboard", 2.0),
    ("Check", "check", 3.0),            # small on a lime box, so heavier
    ("ChevronLeft", "chevron-left", 2.0),
    ("ChevronRight", "chevron-right", 2.0),
    ("Pencil", "pencil", 2.0),
    ("Trash", "trash-2", 2.0),
    ("ChevronDown", "chevron-down", 2.0),
]

CELL = 48          # px an icon in the atlas
GUTTER = 12        # px of empty space round every cell
SS = 8             # the canvas, times the cell
COLS = 4


def parse_icon(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    body = text[text.index("createLucideIcon("):]
    elems = []
    for tag, attrs in re.findall(r'"(\w+)",\s*\{([^}]*)\}', body):
        elems.append((tag, dict(re.findall(r'(\w+):\s*"([^"]*)"', attrs))))
    return elems


def arc_points(x1, y1, rx, ry, phi, large, sweep, x2, y2):
    """An SVG elliptical arc in endpoint form, as points, by the SVG implementation notes."""
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    rx, ry = abs(rx), abs(ry)
    cp, sp = math.cos(math.radians(phi)), math.sin(math.radians(phi))
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)

    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    n = max(8, int(abs(dt) * max(rx, ry) * 4))
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return pts


def cubic(p0, p1, p2, p3, n=24):
    out = []
    for k in range(1, n + 1):
        t = k / n
        u = 1 - t
        out.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return out


def path_subpaths(d):
    toks = re.findall(r"[MmLlHhVvCcSsAaZz]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?", d)
    i = 0
    subs, pts = [], []
    cur = start = (0.0, 0.0)
    cmd = None
    last_ctrl = None

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v

    while i < len(toks):
        t = toks[i]
        if re.match(r"[A-Za-z]", t):
            cmd = t
            i += 1
            if cmd in "Zz":
                if pts:
                    subs.append((pts, True))
                pts = []
                cur = start
                continue
        rel = cmd.islower()
        c = cmd.upper()
        if c == "M":
            x, y = num(), num()
            if rel:
                x, y = x + cur[0], y + cur[1]
            if pts:
                subs.append((pts, False))
            cur = start = (x, y)
            pts = [cur]
            cmd = "l" if rel else "L"
        elif c == "L":
            x, y = num(), num()
            cur = (x + cur[0], y + cur[1]) if rel else (x, y)
            pts.append(cur)
        elif c == "H":
            x = num()
            cur = (x + cur[0] if rel else x, cur[1])
            pts.append(cur)
        elif c == "V":
            y = num()
            cur = (cur[0], y + cur[1] if rel else y)
            pts.append(cur)
        elif c == "C":
            a = [num() for _ in range(6)]
            if rel:
                a = [a[k] + cur[k % 2] for k in range(6)]
            pts += cubic(cur, (a[0], a[1]), (a[2], a[3]), (a[4], a[5]))
            last_ctrl = (a[2], a[3])
            cur = (a[4], a[5])
        elif c == "S":
            a = [num() for _ in range(4)]
            if rel:
                a = [a[k] + cur[k % 2] for k in range(4)]
            c1 = (2 * cur[0] - last_ctrl[0], 2 * cur[1] - last_ctrl[1]) if last_ctrl else cur
            pts += cubic(cur, c1, (a[0], a[1]), (a[2], a[3]))
            last_ctrl = (a[0], a[1])
            cur = (a[2], a[3])
        elif c == "A":
            rx, ry, phi = num(), num(), num()
            large, sweep = int(num()), int(num())
            x, y = num(), num()
            if rel:
                x, y = x + cur[0], y + cur[1]
            pts += arc_points(cur[0], cur[1], rx, ry, phi, large, sweep, x, y)
            cur = (x, y)
        else:
            raise ValueError("unsupported path command " + cmd)
        if c not in "CS":
            last_ctrl = None
    if pts:
        subs.append((pts, False))
    return subs


def element_subpaths(tag, a):
    f = lambda k, d=0.0: float(a.get(k, d))
    if tag == "path":
        return path_subpaths(a["d"])
    if tag == "line":
        return [([(f("x1"), f("y1")), (f("x2"), f("y2"))], False)]
    if tag == "rect":
        x, y, w, h = f("x"), f("y"), f("width"), f("height")
        r = min(f("rx", f("ry")), w / 2, h / 2)
        pts = []
        for cx, cy, a0 in [(x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)]:
            for k in range(13):
                t = math.radians(a0 + 90 * k / 12)
                pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
        return [(pts, True)]
    raise ValueError("unsupported element " + tag)


def rasterise(elems, stroke):
    size = CELL * SS
    k = size / 24.0
    img = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(img)
    rad = stroke * k / 2
    for tag, a in elems:
        for pts, closed in element_subpaths(tag, a):
            p = [(x * k, y * k) for x, y in pts]
            if closed:
                p = p + [p[0]]
            for (x0, y0), (x1, y1) in zip(p, p[1:] if len(p) > 1 else p):
                steps = max(1, int(math.hypot(x1 - x0, y1 - y0) / (rad * 0.25)))
                for s in range(steps + 1):
                    t = s / steps
                    x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
                    draw.ellipse((x - rad, y - rad, x + rad, y + rad), fill=255)
    a = np.asarray(img, dtype=np.float32).reshape(CELL, SS, CELL, SS).mean(axis=(1, 3))
    return np.clip(a + 0.5, 0, 255).astype(np.uint8)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    stride = CELL + 2 * GUTTER
    rows = (len(ICONS) + COLS - 1) // COLS
    atlas = np.zeros((rows * stride, COLS * stride), dtype=np.uint8)
    for n, (name, source, stroke) in enumerate(ICONS):
        cell = rasterise(parse_icon(os.path.join(sys.argv[1], source + ".js")), stroke)
        r, c = divmod(n, COLS)
        atlas[r * stride + GUTTER:r * stride + GUTTER + CELL, c * stride + GUTTER:c * stride + GUTTER + CELL] = cell
    h, w = atlas.shape
    data = atlas.tobytes()
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Generated by tools/make_icons.py from Lucide (ISC, assets/icons/LICENSE-lucide.txt).\n")
        f.write("// Alpha coverage, a byte a pixel. Icon n is in cell (n % kIconCols, n / kIconCols), each cell\n")
        f.write("// kIconCell px with kIconGutter px clear on every side.\n")
        f.write("#pragma once\n\nnamespace citron {\n")
        f.write("inline constexpr int kIconAtlasW = %d, kIconAtlasH = %d;\n" % (w, h))
        f.write("inline constexpr int kIconCell = %d, kIconGutter = %d, kIconCols = %d, kIconCount = %d;\n" % (CELL, GUTTER, COLS, len(ICONS)))
        f.write("inline constexpr unsigned char kIconAtlasAlpha[] = {\n")
        for j in range(0, len(data), 48):
            f.write("    " + ",".join(str(b) for b in data[j:j + 48]) + ",\n")
        f.write("};\n}  // namespace citron\n")
    print("wrote %s: %d x %d, %d icons" % (OUT, w, h, len(ICONS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
