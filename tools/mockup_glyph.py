#!/usr/bin/env python3
"""renders the viewer's own icon set from src/glyphs.c.

the glyphs are vector geometry on a 48x48 design grid, drawn by gdi+
through a flat api with a round capped pen. this module parses that
geometry straight out of the c source and replays the same pipeline in
pil: the same scale (size / 48), the same pen width floor (1.25 device
pixels), the same round caps, the same arc and ellipse primitives.

the point is fidelity: a preview that draws a different icon is a
preview of a different app. replaying the real geometry keeps the
mockup honest about what the shipped binary draws.

the gdi+ angle convention is reproduced too: degrees clockwise from the
positive x axis in the y-down grid, so 0 is three o'clock, 90 is the
bottom and 270 is the top.
"""
import math
import os
import re

from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
GLYPHS_C = os.path.join(ROOT, "src", "glyphs.c")
GLYPHS_H = os.path.join(ROOT, "src", "glyphs.h")

# the theme stroke colors, straight out of glyphs.c.
COLOR_DARK = (0xE8, 0xE8, 0xE8, 255)
COLOR_LIGHT = (0x3C, 0x40, 0x43, 255)

DESIGN_GRID = 48.0


def _strip_c_comments(text):
    """blank out the c comments, keeping every byte offset intact.

    the names this module looks up also appear in prose ("the magnifier
    ring ..."), and a name found in a comment would resolve to the wrong
    declaration.
    """
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(" " * (j - i))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def _parse_points(text):
    """a _glyphs_point_t array: {x,y} pairs."""
    pairs = re.findall(r"\{\s*(-?\d+)\s*,\s*(-?\d+)\s*\}", text)
    return [(int(a), int(b)) for a, b in pairs]


def _parse_floats(text):
    nums = re.findall(r"-?\d+(?:\.\d+)?f?", text)
    return [float(n.rstrip("f")) for n in nums]


