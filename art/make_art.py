#!/usr/bin/env python3
"""Draws the SurgiQ screen's artwork into this folder (needs Pillow). Run from the repo root:
    docker run --rm -v <repo>:/r -w /r python:3.11-slim sh -c \
      "pip install -q --target /tmp/p pillow; PYTHONPATH=/tmp/p python3 ports/peq/art/make_art.py"
It also rewrites ../layout.conf and writes geometry.json (for ../skin_post.py), so the sizes here, the layout's coordinates
and the post-build step can't drift apart. One look since 0.12.0: white, with purple shimmer accents (C below).

Two tabs: EQ (the graph, the analyser and the band controls) and FX (reverb, delay, chorus, shimmer, and the analyser's
settings).
col_NN.png    one curve column per level (37: -18..+18 dB in 1 dB steps, 18 = 0 dB): the curve line with its glow
pill_B.png    band B's numbered pill (purple), one pill cell: a button at its frequency and gain (a tap selects the band)
ring.png      the selected band's ring, drawn over its pill
stem_sel.png  the selected band's stem through the graph
wave_NN.png   one analyser bar per step (25: -60..0 dB in 2.5 dB steps), rising from the bottom
fxm_E_S_NN.png  effect E's range bracket (S = lo / hi) at pill column NN (the last frame: hidden), one lane of the graph
graph.svg, wave.svg, divider.svg, fx.svg   the grids, labels and name, the panel's purple top line, the FX tab's heading
skin.css      the browser renderer's styling for the controls
clear.png     a near-invisible pixel strip: the drag areas (skin_post.py)
Sizes and the log axis must match src/peq.c and gen_params.py (NCOL, LEVELS, WCOL, WLEVELS, PILL_COLS, FXM_COLS)."""
import json
import math
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
NCOL, LEVELS = 48, 37
COL_W, GRAPH_H = 26, 324          # 9 px per dB
GRAPH_W = NCOL * COL_W            # 1248: nearly the whole plugin width
GRAPH_X, GRAPH_Y = 16, 88         # layout coordinates (the plugin area is y 86..714)
WCOL, WLEVELS = 48, 25            # the analyser's columns (the curve's) and steps (gen_params.py)
WAVE_H = 120                      # 5 px per step
WAVE_Y = GRAPH_Y + GRAPH_H        # right under the curve
DB_RANGE = 18.0
BANDS = [("HP", "m_hp"), ("LS", "m_ls"), ("1", "m_p1"), ("2", "m_p2"), ("3", "m_p3"), ("4", "m_p4"), ("5", "m_p5"),
         ("HS", "m_hs"), ("LP", "m_lp")]
SEG_W = 60                        # band selector segment width
PAD_ROWS, PAD_ZERO = 11, 5        # the pills' rows (gen_params.py): every 3 dB, +15 dB at the top, 0 dB in the middle
PILL_COLS = 24                    # ... and columns: every other curve column
PITCH_X, PITCH_Y = 2 * COL_W, 27  # a pill cell: two curve columns by 3 dB
PILL_W, PILL_H = PITCH_X - 4, PITCH_Y - 2
# the effects (src/peq.c FX_*): key, name, letter on the graph, colour; their range brackets sit in lanes at the bottom
# of the graph, one per effect
FX = [("rev", "REVERB", "R", "7b3fe4", (("SIZE", "size"), ("DAMP", "damp"))),
      ("dly", "DELAY", "D", "d6338a", (("TIME", "time"), ("FEEDBACK", "fb"))),
      ("cho", "CHORUS", "C", "2f74d0", (("RATE", "rate"), ("DEPTH", "depth"))),
      ("shm", "SHIMMER", "S", "b07a00", (("SIZE", "size"), ("AMOUNT", "amt")))]
FXM_COLS, LANE_H = 24, 11

# white panels, purple accents with a lilac shimmer; the grid's lines a shade darker than 0.11's
C = dict(page="ffffff", panel="ffffff", line="dcd6ea", grid="f4f3f8",
         g_minor="cbc9d6", g_major="b0adc2", g_zero="8a86a0", ink="2a2733", dim="545068", faint="7b7690",
         accent="7b3fe4", deep="4f1fae", shimmer="b896ff", foot="e9defd", pill_ink="ffffff",
         seg="eeebf6", knob="ffffff", knob_ring="e0dbee", on_ink="ffffff")


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


