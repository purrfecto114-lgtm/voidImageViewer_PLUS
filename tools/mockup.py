#!/usr/bin/env python3
"""the ui mockup composer: the chrome, drawn from the real numbers.

everything this renders comes out of the tree, not out of taste:

  the palette          sim/theme-tokens.ts (generated from src/viv_theme.c)
  the icons            src/glyphs.c, replayed through the same pipeline
  the strings          src/localization_en_us.h / _zh_cn.h
  the metrics          the layout constants in the c sources
                       (_VIV_TOOLBAR_BAR_HIGH, _VIV_SETTINGS_ROW_HIGH_DESC,
                       the menubar and status metrics, the zoomui cell box)

so the "before" pictures are a reconstruction of what the shipped binary
draws, and the "after" pictures are the same drawing with the redesign
applied. where the two differ, the difference is the design.

usage: python3 tools/mockup/compose.py
"""
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mockup_glyph import Glyphs  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "audit", "previews")

# the render scale: the canvas is drawn at twice the logical size, so a
# preview reads like a 200% dpi display - which is the scale the field
# reports came in at.
SCALE = 2

# the candidates run debian's cjk package name first and the flat
# Noto Sans SC build second, so the renderer works on both hosts.
_FONT_CANDIDATES = (
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/chinese/NotoSansSC-Regular.ttf",
)
_FONT_CANDIDATES_BOLD = (
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/truetype/chinese/NotoSansSC-Bold.ttf",
)


def _font_path(candidates):
    import os as _os
    for cand in candidates:
        if _os.path.exists(cand):
            return cand
    return candidates[0]


FONT_CJK = _font_path(_FONT_CANDIDATES)
FONT_CJK_BOLD = _font_path(_FONT_CANDIDATES_BOLD)

_fonts = {}


def font(size, bold=False):
    key = (size, bold)
    if key not in _fonts:
        path = FONT_CJK_BOLD if bold else FONT_CJK
        _fonts[key] = ImageFont.truetype(path, int(size * SCALE))
    return _fonts[key]


# ---------------------------------------------------------------- theme ---
def load_tokens():
    """the palette, from the generated token file."""
    path = os.path.join(ROOT, "sim", "theme-tokens.ts")
    text = open(path, encoding="utf-8").read()

    def block(name):
        m = re.search(r"export const %s: Record<string, string> = \{(.*?)\n\};" % name, text, re.S)
        out = {}
        for k, v in re.findall(r'"([A-Z0-9_]+)":\s*"([^"]+)"', m.group(1)):
            out[k] = v
        return out

    sysdefs = {}
    m = re.search(r"export const LIGHT_SYS_DEFAULTS: Record<string, string> = \{(.*?)\n\};", text, re.S)
    for k, v in re.findall(r'"([A-Z0-9_]+)":\s*"([^"]+)"', m.group(1)):
        sysdefs[k] = v

    accents = []
    m = re.search(r"export const ACCENTS: Accent\[\] = \[(.*?)\n\];", text, re.S)
    for name, base, hot, down in re.findall(
            r'"name":\s*"([^"]+)",\s*"base":\s*"([^"]+)",\s*"hot":\s*"([^"]+)",\s*"down":\s*"([^"]+)"',
            m.group(1)):
        accents.append((name, base, hot, down))

    return {"dark": block("DARK_TOKENS"), "light": block("LIGHT_TOKENS"),
            "sys": sysdefs, "accents": accents}


TOKENS = load_tokens()


def rgb(hexstr):
    hexstr = hexstr.lstrip("#")
    return tuple(int(hexstr[i:i + 2], 16) for i in (0, 2, 4))


class Theme(object):
    """a resolved palette: token name -> rgb tuple."""

    def __init__(self, mode="dark", accent=0):
        self.mode = mode
        self.accent = accent
        raw = TOKENS[mode]
        self.t = {}
        for k, v in raw.items():
            if v.startswith("sys:"):
                v = TOKENS["sys"].get(v[4:], "#ff00ff")
            self.t[k] = rgb(v)
        name, base, hot, down = TOKENS["accents"][accent]
        self.t["VIV_TK_ACCENT"] = rgb(base)
        self.t["VIV_TK_ACCENT_HOT"] = rgb(hot)
        self.t["VIV_TK_ACCENT_DOWN"] = rgb(down)
        self.accent_name = name

    def __getitem__(self, token):
        return self.t[token]

    def text(self, color, alpha=255):
        return color + (alpha,)


# --------------------------------------------------------- localization ---
def load_strings(lang="en_us"):
    """the string table: id -> text, parsed out of the c header."""
    path = os.path.join(ROOT, "src", "localization_%s.h" % lang)
    text = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r'^\s*("(?:[^"\\]|\\.)*")\s*,\s*//\s*(LOCALIZATION_ID_[A-Z0-9_]+)',
                         text, re.M):
        out[m.group(2)] = _c_string(m.group(1))
    return out