class Glyphs(object):
    def __init__(self, path=GLYPHS_C):
        self.text = _strip_c_comments(open(path, encoding="utf-8", errors="replace").read())
        self.ids = self._parse_ids()
        self.strokes = self._parse_strokes()
        self.table = self._parse_table()

    # ---- source parsing -------------------------------------------------
    def _parse_ids(self):
        head = _strip_c_comments(open(GLYPHS_H, encoding="utf-8", errors="replace").read())
        out = {}
        for m in re.finditer(r"#define\s+(GLYPH_[A-Z0-9_]+)\s+(\d+)", head):
            name = m.group(1)
            if name == "GLYPH_COUNT":
                continue
            out[name] = int(m.group(2))
        return out

    def _arr(self, name):
        """the initializer block of a named static.

        the point lists are arrays (name[] = {...}), the ellipse and arc
        descriptors are scalars (name = {...}), some of them sit on one
        line and some span three. a brace matching scan answers all
        four shapes; a regex over the closing brace does not, because
        the single line spellings would run on to the next declaration.
        """
        m = re.search(re.escape(name) + r"\s*(?:\[\])?\s*=\s*", self.text)
        if not m:
            return ""
        i = m.end()
        while i < len(self.text) and self.text[i] in " \t\r\n":
            i += 1
        if i >= len(self.text) or self.text[i] != "{":
            return ""
        depth = 0
        start = i
        while i < len(self.text):
            if self.text[i] == "{":
                depth += 1
            elif self.text[i] == "}":
                depth -= 1
                if depth == 0:
                    return self.text[start + 1:i]
            i += 1
        return ""

    def _parse_strokes(self):
        """name -> list of strokes.

        a stroke is a dict: {'kind': 'poly'|'ellipse'|'arc', 'width': int,
        'points': [...], 'cx','cy','rx','ry','start','sweep'}.
        """
        out = {}
        for m in re.finditer(r"static\s+const\s+_glyphs_stroke_t\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\n\};", self.text, re.S):
            name, body = m.group(1), m.group(2)
            strokes = []
            for entry in re.finditer(r"\{([^{}]*)\}", body):
                fields = [f.strip() for f in entry.group(1).split(",") if f.strip() != ""]
                if len(fields) < 2:
                    continue
                point_count = int(fields[0])
                width = int(fields[1])
                rest = fields[2:]
                stroke = {"kind": "poly", "width": width, "points": []}
                if point_count == 0:
                    ref = None
                    for f in rest:
                        if f.startswith("&"):
                            ref = f[1:]
                    if ref is None:
                        continue
                    vals = _parse_floats(self._arr(ref))
                    if "arc" in ref:
                        stroke["kind"] = "arc"
                        stroke["cx"], stroke["cy"], stroke["rx"], stroke["ry"] = vals[:4]
                        stroke["start"], stroke["sweep"] = vals[4], vals[5]
                    else:
                        stroke["kind"] = "ellipse"
                        stroke["cx"], stroke["cy"], stroke["rx"], stroke["ry"] = vals[:4]
                else:
                    stroke["kind"] = "poly"
                    stroke["points"] = _parse_points(self._arr(rest[0]))
                strokes.append(stroke)
            out[name] = strokes
        return out

    def _parse_table(self):
        """the _glyphs_table: one entry per glyph id, in id order."""
        m = re.search(r"_glyphs_table\[GLYPH_COUNT\]\s*=\s*\{(.*?)\n\};", self.text, re.S)
        if not m:
            return []
        entries = []
        for entry in re.finditer(r"\{\s*(\d+)\s*,\s*(\w+)\s*\}", m.group(1)):
            entries.append((int(entry.group(1)), entry.group(2)))
        return entries

    def glyph(self, glyph_id):
        """the stroke list for a glyph id."""
        if glyph_id >= len(self.table):
            raise KeyError("glyph id %d is past the table" % glyph_id)
        count, name = self.table[glyph_id]
        strokes = self.strokes.get(name, [])
        return strokes[:count] if count <= len(strokes) else strokes

    # ---- rendering ------------------------------------------------------
    def render(self, glyph_id, size, dark=True, ss=4):
        """render one glyph to an RGBA image of size x size."""
        img = Image.new("RGBA", (size * ss, size * ss), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        color = COLOR_DARK if dark else COLOR_LIGHT
        scale = (size * ss) / DESIGN_GRID

        for stroke in self.glyph(glyph_id):
            pen_w = max(stroke["width"] * scale, 1.25)
            kind = stroke["kind"]
            if kind == "poly":
                pts = [(x * scale, y * scale) for x, y in stroke["points"]]
                if len(pts) >= 2:
                    self._stroke(draw, pts, pen_w, color)
            elif kind == "ellipse":
                cx, cy, rx, ry = (stroke[k] * scale for k in ("cx", "cy", "rx", "ry"))
                draw.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), outline=color,
                             width=max(1, int(round(pen_w))))
            elif kind == "arc":
                cx, cy, rx, ry = (stroke[k] * scale for k in ("cx", "cy", "rx", "ry"))
                pts = self._arc_points((cx - rx, cy - ry, cx + rx, cy + ry),
                                       stroke["start"], stroke["sweep"])
                self._stroke(draw, pts, pen_w, color)

        return img.resize((size, size), Image.LANCZOS)

    @staticmethod
    def _arc_points(box, start_deg, sweep_deg):
        """sample a gdi+ arc: the bounding box, clockwise from the x axis."""
        x0, y0, x1, y1 = box
        cx = (x0 + x1) / 2.0
        cy = (y0 + y1) / 2.0
        rx = (x1 - x0) / 2.0
        ry = (y1 - y0) / 2.0
        steps = max(8, int(abs(sweep_deg) / 3))
        pts = []
        for i in range(steps + 1):
            t = math.radians(start_deg + sweep_deg * (i / float(steps)))
            pts.append((cx + rx * math.cos(t), cy + ry * math.sin(t)))
        return pts

    @staticmethod
    def _stroke(draw, pts, pen_w, color):
        """a round capped polyline: the caps are the signature of the family."""
        w = max(1, int(round(pen_w)))
        r = w / 2.0
        draw.line(pts, fill=color, width=w, joint="curve")
        for (x, y) in pts:
            draw.ellipse((x - r, y - r, x + r, y + r), fill=color)


if __name__ == "__main__":
    g = Glyphs()
    names = sorted(g.ids.items(), key=lambda kv: kv[1])
    sheet = Image.new("RGBA", (72 * len(names), 140), (32, 32, 36, 255))
    for name, gid in names:
        sheet.alpha_composite(g.render(gid, 64, dark=True), (gid * 72 + 4, 8))
    out = os.path.join(ROOT, "audit", "previews", "glyph-sheet.png")
    sheet.save(out)
    print("wrote %s  (%d glyphs)" % (out, len(names)))