ACCENT, DEEP, SHIMMER = rgb(C["accent"]), rgb(C["deep"]), rgb(C["shimmer"])


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:   # Pillow < 10.1: no sized default font
        return ImageFont.load_default()


def y_of_db(db):
    return GRAPH_H / 2 - db / DB_RANGE * (GRAPH_H / 2)


def x_of_hz(hz):
    return GRAPH_W * math.log(hz / 20.0) / math.log(1000.0)


def columns():
    zero = round(y_of_db(0))
    for lv in range(LEVELS):
        y = min(GRAPH_H - 1, round(y_of_db(lv - (LEVELS - 1) // 2)))   # -18 dB lands on the bottom edge
        im = Image.new("RGBA", (COL_W, GRAPH_H), (0, 0, 0, 0))
        px = im.load()
        top, bot = min(y, zero), max(y, zero)
        span = max(1, bot - top)
        for yy in range(top, bot + 1):   # a whisper of the accent under the line, fading towards 0 dB
            a = int(28 * (1 - abs(yy - y) / span))
            for xx in range(COL_W):
                px[xx, yy] = ACCENT + (a,)
        d = ImageDraw.Draw(im)
        for dy, a in ((-5, 18), (-4, 36), (-3, 70), (-2, 130), (2, 130), (3, 70), (4, 36), (5, 18)):   # the lilac glow
            if 0 <= y + dy < GRAPH_H:
                d.line([0, y + dy, COL_W - 1, y + dy], fill=SHIMMER + (a,))
        d.rectangle([0, max(0, y - 1), COL_W - 1, min(GRAPH_H - 1, y + 1)], fill=ACCENT + (255,))
        if y - 1 >= 0:   # the shimmer: a light edge on the line
            d.line([0, y - 1, COL_W - 1, y - 1], fill=SHIMMER + (255,))
        im.save(os.path.join(HERE, "col_%02d.png" % lv))


def ring():
    """ring.png: the selected band's ring, over its (purple) pill: a dark purple outline with a white inner line"""
    im = Image.new("RGBA", (PILL_W, PILL_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([0, 0, PILL_W - 1, PILL_H - 1], radius=11, outline=DEEP + (255,), width=3)
    d.rounded_rectangle([3, 3, PILL_W - 4, PILL_H - 4], radius=8, outline=(255, 255, 255, 255), width=2)
    im.save(os.path.join(HERE, "ring.png"))


def pills():
    f = font(13)
    for b, (label, _) in enumerate(BANDS):
        im = Image.new("RGBA", (PILL_W, PILL_H), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle([1, 1, PILL_W - 2, PILL_H - 2], radius=10, fill=ACCENT + (255,), outline=DEEP + (255,), width=1)
        d.rounded_rectangle([5, 3, PILL_W - 6, 8], radius=3, fill=SHIMMER + (150,))   # the shimmer's highlight
        tw = d.textlength(label, font=f)
        d.text((PILL_W / 2 - tw / 2, PILL_H / 2 - 8), label, fill=rgb(C["pill_ink"]) + (255,), font=f)
        im.save(os.path.join(HERE, "pill_%d.png" % b))


def stems():
    cx = COL_W // 2
    im = Image.new("RGBA", (COL_W, GRAPH_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle([cx - 5, 0, cx + 4, GRAPH_H - 1], fill=SHIMMER + (40,))
    d.rectangle([cx - 1, 0, cx, GRAPH_H - 1], fill=ACCENT + (200,))
    im.save(os.path.join(HERE, "stem_sel.png"))


def wave_columns():
    foot = rgb(C["foot"])
    for lv in range(WLEVELS):
        im = Image.new("RGBA", (COL_W, WAVE_H), (0, 0, 0, 0))
        px = im.load()
        top = WAVE_H - lv * WAVE_H // (WLEVELS - 1)
        for y in range(top, WAVE_H):
            t = (WAVE_H - y) / WAVE_H
            c = tuple(int(foot[i] + (ACCENT[i] - foot[i]) * min(1.0, t * 1.6)) for i in range(3))
            for x in range(2, COL_W - 2):
                px[x, y] = c + (int(150 + 105 * t),)
        if lv:   # a bright cap
            for x in range(2, COL_W - 2):
                px[x, top] = DEEP + (255,)
                if top + 1 < WAVE_H:
                    px[x, top + 1] = SHIMMER + (255,)
        im.save(os.path.join(HERE, "wave_%02d.png" % lv))


def fx_marks():
    """fxm_E_lo_NN.png / fxm_E_hi_NN.png: one lane wide, the bracket opening towards the range at pill column NN"""
    f = font(10)
    for k, _, letter, col, _ in FX:
        c = rgb(col)
        for side in ("lo", "hi"):
            for n in range(FXM_COLS + 1):
                im = Image.new("RGBA", (GRAPH_W, LANE_H), (0, 0, 0, 0))
                if n < FXM_COLS:
                    d = ImageDraw.Draw(im)
                    x = n * PITCH_X + (1 if side == "lo" else PITCH_X - 3)
                    d.rectangle([x, 0, x + 1, LANE_H - 1], fill=c + (255,))
                    tick = [x, 0, x + 9, 1] if side == "lo" else [x - 8, 0, x + 1, 1]
                    d.rectangle(tick, fill=c + (255,))
                    d.rectangle([tick[0], LANE_H - 2, tick[2], LANE_H - 1], fill=c + (255,))
                    if side == "lo":
                        d.rectangle([x + 2, 2, x + PITCH_X - 3, LANE_H - 3], fill=c + (40,))
                        d.text((x + 13, -2), letter, fill=c + (255,), font=f)
                im.save(os.path.join(HERE, "fxm_%s_%s_%02d.png" % (k, side, n)))


def svg(name, w, h, body):
    open(os.path.join(HERE, name), "w").write(
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n%s\n</svg>\n' % (w, h, w, h, "\n".join(body)))


FONT = 'font-family="Titillium Web, sans-serif" font-weight="600"'
LOGO = ('<text x="%d" y="%d" fill="#%s" font-family="Titillium Web, sans-serif" font-weight="700" font-size="34" '
        'letter-spacing="1">SurgiQ</text>')


def freq_lines(h):
    b = []
    for dec in (10, 100, 1000, 10000):   # minor lines: 2..9 of each decade
        for m in range(2, 10):
            hz = dec * m
            if 20 < hz < 20000:
                b.append('<line x1="%.1f" y1="0" x2="%.1f" y2="%d" stroke="#%s" stroke-width="1"/>' % (x_of_hz(hz), x_of_hz(hz), h, C["g_minor"]))
    for hz in (50, 100, 200, 500, 1000, 2000, 5000, 10000):
        b.append('<line x1="%.1f" y1="0" x2="%.1f" y2="%d" stroke="#%s" stroke-width="1"/>' % (x_of_hz(hz), x_of_hz(hz), h, C["g_major"]))
    return b


def graph():
    b = ['<rect width="100%%" height="100%%" fill="#%s"/>' % C["grid"]] + freq_lines(GRAPH_H)
    for db in (-15, -12, -9, -6, -3, 3, 6, 9, 12, 15):   # level lines every 3 dB, the 6 dB ones darker
        b.append('<line x1="0" y1="%.1f" x2="%d" y2="%.1f" stroke="#%s" stroke-width="1"/>'
                 % (y_of_db(db), GRAPH_W, y_of_db(db), C["g_major"] if db % 6 == 0 else C["g_minor"]))
    b.append('<line x1="0" y1="%.1f" x2="%d" y2="%.1f" stroke="#%s" stroke-width="1.5"/>' % (y_of_db(0), GRAPH_W, y_of_db(0), C["g_zero"]))
    for db in (-12, -6, 0, 6, 12):
        b.append('<text x="%d" y="%.1f" fill="#%s" %s font-size="13" text-anchor="end">%s</text>'
                 % (GRAPH_W - 8, y_of_db(db) - 5, C["faint"], FONT, "0 dB" if db == 0 else "%+d" % db))
    b.append(LOGO % (12, 36, C["accent"]))
    b.append('<text x="140" y="33" fill="#%s" %s font-size="12" letter-spacing="2">DRAG UP / DOWN · GAIN OF THE SELECTED BAND'
             '</text>' % (C["faint"], FONT))
    svg("graph.svg", GRAPH_W, GRAPH_H, b)


def wave_bg():
    b = ['<rect width="100%%" height="100%%" fill="#%s"/>' % C["grid"]] + freq_lines(WAVE_H)
    for step in (4, 8, 12, 16, 20):   # -50 .. -10 dB
        y = WAVE_H - step * WAVE_H / (WLEVELS - 1)
        b.append('<line x1="0" y1="%.1f" x2="%d" y2="%.1f" stroke="#%s" stroke-width="1"/>' % (y, GRAPH_W, y, C["g_minor"]))
    b.append('<line x1="0" y1="0.5" x2="%d" y2="0.5" stroke="#%s" stroke-width="1"/>' % (GRAPH_W, C["g_zero"]))
    for hz in (50, 100, 200, 500, 1000, 2000, 5000, 10000):
        label = "%dk" % (hz // 1000) if hz >= 1000 else str(hz)
        b.append('<text x="%.1f" y="%d" fill="#%s" %s font-size="12" text-anchor="middle" letter-spacing="1">%s</text>'
                 % (x_of_hz(hz), WAVE_H - 6, C["faint"], FONT, label))
    b.append('<text x="12" y="18" fill="#%s" %s font-size="12" letter-spacing="2">ANALYSER · DRAG LEFT / RIGHT · FREQUENCY</text>'
             % (C["faint"], FONT))
    svg("wave.svg", GRAPH_W, WAVE_H, b)


def divider():
    """divider.svg: the thin purple line between the analyser and the control panel, a lilac shimmer along it"""
    svg("divider.svg", 1260, 3, [
        '<defs><linearGradient id="s" x1="0" x2="1" y1="0" y2="0"><stop offset="0" stop-color="#%s"/>'
        '<stop offset="0.35" stop-color="#%s"/><stop offset="0.5" stop-color="#%s"/><stop offset="0.65" stop-color="#%s"/>'
        '<stop offset="1" stop-color="#%s"/></linearGradient></defs>' % (C["accent"], C["accent"], C["shimmer"], C["accent"], C["accent"]),
        '<rect width="1260" height="3" fill="url(#s)"/>'])


def fx_bg():
    """fx.svg: the FX tab's heading, and which letter and colour marks each effect's range on the graph"""
    b = ['<rect width="100%%" height="100%%" fill="#%s"/>' % C["page"], LOGO % (0, 40, C["accent"]),
         '<text x="132" y="37" fill="#%s" %s font-size="14" letter-spacing="3">FX · SENDS AFTER THE EQ</text>' % (C["faint"], FONT)]
    x = 1248
    for k, name, letter, col, _ in reversed(FX):
        label = "%s %s" % (letter, name)
        w = 16 + 9 * len(label)
        x -= w
        b.append('<rect x="%d" y="26" width="4" height="14" fill="#%s"/>' % (x, col))
        b.append('<text x="%d" y="38" fill="#%s" %s font-size="13" letter-spacing="1.5">%s</text>' % (x + 9, col, FONT, label))
        x -= 8
    b.append('<text x="1248" y="58" fill="#%s" %s font-size="11" letter-spacing="1.5" text-anchor="end">'
             'RANGES SHOW AS BRACKETS ALONG THE BOTTOM OF THE EQ GRAPH</text>' % (C["faint"], FONT))
    svg("fx.svg", 1248, 64, b)


def css():
    open(os.path.join(HERE, "skin.css"), "w").write("""/* SurgiQ (tools/html_art/default.css lists every class). WRITTEN BY art/make_art.py. */
:root { --sheen: 0; --radius: 5px; --shadow: none; --title-size: 14px; --label-size: 13px; --seg-size: 16px;
        --button-size: 15px; }
.frame-border { fill: #%(panel)s; stroke: #%(line)s; stroke-width: 1; }
.frame-rule { stroke: none; }
.frame-title { font-size: 14px; font-weight: 600; fill: #%(accent)s; letter-spacing: 0.22em; }
.box-label { font-size: 12px; fill: #%(dim)s; letter-spacing: 0.18em; }
.box { fill: #%(knob)s; stroke: #%(line)s; }
.knob-track { stroke: #%(knob_ring)s; }
.knob-face { fill: #%(knob)s; stroke: #%(line)s; stroke-width: 1.5; }
.knob-arc, .knob-pointer { stroke: var(--accent); }
.seg-bg { rx: 4px; }
.seg-tx { font-weight: 600; letter-spacing: 0.06em; }
.button-bg { rx: 5px; }
.button-tx { font-weight: 700; letter-spacing: 0.14em; }
.pill-track { fill: #%(seg)s; stroke: #%(line)s; }
.pill-thumb { fill: #%(knob)s; stroke: #%(dim)s; stroke-width: 1; }
.on .pill-thumb { fill: var(--accent); stroke: none; }
""" % C)


def layout():
    """../layout.conf and geometry.json"""
    y0 = WAVE_Y + WAVE_H + 6                  # the control panel fills the rest of the plugin area
    h = 712 - y0
    r1, r2 = y0 + 50, y0 + 130                # the panel's two rows
    # four groups across the panel, an even gap between them: band, readouts, Q, output
    sel_w = len(BANDS) * SEG_W + (len(BANDS) - 1) * 2
    widths = [sel_w, 240, 140, 110]
    gap = (1248 - sum(widths)) // (len(widths) - 1)
    xs, x = [], GRAPH_X
    for w in widths:
        xs.append((x, x + w))
        x += w + gap
    mid = [(a + b) // 2 for a, b in xs]
    (ba, bb), rd, qx, ox = xs[0], mid[1], mid[2], mid[3]
    freq_box = (rd - 120, r1 + 4 - 21, 240, 42)   # the readouts' boxes (w 240, h 42)
    gain_box = (rd - 120, r2 + 4 - 21, 240, 42)
    pad_cy = round(GRAPH_Y + y_of_db(0) - PAD_ZERO * PITCH_Y)   # the top row's centre (+15 dB)
    cell0 = (GRAPH_X + 2, pad_cy - PILL_H // 2)                 # the first pill cell: two columns by 3 dB, 0 dB in row 5
    cols = ",".join("art/col_%02d.png" % v for v in range(LEVELS))
    L = ["# SurgiQ. WRITTEN BY art/make_art.py: edit that script, not this file.",
         "# EQ tab: a live curve (picture columns on a log axis), each band's pill at its frequency and gain over it (made",
         "# transparent by skin_post.py) with the selected one's ring, the effects' range brackets, the analyser on the same",
         "# columns, drag areas on the graph, the analyser and the readouts (skin_post.py makes them) and the band controls.",
         "# FX tab: reverb, delay, chorus and shimmer, and the analyser's settings.",
         "art_css=art/skin.css",
         "theme_bg=" + C["page"], "theme_panel=" + C["panel"], "theme_line=" + C["line"], "theme_ink=" + C["ink"],
         "theme_ink_dim=" + C["dim"], "theme_ink_faint=" + C["faint"], "theme_accent=" + C["accent"],
         "theme_accent_hi=" + C["deep"], "theme_knob_face=" + C["knob"], "theme_knob_ring=" + C["knob_ring"],
         "theme_knob_dot=" + C["accent"], "theme_lcd=" + C["knob"], "theme_seg_active=" + C["accent"],
         "theme_seg_inactive=" + C["seg"], "theme_seg_active_tx=" + C["on_ink"], "theme_btn_text=" + C["on_ink"],
         "[tab EQ]",
         'frame x=10 y=%d w=1260 h=%d title=""' % (y0, h),
         'art file="art/divider.svg" x=10 y=%d w=1260 h=3' % (y0 - 4),
         'art file="art/graph.svg" x=%d y=%d w=%d h=%d' % (GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H)]
    for i in range(NCOL):
        L.append('picture x=%d y=%d w=%d h=%d key=c%02d files="%s"' % (GRAPH_X + i * COL_W, GRAPH_Y, COL_W, GRAPH_H, i, cols))
    # the effects' range brackets: one lane each along the bottom of the graph (a picture per bracket: few screen parts)
    for e, (k, _, _, _, _) in enumerate(FX):
        ly = GRAPH_Y + GRAPH_H - 4 - (len(FX) - e) * (LANE_H + 1)
        for side in ("lo", "hi"):
            files = ",".join("art/fxm_%s_%s_%02d.png" % (k, side, n) for n in range(FXM_COLS + 1))
            L.append('picture x=%d y=%d w=%d h=%d key=fxm_%s_%s files="%s"' % (GRAPH_X, ly, GRAPH_W, LANE_H, k, side, files))
    # over the curve: the selected band's stem (one image per column), then each band's pill (one per pill cell)
    for c in range(NCOL):
        L.append('art file="art/stem_sel.png" x=%d y=%d w=%d h=%d when=m_sel:%d' % (GRAPH_X + c * COL_W, GRAPH_Y, COL_W, GRAPH_H, c))
    # the graph's drag area (up/down: the selected band's gain; a thin slider grown by skin_post.py), under the pills so a
    # tap on a pill reaches it
    L.append('slider_v cx=%d cy=%d w=8 h=%d label="" key=sel_gain ns=0 vs=12 img=art/clear.png base=art/clear.png'
             % (GRAPH_X + GRAPH_W // 2, GRAPH_Y + GRAPH_H // 2, GRAPH_H))
    # each band's pill: a button (tap: select the band) shown only in the cell its pp_xx names (transparent edges)
    for b, (_, key) in enumerate(BANDS):
        for i in range(PILL_COLS * PAD_ROWS):
            r, c = divmod(i, PILL_COLS)
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick_%s img=art/pill_%d.png img_on=art/pill_%d.png when=pp_%s:%d'
                     % (cell0[0] + c * PITCH_X + PILL_W // 2, cell0[1] + r * PITCH_Y + PILL_H // 2, PILL_W, PILL_H,
                        key[2:], b, b, key[2:], i))
    for i in range(PILL_COLS * PAD_ROWS):   # the selected band's ring, over its pill
        r, c = divmod(i, PILL_COLS)
        L.append('art file="art/ring.png" x=%d y=%d w=%d h=%d when=pp_sel:%d'
                 % (cell0[0] + c * PITCH_X, cell0[1] + r * PITCH_Y, PILL_W, PILL_H, i))
    L.append('art file="art/wave.svg" x=%d y=%d w=%d h=%d' % (GRAPH_X, WAVE_Y, GRAPH_W, WAVE_H))
    wfiles = ",".join("art/wave_%02d.png" % v for v in range(WLEVELS))
    for i in range(WCOL):
        L.append('picture x=%d y=%d w=%d h=%d key=w%02d files="%s"' % (GRAPH_X + i * COL_W, WAVE_Y, COL_W, WAVE_H, i, wfiles))
    btn = [ba + (bb - ba) * k // 8 for k in (1, 3, 5, 7)]   # four buttons evenly under the band selector
    L += [# the other drag areas: thin sliders here, grown by skin_post.py to the analyser (left/right: frequency) and the
          # readout boxes. The GAIN box drives g_rel, the relative gain control (fine steps, like its Q-Link)
          'slider_h cx=%d cy=%d w=%d h=8 label="" key=sel_freq ns=0 vs=12 img=art/clear.png base=art/clear.png'
          % (GRAPH_X + GRAPH_W // 2, WAVE_Y + WAVE_H // 2, GRAPH_W),
          'slider_h cx=%d cy=%d w=%d h=8 label="" key=sel_freq ns=0 vs=12 img=art/clear.png base=art/clear.png'
          % (freq_box[0] + freq_box[2] // 2, freq_box[1] + freq_box[3] // 2, freq_box[2]),
          'slider_v cx=%d cy=%d w=8 h=%d label="" key=g_rel ns=0 vs=12 img=art/clear.png base=art/clear.png'
          % (gain_box[0] + gain_box[2] // 2, gain_box[1] + gain_box[3] // 2, gain_box[3]),
          # 1. band: the selector, then its buttons
          'enum_h cx=%d cy=%d label="" key=sel_band sw=%d sh=38' % ((ba + bb) // 2, r1, SEG_W),
          'button cx=%d cy=%d label="PREV" key=band_prev' % (btn[0], r2),
          'button cx=%d cy=%d label="NEXT" key=band_next' % (btn[1], r2),
          'toggle cx=%d cy=%d label="BAND ON" key=sel_on bw=110' % (btn[2], r2 - 8),
          'button cx=%d cy=%d label="RESET" key=band_reset' % (btn[3], r2),
          # 2. the selected band's values (drag them to fine-tune)
          'readout cx=%d cy=%d w=240 h=42 label="FREQUENCY · DRAG" key=sel_freq tsize=28 tcolor=%s' % (rd, r1 + 4, C["accent"]),
          'readout cx=%d cy=%d w=240 h=42 label="GAIN · DRAG" key=sel_gain tsize=28 tcolor=%s' % (rd, r2 + 4, C["accent"]),
          # 3. Q, and the filters' slope under it (the knob's touch box, to cy + r + 40, ends above the slope's, from cy - 39)
          'knob cx=%d cy=%d r=32 label="Q" key=sel_q bw=130' % (qx, r1 - 6),
          'enum_h cx=%d cy=%d label="" key=sel_slope sw=40 when=sel_band:HP' % (qx, y0 + 154),
          'enum_h cx=%d cy=%d label="" key=sel_slope sw=40 when=sel_band:LP' % (qx, y0 + 154),
          # 4. output
          'knob cx=%d cy=%d r=32 label="OUT" key=out_gain bw=100' % (ox, r1 + 2),
          '# Q-Links: one column of 4 per press of the Q-Link button. Gain is the relative control (fine steps); the short',
          '# option lists are on their q_* versions: continuous knobs split into zones (src/peq.c QKEYS)',
          'qlinks "EQ" = sel_band,sel_freq,g_rel,out_gain,sel_q,q_on,q_slope,q_view,q_speed,q_rate,q_spec,-']
    # the FX tab: the heading, the analyser's settings in a strip, then one panel per effect (knobs all r=32: the EQ tab's
    # size, so they share one filmstrip; each knob size is one more filmstrip MPC decodes per open)
    ay, ah = 158, 104                          # the analyser strip
    fy, fh = ay + ah + 12, 712 - (ay + ah + 12)   # the effect panels
    fw = (1248 - 3 * 16) // 4
    L += ["[tab FX]",
          'art file="art/fx.svg" x=16 y=90 w=1248 h=64',
          'frame x=16 y=%d w=1248 h=%d title="ANALYSER"' % (ay, ah),
          'toggle cx=150 cy=%d label="ANALYSER ON" key=spec_on bw=170' % (ay + 60),
          'enum_h cx=440 cy=%d label="SHOW" key=wave_view sw=84 sh=40' % (ay + 64),
          'enum_h cx=760 cy=%d label="FALL-OFF SPEED" key=wave_time sw=84 sh=40' % (ay + 64),
          'enum_h cx=1080 cy=%d label="UPDATE RATE" key=spec_rate sw=84 sh=40' % (ay + 64)]
    for e, (k, name, _, _, ((n1, s1), (n2, s2))) in enumerate(FX):
        x0 = 16 + e * (fw + 16)
        p = "fx_%s_" % k
        L += ['frame x=%d y=%d w=%d h=%d title="%s"' % (x0, fy, fw, fh, name),
              'toggle cx=%d cy=%d label="ON" key=%son bw=100' % (x0 + 72, fy + 64, p),
              'enum_h cx=%d cy=%d label="" key=%sfull sw=74 sh=38' % (x0 + fw - 88, fy + 64, p),
              'knob cx=%d cy=%d r=32 label="MIX" key=%smix bw=90' % (x0 + fw // 6, fy + 160, p),
              'knob cx=%d cy=%d r=32 label="%s" key=%s%s bw=90' % (x0 + fw // 2, fy + 160, n1, p, s1),
              'knob cx=%d cy=%d r=32 label="%s" key=%s%s bw=90' % (x0 + fw * 5 // 6, fy + 160, n2, p, s2),
              'knob cx=%d cy=%d r=32 label="BAND LOW" key=%slo bw=120' % (x0 + fw // 4 + 6, fy + 300, p),
              'knob cx=%d cy=%d r=32 label="BAND HIGH" key=%shi bw=120' % (x0 + fw * 3 // 4 - 6, fy + 300, p)]
    L += ['# Q-Links: each effect\'s mix and its two settings in turn, then the analyser\'s settings',
          'qlinks "FX" = fx_rev_mix,fx_rev_size,fx_rev_damp,fx_dly_mix,fx_dly_time,fx_dly_fb,fx_cho_mix,fx_cho_rate,'
          'fx_cho_depth,fx_shm_mix,fx_shm_size,fx_shm_amt,q_spec,q_view,q_speed,q_rate']
    open(os.path.join(HERE, "..", "layout.conf"), "w").write("\n".join(L) + "\n")
    json.dump({"graph": [GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H], "col_w": COL_W,
               # each drag area: the builder's slider definition (by its name's start) and the rect it should cover
               "drags": [["shSlider_v_8x%d" % GRAPH_H, [GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H]],
                         ["shSlider_h_%dx8" % GRAPH_W, [GRAPH_X, WAVE_Y, GRAPH_W, WAVE_H]],
                         ["shSlider_h_%dx8" % freq_box[2], list(freq_box)],
                         ["shSlider_v_8x%d" % gain_box[3], list(gain_box)]],
               "stems": {"m_sel": "stem_sel.png"},
               # the selected band's ring (art): its parameter's option o is pill cell o. The pills themselves are buttons
               # since 0.12.1 (the builder keeps a button's image transparent and places it exactly)
               "pills": {"pp_sel": "ring.png"},
               "cells": {"x": cell0[0], "y": cell0[1], "pitch_x": PITCH_X, "pitch_y": PITCH_Y, "w": PILL_W, "h": PILL_H,
                         "cols": PILL_COLS},
               # the effects' bracket pictures: each frame becomes its transparent PNG over exactly its lane (the builder
               # bakes them opaque, so a lane's high bracket would hide its low one and the curve)
               "lanes": {"fxm_%s_%s" % (k, side): {"rect": [GRAPH_X, GRAPH_Y + GRAPH_H - 4 - (len(FX) - e) * (LANE_H + 1),
                                                            GRAPH_W, LANE_H],
                                                   "file": "fxm_%s_%s_%%02d.png" % (k, side), "frames": FXM_COLS + 1}
                         for e, (k, _, _, _, _) in enumerate(FX) for side in ("lo", "hi")}},
              open(os.path.join(HERE, "geometry.json"), "w"), indent=1)
    return y0, h, gap


if __name__ == "__main__":
    for f in os.listdir(HERE):   # drop the previous build's images (other sizes, the old themes)
        if f.endswith(".png") or f.endswith(".svg"):
            os.remove(os.path.join(HERE, f))
    columns()
    ring()
    pills()
    stems()
    wave_columns()
    fx_marks()
    graph()
    wave_bg()
    divider()
    fx_bg()
    css()
    # the drag areas' strip: near-invisible, alpha 1 (a fully transparent image may not take the touch)
    Image.new("RGBA", (4, 8), (0, 0, 0, 1)).save(os.path.join(HERE, "clear.png"))
    y0, h, gap = layout()
    print("art: %d columns x %d levels, %d bands on %d pill columns, %d effects, analyser %dx%d, layout.conf (panel y=%d h=%d)"
          % (NCOL, LEVELS, len(BANDS), PILL_COLS, len(FX), WCOL, WLEVELS, y0, h))