def _c_string(literal):
    """the body of a c string literal, unescaped - without touching the utf-8.

    the naive unicode_escape round trip mangles every non ascii byte (the
    chinese table is utf-8 by contract, the sources compile with /utf-8),
    so the escapes are handled one at a time and everything else is left
    as it is.
    """
    body = literal[1:-1]
    out = []
    i = 0
    while i < len(body):
        ch = body[i]
        if ch == "\\" and i + 1 < len(body):
            nxt = body[i + 1]
            mapping = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'"}
            if nxt in mapping:
                out.append(mapping[nxt])
                i += 2
                continue
            if nxt == "x" and i + 3 < len(body):
                try:
                    out.append(chr(int(body[i + 2:i + 4], 16)))
                    i += 4
                    continue
                except ValueError:
                    pass
            out.append(nxt)
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out)


EN = load_strings("en_us")
ZH = load_strings("zh_cn")


# ---------------------------------------------------------------- canvas ---
class Canvas(object):
    """a scale aware drawing surface."""

    def __init__(self, wide, high, bg):
        self.wide = wide
        self.high = high
        self.img = Image.new("RGBA", (wide * SCALE, high * SCALE), bg + (255,))
        self.d = ImageDraw.Draw(self.img)

    def rect(self, box, fill=None, outline=None, width=1):
        x0, y0, x1, y1 = [v * SCALE for v in box]
        self.d.rectangle((x0, y0, x1, y1), fill=fill, outline=outline,
                         width=max(1, int(width * SCALE)) if outline else 0)

    def line(self, pts, fill, width=1):
        p = [(x * SCALE, y * SCALE) for x, y in pts]
        self.d.line(p, fill=fill, width=max(1, int(width * SCALE)))

    def roundrect(self, box, radius, fill=None, outline=None, width=1):
        x0, y0, x1, y1 = [v * SCALE for v in box]
        r = radius * SCALE
        self.d.rounded_rectangle((x0, y0, x1, y1), radius=r, fill=fill,
                                 outline=outline,
                                 width=max(1, int(width * SCALE)) if outline else 0)

    def text(self, x, y, s, f, fill, anchor="la"):
        self.d.text((x * SCALE, y * SCALE), s, font=f, fill=fill, anchor=anchor)

    def text_size(self, s, f):
        box = self.d.textbbox((0, 0), s, font=f)
        return ((box[2] - box[0]) / SCALE, (box[3] - box[1]) / SCALE)

    def paste(self, img, x, y):
        self.img.alpha_composite(img, (int(x * SCALE), int(y * SCALE)))

    def save(self, name):
        path = os.path.join(OUT, name)
        self.img.convert("RGB").save(path)
        print("wrote %s" % path)
        return path


# ------------------------------------------------------------- the icons ---
GLYPHS = Glyphs()
_icon_cache = {}


def icon(glyph_id, size, dark=True):
    key = (glyph_id, size, dark)
    if key not in _icon_cache:
        _icon_cache[key] = GLYPHS.render(glyph_id, size * SCALE, dark=dark, ss=4)
    return _icon_cache[key]



# ------------------------------------------------------- live metrics ---
# the layout constants, read out of the c sources at render time.
#
# a mockup that carries its own copy of the numbers is a mockup of the
# mockup: the moment the tree moves, the "before" picture starts lying.
# every constant below names the file and the define it comes from, so
# the picture cannot drift away from the code it claims to draw.

def _defines(path):
    """name -> the raw right hand side of every #define in a file."""
    text = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\S+)", text, re.M):
        out[m.group(1)] = m.group(2)
    return out


def _resolve(defs, name, depth=0):
    """a define's value, following one level of token indirection."""
    if name is None or depth > 4:
        return None
    v = defs.get(name)
    if v is None:
        return None
    if v.isdigit():
        return int(v)
    inner = _resolve(defs, v, depth + 1)
    return inner


def live_metrics():
    """the layout constants, from the tree that is checked out right now."""
    src = os.path.join(ROOT, "src")
    tb = _defines(os.path.join(src, "viv_toolbar.c"))
    st = _defines(os.path.join(src, "viv_settings.c"))
    th = _defines(os.path.join(src, "viv_theme.h"))
    zu = _defines(os.path.join(src, "zoomui.c"))
    all_defs = {}
    for d in (tb, st, th, zu):
        all_defs.update(d)

    def pick(d, name, fallback):
        # the first lookup is in the file that owns the define; the
        # indirection is followed across every file, because a token
        # like VIV_UI_PAD lives in viv_theme.h and is referenced by
        # name from viv_toolbar.c.
        v = _resolve(d, name)
        if v is None:
            v = _resolve(all_defs, name)
        return fallback if v is None else v

    m = {
        # viv_toolbar.c
        "toolbar_high": pick(tb, "_VIV_TOOLBAR_BAR_HIGH", 40),
        "toolbar_icon": pick(tb, "_VIV_TOOLBAR_ICON_SIZE", 20),
        "toolbar_gap": pick(tb, "_VIV_TOOLBAR_ICON_TEXT_GAP", 6),
        "toolbar_pad": pick(tb, "_VIV_TOOLBAR_BUTTON_PAD", 12),
        "toolbar_button_gap": pick(tb, "_VIV_TOOLBAR_BUTTON_GAP", 8),
        "toolbar_sep_high": pick(tb, "_VIV_TOOLBAR_SEP_HIGH", 20),
        "toolbar_target": pick(tb, "_VIV_TOOLBAR_TARGET", 44),
        "toolbar_zoom_cell": pick(tb, "_VIV_TOOLBAR_ZOOM_CELL_WIDE", 72),
        "toolbar_radius": pick(tb, "_VIV_TOOLBAR_RADIUS", 4),
        # viv_theme.h
        "ui_radius": pick(th, "VIV_UI_RADIUS", 6),
        "status_high": pick(th, "VIV_UI_STATUS_HIGH", 26),
        # viv_settings.c
        "set_nav_wide": pick(st, "_VIV_SETTINGS_NAV_WIDE", 150),
        "set_nav_item_high": pick(st, "_VIV_SETTINGS_NAV_ITEM_HIGH", 36),
        "set_nav_top": pick(st, "_VIV_SETTINGS_NAV_TOP", 12),
        "set_nav_x": pick(st, "_VIV_SETTINGS_NAV_X", 8),
        "set_content_pad": pick(st, "_VIV_SETTINGS_CONTENT_PAD", 24),
        "set_content_top": pick(st, "_VIV_SETTINGS_CONTENT_TOP", 18),
        "set_section_high": pick(st, "_VIV_SETTINGS_SECTION_HIGH", 30),
        "set_section_air": pick(st, "_VIV_SETTINGS_SECTION_AIR", 6),
        "set_section_rule": pick(st, "_VIV_SETTINGS_SECTION_RULE", 22),
        "set_label_high": pick(st, "_VIV_SETTINGS_LABEL_HIGH", 20),
        "set_desc_mid": pick(st, "_VIV_SETTINGS_DESC_MID", 32),
        "set_label_col": pick(st, "_VIV_SETTINGS_LABEL_COL", 320),
        "set_desc_high": pick(st, "_VIV_SETTINGS_DESC_HIGH", 18),
        "set_row_high": pick(st, "_VIV_SETTINGS_ROW_HIGH", 34),
        "set_row_high_desc": pick(st, "_VIV_SETTINGS_ROW_HIGH_DESC", 52),
        "set_switch_wide": pick(st, "_VIV_SETTINGS_SWITCH_WIDE", 44),
        "set_switch_high": pick(st, "_VIV_SETTINGS_SWITCH_HIGH", 24),
        "set_footer_high": pick(st, "_VIV_SETTINGS_FOOTER_HIGH", 76),
        "set_title_high": pick(st, "_VIV_SETTINGS_TITLE_HIGH", 44),
        # zoomui.c: the cell box and the nine tenths diet
        "pill_scale_num": pick(zu, "_ZOOMUI_PILL_SCALE_NUM", 9),
        "pill_scale_den": pick(zu, "_ZOOMUI_PILL_SCALE_DEN", 10),
        "pill_margin": 6,
        # the one command band: the menus and the commands are siblings at
        # the same y, so the band rides the command strip's own height.
        "menubar_high": pick(tb, "_VIV_TOOLBAR_BAR_HIGH", 48),
    }
    num = m["pill_scale_num"]
    den = m["pill_scale_den"]
    m["pill_cell_w"] = max(29, (48 * num) // den)
    m["pill_cell_h"] = max(25, (44 * num) // den)
    m["pill_gap"] = max(2, (4 * num) // den)
    m["pill_margin"] = (6 * num) // den
    return m


M = live_metrics()

# ------------------------------------------------------------ the scenes ---
# the metrics, read off the c sources. each one names its home.


def photo(w, h, theme):
    """a stand-in photograph for the canvas (device pixels)."""
    w, h = int(w * SCALE), int(h * SCALE)
    img = Image.new("RGB", (w, h), (34, 36, 44))
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / float(h)
        d.line([(0, y), (w, y)], fill=(int(28 + 40 * t), int(34 + 52 * t), int(58 + 74 * t)))
    # a ridge line, so the canvas reads as a picture and not a swatch
    pts = []
    for x in range(0, w + 1, 8):
        t = x / float(w)
        y = h * (0.58 + 0.16 * (1 - t) + 0.05 * ((t * 7) % 1))
        pts.append((x, y))
    d.polygon(pts + [(w, h), (0, h)], fill=(24, 28, 38))
    pts2 = [(x, y - h * 0.18) for x, y in pts]
    d.polygon(pts2 + [(w, h), (0, h)], fill=(30, 36, 48))
    d.ellipse((w * 0.72, h * 0.16, w * 0.79, h * 0.29), fill=(214, 198, 152))
    return img.convert("RGBA")


def current_main(theme, wide=1180, high=720, lang="en"):
    """the chrome as the shipped binary draws it today."""
    S = EN if lang == "en" else ZH
    c = Canvas(wide, high, theme["VIV_TK_FRAME"])

    # --- menu bar
    c.rect((0, 0, wide, M["menubar_high"]), fill=theme["VIV_TK_CHROME"])
    f = font(12)
    x = 10
    for key in ("LOCALIZATION_ID_FILE", "LOCALIZATION_ID_EDIT", "LOCALIZATION_ID_VIEW",
                "LOCALIZATION_ID_SLIDESHOW", "LOCALIZATION_ID_NAVIGATE", "LOCALIZATION_ID_HELP"):
        label = S.get(key, "?").replace("&", "")
        c.text(x, M["menubar_high"] / 2, label, f, theme["VIV_TK_TEXT"], anchor="lm")
        x += c.text_size(label, f)[0] + 16
    menubar_w = x

    # --- the command cluster: the band's right side, one strip with the menus
    ty = 0
    bh = M["menubar_high"]
    c.line([(0, 0), (wide, 0)], theme["VIV_TK_CHROME_LINE"])
    items = [
        ("sep", 0),
        ("btn", 8),
        ("sep", 0),
        ("btn", 0), ("btn", 3), ("btn", 1),
        ("sep", 0),
        ("btn", 5), ("btn", 4),
        ("sep", 0),
        ("zoom", 0),
        ("sep", 0),
        ("btn", 11),
        ("sep", 0),
        ("btn", 12),
    ]
    x = menubar_w + 8
    ic = M["toolbar_icon"]
    tgt = M["toolbar_target"]
    for kind, gid in items:
        if kind == "sep":
            slot = M["toolbar_button_gap"] * 2 + 1
            c.line([(x + slot / 2.0, (bh - M["toolbar_sep_high"]) / 2.0),
                    (x + slot / 2.0, (bh + M["toolbar_sep_high"]) / 2.0)],
                   theme["VIV_TK_CHROME_LINE"])
            x += slot
            continue
        if kind == "zoom":
            # the cluster: the one control with a face - the input capsule,
            # the painted percent between the two handles.
            cw_ = tgt * 2 + M["toolbar_zoom_cell"]
            cy_ = (bh - tgt) / 2.0
            c.roundrect((x, cy_, x + cw_, cy_ + tgt), tgt / 2.0,
                        fill=theme["VIV_TK_INPUT"], outline=theme["VIV_TK_LINE"])
            c.paste(icon(9, ic, dark=theme.mode == "dark"),
                    x + (tgt - ic) / 2.0, cy_ + (tgt - ic) / 2.0)
            c.text(x + tgt + M["toolbar_zoom_cell"] / 2.0, bh / 2.0, "100%",
                   font(12, True), theme["VIV_TK_TEXT"], anchor="mm")
            c.paste(icon(10, ic, dark=theme.mode == "dark"),
                    x + tgt + M["toolbar_zoom_cell"] + (tgt - ic) / 2.0, cy_ + (tgt - ic) / 2.0)
            x += cw_
            continue
        c.paste(icon(gid, ic, dark=theme.mode == "dark"),
                x + (tgt - ic) / 2.0, (bh - ic) / 2.0)
        x += tgt

    # --- canvas
    cy = bh
    ch = high - cy - M["status_high"]
    c.paste(photo(wide, ch, theme), 0, cy)

    # --- status bar: six panes, the last one the date
    sy = high - M["status_high"]
    c.rect((0, sy, wide, high), fill=theme["VIV_TK_CHROME"])
    c.line([(0, sy), (wide, sy)], theme["VIV_TK_CHROME_LINE"])
    sf = font(11)
    # the fact strip: the position, the dimensions and the frame counter -
    # the zoom left for the cluster, the date for the properties dialog,
    # the pixel pair for the chip that follows the cursor.
    panes = ["3 / 27", "5712 x 4284  (24.5 MP)", "Frame 1 / 1"]
    px = 12
    for i, p in enumerate(panes):
        c.text(px, sy + M["status_high"] / 2.0, p, sf, theme["VIV_TK_TEXT2"], anchor="lm")
        px += c.text_size(p, sf)[0] + 26
        if i == 0:
            c.line([(px - 13, sy + 6), (px - 13, sy + M["status_high"] - 6)], theme["VIV_TK_CHROME_LINE"])

    # --- the pixel chip: the hover facts' readout, riding past the cursor
    chip_w, chip_h = 128, 56
    chx, chy = wide * 0.55, high * 0.52
    c.roundrect((chx, chy, chx + chip_w, chy + chip_h), 12,
                fill=theme["VIV_TK_NAV_FACE"], outline=theme["VIV_TK_LINE"])
    c.text(chx + 10, chy + chip_h * 0.28, "POS 5712, 284", font(11),
           theme["VIV_TK_TEXT"], anchor="lm")
    c.text(chx + 10, chy + chip_h * 0.72, "RGB 222, 116, 84", font(11),
           theme["VIV_TK_TEXT"], anchor="lm")

    # --- the floating pill: the transport and the zoom, over the canvas
    cells = [("g", 0), ("g", 3), ("g", 1), ("sep",), ("g", 9), ("pct", "100%"), ("g", 10)]
    cw, chh = M["pill_cell_w"], M["pill_cell_h"]
    gap = M["pill_gap"]
    row_w = sum(cw if c[0] != "sep" else 1 for c in cells) + gap * (len(cells) - 1)
    pw = row_w + M["pill_margin"] * 2
    ph = chh + M["pill_margin"] * 2
    px = wide - pw - 24
    py = cy + ch - ph - 24
    c.roundrect((px, py, px + pw, py + ph), ph / 2.0, fill=theme["VIV_TK_FACE"],
                outline=theme["VIV_TK_LINE"])
    cx = px + M["pill_margin"]
    for cell in cells:
        if cell[0] == "g":
            c.paste(icon(cell[1], 20, dark=theme.mode == "dark"),
                    cx + (cw - 20) / 2.0, py + M["pill_margin"] + (chh - 20) / 2.0)
            cx += cw + gap
        elif cell[0] == "pct":
            c.text(cx + cw / 2.0, py + M["pill_margin"] + chh / 2.0, cell[1], font(12, True),
                   theme["VIV_TK_TEXT"], anchor="mm")
            cx += cw + gap
        else:
            c.line([(cx, py + M["pill_margin"] + 8), (cx, py + M["pill_margin"] + chh - 8)],
                   theme["VIV_TK_LINE"])
            cx += 1 + gap
    return c


# ---------------------------------------------------------- the redesign ---
# "quiet chrome": two bands become one, every fact gets exactly one
# persistent home, and the separators speak in the icon's own height.
R = {
    "cmd_high": 48,          # one band, in place of 22 + 40
    "menu_pad": 14,
    "cmd_icon": 20,
    "cmd_label_gap": 8,
    "cmd_pad": 10,
    "cmd_gap": 6,
    "group_gap": 18,         # the air around a group divider
    "sep_high": 20,          # the separator speaks in the icon's height
    "sep_slot": 13,
    "radius": 6,
    "fact_high": 26,         # the fact strip
    "zoom_cluster_h": 32,
}


def redesign_main(theme, wide=1180, high=720, lang="en"):
    """the redesign, drawn - which is the shipped tree since the round
    landed. the model used to carry its own aspirational chrome (the
    right-aligned cluster, the count pane); the compare previews pin the
    landing now, so both sides draw the same code-honest picture."""
    return current_main(theme, wide=wide, high=high, lang=lang)

S = {
    "win_w": 900, "win_h": 660,
    "nav_w": 150, "nav_item": 36, "nav_top": 12, "nav_x": 8,
    "title_h": 44, "content_top": 18, "content_pad": 24,
    "section_h": 30, "row_h": 34, "row_h_desc": 52, "desc_h": 18,
    "switch_w": 44, "switch_h": 24, "footer_h": 76,
    "value_w": 180,
    # the redesign
    "r_label_col": 320,   # one measure for every label and every description
    "r_row_h": 36, "r_row_h_desc": 56, "r_label_line": 20,
    "r_desc_off": 32,     # the description line, measured from the row top
}
# the landing: the redesign columns equal the shipped ones, so the sheet
# reads the c's own constants for both sides - the before picture cannot
# drift away from the code it claims to draw.
S["section_h"] = M["set_section_high"]
S["row_h"] = M["set_row_high"]
S["row_h_desc"] = M["set_row_high_desc"]
S["desc_h"] = M["set_desc_high"]
S["label_high"] = M["set_label_high"]
S["desc_mid"] = M["set_desc_mid"]
S["label_col"] = M["set_label_col"]
S["section_air"] = M["set_section_air"]
S["section_rule"] = M["set_section_rule"]
S["switch_w"] = M["set_switch_wide"]
S["switch_h"] = M["set_switch_high"]


def switch_widget(c, x, y, w, h, on, theme):
    """the app's own switch: a 44x24 pill with a 20 thumb."""
    track = theme["VIV_TK_ACCENT"] if on else theme["VIV_TK_INPUT"]
    c.roundrect((x, y, x + w, y + h), h / 2.0, fill=track,
                outline=theme["VIV_TK_LINE"] if not on else None)
    r = h - 6
    tx = x + w - r - 3 if on else x + 3
    c.roundrect((tx, y + 3, tx + r, y + 3 + r), r / 2.0,
                fill=theme["VIV_TK_ON_ACCENT"] if on else theme["VIV_TK_TEXT2"])


def settings_page(theme, redesign=False, lang="en"):
    """the general page of the settings window, before and after."""
    T = EN if lang == "en" else ZH
    dark = theme.mode == "dark"
    w, h = S["win_w"], S["win_h"]
    c = Canvas(w, h, theme["VIV_TK_FACE"])

    f = font(12)
    fsmall = font(11)
    fbold = font(12, True)

    row_h = S["r_row_h"] if redesign else S["row_h"]
    row_h_desc = S["r_row_h_desc"] if redesign else S["row_h_desc"]
    label_col = S["r_label_col"] if redesign else 0

    # --- the title band
    c.rect((0, 0, w, S["title_h"]), fill=theme["VIV_TK_FACE"])
    c.text(24, S["title_h"] / 2.0, "Settings", fbold, theme["VIV_TK_TEXT"], anchor="lm")
    c.line([(0, S["title_h"] - 1), (w, S["title_h"] - 1)], theme["VIV_TK_LINE"])

    # --- the nav
    c.rect((0, S["title_h"], S["nav_w"], h), fill=theme["VIV_TK_NAV_FACE"])
    c.line([(S["nav_w"] - 1, S["title_h"]), (S["nav_w"] - 1, h)], theme["VIV_TK_LINE"])
    pages = [("LOCALIZATION_ID_SETTINGS_PAGE_GENERAL", 13),
             ("LOCALIZATION_ID_SETTINGS_PAGE_VIEW", 14),
             ("LOCALIZATION_ID_SETTINGS_PAGE_CONTROLS", 15)]
    for i, (key, gid) in enumerate(pages):
        iy = S["title_h"] + S["nav_top"] + i * S["nav_item"]
        if i == 0:
            c.roundrect((S["nav_x"], iy, S["nav_x"] + S["nav_w"] - 20, iy + S["nav_item"]),
                        6, fill=theme["VIV_TK_HOVER"])
        c.paste(icon(gid, 20, dark=dark), S["nav_x"] + 12, iy + (S["nav_item"] - 20) / 2.0)
        c.text(S["nav_x"] + 44, iy + S["nav_item"] / 2.0, T.get(key, "?"), f,
               theme["VIV_TK_TEXT"] if i == 0 else theme["VIV_TK_TEXT2"], anchor="lm")

    # --- the content column
    cx = S["nav_w"] + S["content_pad"]
    cy = S["title_h"] + S["content_top"]
    cw = w - cx - S["content_pad"]
    switch_x = cx + cw - S["switch_w"]

    def section(y, key):
        # the caption sits on the label line, the hairline hangs under it,
        # the air follows - the shipped rule, from the c's own constants.
        c.text(cx, y + S["label_high"] / 2.0, T.get(key, key), fbold, theme["VIV_TK_TEXT"], anchor="lm")
        c.line([(cx, y + S["section_rule"]), (cx + cw, y + S["section_rule"])], theme["VIV_TK_LINE"])
        return y + S["section_h"] + S["section_air"]

    def row_switch(y, rh, label, desc, on):
        """one switch row. the alignment rule is the whole point.

        the shipped rule (viv_settings.c:4994 and :927): a row with a
        description is 52 dip tall, the label is centred in the row's
        TOP half (13 dip from the top) and the switch is centred in the
        WHOLE row (26 dip from the top). the control therefore sits
        13 dip below the label it belongs to - 39 px at the 300% dpi
        the field report came in at.

        the redesigned rule: the control rides the label line, so the
        two are one object again, and the description hangs under the
        label inside a fixed measure.
        """
        # the shipped rule IS the redesign rule now (the 13 dip closed):
        # the control rides the label line, the description hangs on its
        # own line, both read from the c's live constants.
        if desc:
            label_cy = y + S["label_high"] / 2.0
            ctl_cy = label_cy
            c.text(cx, label_cy, T.get(label, label), f, theme["VIV_TK_TEXT"], anchor="lm")
            c.text(cx, y + S["desc_mid"], T.get(desc, desc), fsmall,
                   theme["VIV_TK_TEXT2"], anchor="lm")
            switch_widget(c, switch_x, ctl_cy - S["switch_h"] / 2.0,
                          S["switch_w"], S["switch_h"], on, theme)
        else:
            ctl_cy = y + rh / 2.0
            c.text(cx, ctl_cy, T.get(label, label), f, theme["VIV_TK_TEXT"], anchor="lm")
            switch_widget(c, switch_x, ctl_cy - S["switch_h"] / 2.0,
                          S["switch_w"], S["switch_h"], on, theme)
        return y + rh

    def row_value(y, rh, label, value):
        c.text(cx, y + rh / 2.0, T.get(label, label), f, theme["VIV_TK_TEXT"], anchor="lm")
        vw = 150
        c.roundrect((cx + cw - vw, y + (rh - 28) / 2.0, cx + cw, y + (rh - 28) / 2.0 + 28),
                    6, fill=theme["VIV_TK_INPUT"], outline=theme["VIV_TK_LINE"])
        c.text(cx + cw - vw + 12, y + rh / 2.0, value, f, theme["VIV_TK_TEXT"], anchor="lm")
        c.text(cx + cw - vw - 10, y + rh / 2.0, "▾", f, theme["VIV_TK_TEXT2"], anchor="rm")
        return y + rh

    # --- the interface section
    y = section(cy, T.get("LOCALIZATION_ID_SETTINGS_SECTION_INTERFACE", "Interface"))
    y = row_value(y, row_h, "LOCALIZATION_ID_SETTINGS_LANGUAGE", "English")
    y = row_value(y, row_h, "LOCALIZATION_ID_SETTINGS_THEME", "Dark")
    y = row_value(y, row_h, "LOCALIZATION_ID_ACCENT_COLOR", "Azure")
    y = row_switch(y, row_h, "LOCALIZATION_ID_SETTINGS_TOOLBAR_ICON_ONLY", None, False)
    y = row_switch(y, row_h, "LOCALIZATION_ID_SETTINGS_FLOATING_BAR", None, True)
    y = row_switch(y, row_h, "LOCALIZATION_ID_SETTINGS_AUTO_HIDE_BAR", None, True)
    y = row_switch(y, row_h, "LOCALIZATION_ID_SETTINGS_PIXEL_INFO", None, True)
    y = row_switch(y, row_h, "LOCALIZATION_ID_SETTINGS_HARDWARE_ACCELERATION",
                   "LOCALIZATION_ID_SETTINGS_HARDWARE_ACCELERATION_DESC", True)
    y += 10

    # --- the system integration section (the two line row lives here)
    y = section(y, T.get("LOCALIZATION_ID_SETTINGS_SECTION_STARTUP", "System integration"))
    y = row_switch(y, row_h_desc, "LOCALIZATION_ID_SETTINGS_ALLOW_MULTIPLE",
                   "LOCALIZATION_ID_SETTINGS_ALLOW_MULTIPLE_DESC", True)
    y = row_switch(y, row_h, "LOCALIZATION_ID_SETTINGS_RESUME_LAST", None, False)
    y = row_switch(y, row_h, "LOCALIZATION_ID_STARTMENU_SHORTCUTS", None, False)
    y = row_switch(y, row_h, "LOCALIZATION_ID_STORE_SETTINGS_APPDATA", None, False)

    # --- the footer
    fy = h - S["footer_h"]
    c.rect((0, fy, w, h), fill=theme["VIV_TK_FACE"])
    c.line([(0, fy), (w, fy)], theme["VIV_TK_LINE"])
    bw, bh = 140, 32
    bx = w - S["content_pad"] - bw
    for label, primary in (("LOCALIZATION_ID_SETTINGS_CANCEL", False),
                           ("LOCALIZATION_ID_SETTINGS_OK", True)):
        by = fy + (S["footer_h"] - bh) / 2.0
        if primary:
            c.roundrect((bx, by, bx + bw, by + bh), 6, fill=theme["VIV_TK_ACCENT"])
            col = theme["VIV_TK_ON_ACCENT"]
        else:
            c.roundrect((bx, by, bx + bw, by + bh), 6, fill=theme["VIV_TK_INPUT"],
                        outline=theme["VIV_TK_LINE"])
            col = theme["VIV_TK_TEXT"]
        c.text(bx + bw / 2.0, by + bh / 2.0, T.get(label, label), f, col, anchor="mm")
        bx -= bw + 10
    return c


# ------------------------------------------------- the annotated detail ---
def row_detail(theme, redesign=False, lang="en"):
    """the two line row, drawn large, with the geometry called out."""
    T = EN if lang == "en" else ZH
    w, h = 760, 150
    c = Canvas(w, h, theme["VIV_TK_FACE"])
    f = font(12)
    fsmall = font(11)
    fbig = font(13, True)

    cx = 40
    rh = S["r_row_h_desc"] if redesign else S["row_h_desc"]
    y = 54

    # the row's own band
    c.rect((cx - 16, y, w - 40, y + rh), fill=theme["VIV_TK_HOVER"])

    # both branches draw the shipped rule now (the 13 dip is closed);
    # the pair stays so the sheet can prove the alignment twice.
    label_cy = y + S["label_high"] / 2.0
    ctl_cy = label_cy
    desc_cy = y + S["desc_mid"]

    c.text(cx, label_cy, T.get("LOCALIZATION_ID_SETTINGS_ALLOW_MULTIPLE", ""), f,
           theme["VIV_TK_TEXT"], anchor="lm")
    c.text(cx, desc_cy, T.get("LOCALIZATION_ID_SETTINGS_ALLOW_MULTIPLE_DESC", ""), fsmall,
           theme["VIV_TK_TEXT2"], anchor="lm")
    switch_widget(c, w - 40 - S["switch_w"], ctl_cy - S["switch_h"] / 2.0,
                  S["switch_w"], S["switch_h"], True, theme)

    # the two centres and the gap between them
    c.line([(cx, label_cy), (w - 40 - S["switch_w"] - 8, label_cy)], (255, 120, 120, 170))
    c.line([(cx, ctl_cy), (w - 40 - S["switch_w"] - 8, ctl_cy)], (120, 200, 255, 170))
    gap = abs(ctl_cy - label_cy)

    c.text(cx, 30, ("the redesigned row" if redesign else "the row as it draws today"),
           fbig, theme["VIV_TK_TEXT"], anchor="lm")
    note = "the control rides the label line: 0 dip apart (the shipped rule)"
    c.text(cx, 46, note, fsmall, theme["VIV_TK_TEXT2"], anchor="lm")

    # the measurement
    mid = (label_cy + ctl_cy) / 2.0
    bx = w - 40 - S["switch_w"] - 24
    if gap > 0.5:
        c.line([(bx, label_cy), (bx, ctl_cy)], (255, 120, 120, 255))
        for yy in (label_cy, ctl_cy):
            c.line([(bx - 5, yy), (bx + 5, yy)], (255, 120, 120, 255))
        c.text(bx - 10, mid - 10, "%.0f dip" % gap, font(13, True),
               (255, 150, 150, 255), anchor="rm")
        c.text(bx - 10, mid + 12, "= 39 px at 300% dpi", fsmall,
               (255, 150, 150, 255), anchor="rm")
    else:
        c.line([(bx - 5, ctl_cy), (bx + 5, ctl_cy)], (130, 215, 255, 255))
        c.text(bx - 10, ctl_cy, "0 dip", font(13, True),
               (140, 220, 255, 255), anchor="rm")
    return c


# ------------------------------------------------------------- the sheet ---
def side_by_side(a, b, gap=24, bg=(20, 20, 23)):
    """two canvases next to each other, on one sheet."""
    w = a.img.width + gap + b.img.width
    h = max(a.img.height, b.img.height)
    out = Image.new("RGB", (w, h), bg)
    out.paste(a.img.convert("RGB"), (0, 0))
    out.paste(b.img.convert("RGB"), (a.img.width + gap, 0))
    return out


def main():
    if not os.path.isdir(OUT):
        os.makedirs(OUT)

    dark = Theme("dark")
    light = Theme("light")

    current_main(dark).save("01-current-main-dark.png")
    redesign_main(dark).save("02-redesign-main-dark.png")
    settings_page(dark, redesign=False).save("03-current-settings-dark.png")
    settings_page(dark, redesign=True).save("04-redesign-settings-dark.png")

    a = row_detail(dark, redesign=False)
    b = row_detail(dark, redesign=True)
    sheet = Image.new("RGB", (a.img.width, a.img.height + b.img.height + 24), (20, 20, 23))
    sheet.paste(a.img.convert("RGB"), (0, 0))
    sheet.paste(b.img.convert("RGB"), (0, a.img.height + 24))
    sheet.save(os.path.join(OUT, "05-row-alignment-detail.png"))
    print("wrote output/05-row-alignment-detail.png")

    redesign_main(light).save("06-redesign-main-light.png")
    settings_page(light, redesign=True).save("07-redesign-settings-light.png")
    redesign_main(dark, lang="zh").save("08-redesign-main-zh.png")

    cur = current_main(dark)
    red = redesign_main(dark)
    side_by_side(cur, red).save(os.path.join(OUT, "09-compare-main.png"))
    print("wrote output/09-compare-main.png")

    cur_s = settings_page(dark, redesign=False)
    red_s = settings_page(dark, redesign=True)
    side_by_side(cur_s, red_s).save(os.path.join(OUT, "10-compare-settings.png"))
    print("wrote output/10-compare-settings.png")


if __name__ == "__main__":
    main()
