#!/usr/bin/env python3
"""Draws the SurgiQ screen's artwork into this folder (needs Pillow). Run from the repo root:
    docker run --rm -v <repo>:/r -w /r python:3.11-slim sh -c \
      "pip install -q --target /tmp/p pillow; PYTHONPATH=/tmp/p python3 ports/peq/art/make_art.py"
It also rewrites ../layout.conf and writes geometry.json (for ../skin_post.py), so the sizes here, the layout's coordinates
and the post-build step can't drift apart. One look since 0.12.0: white, with red accents since 0.13.0 (C below).

Two tabs: EQ (the graph with the analyser's spectrum behind the curve, and the band controls) and FX (reverb, delay,
chorus, shimmer, and the analyser's settings). Since 0.15.0 the spectrum fills the graph behind the curve (Pro-Q style)
instead of a strip under it: both are transparent layers (skin_post.py "lanes"), so the curve shows over the spectrum.
col_NNN.png   one curve column per level and tilt (37 x 9: from -18..+18 dB at its left edge, in 1 dB steps, rising or
              falling 0, 1, 2, 4 or 8 dB across it): a segment of the curve line with its glow
pill_B.png    band B's numbered pill (red), one pill cell, shown at its frequency and gain (an invisible button over it,
              pill_clear.png, takes the taps and drags)
ring.png      the selected band's ring, drawn over its pill
stem_sel.png  the selected band's stem through the graph
wave_NNN.png  one spectrum column per level and tilt (25 x 9: -60..0 dB in 2.5 dB levels, tilting 0, 1, 2, 4 or 8 levels
              across it): a segment of the spectrum's outline over a soft fill; chained by the engine into one shape
fxm_E_S_NN.png  effect E's range bracket (S = lo / hi) at pill column NN (the last frame: hidden), one lane of the graph
graph.svg, divider.svg, fx.svg   the grid, labels and name, the panel's red top line, the FX tab's heading
skin.css      the browser renderer's styling for the controls
clear.png     a near-invisible pixel strip: the drag areas (skin_post.py)
Sizes and the log axis must match src/peq.c and gen_params.py (NCOL, LEVELS, TILTS, WCOL, WLEVELS, PILL_COLS, FXM_COLS)."""
import json
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
NCOL, LEVELS = 48, 37
TILT = (-8, -4, -2, -1, 0, 1, 2, 4, 8)   # a curve segment's tilts, levels across its column (src/peq.c TILT, gen_params.py)
TILTS = len(TILT)
COL_W, GRAPH_H = 25, 444          # 12.33 px per dB (src/peq.c TOUCH_PX_DB); 0.15.0 took in the analyser strip's 120 px
GRAPH_W = NCOL * COL_W            # 1200 (src/peq.c TOUCH_GRAPH_W): since 0.18.1 26 px columns became 25, ...
GRAPH_X, GRAPH_Y = 56, 88         # ... and the graph moved 40 px in from the left edge, so its low end is easier to reach
                                  # (layout coordinates; the plugin area is y 86..714)
WCOL, WLEVELS = 48, 25            # the spectrum's columns (the curve's) and levels (src/peq.c, gen_params.py SCOL)
WTILT = (0, -1, 1, -2, 2, -4, 4, -8, 8)   # its segments' tilts, flat first (src/peq.c WTILT)
WFRAMES = WLEVELS * len(WTILT)    # a spectrum column's frames: level * len(WTILT) + the tilt's index
WCOL_W = GRAPH_W // WCOL          # 25 px
W_TOP = 0.06                      # the spectrum's 0 dB sits this share of the graph below its top
W_STEP = 2.5                      # dB a spectrum level (src/peq.c W_STEP)
DB_RANGE = 18.0
BANDS = [("HP", "m_hp"), ("LS", "m_ls"), ("1", "m_p1"), ("2", "m_p2"), ("3", "m_p3"), ("4", "m_p4"), ("5", "m_p5"),
         ("6", "m_p6"), ("7", "m_p7"), ("HS", "m_hs"), ("LP", "m_lp")]   # 6 and 7 since 0.17.0
SEG_W = 52                        # band selector segment width (11 bands)
PAD_ROWS, PAD_ZERO = 11, 5        # the pills' rows (gen_params.py): every 3 dB, +15 dB at the top, 0 dB in the middle
PILL_COLS = 24                    # ... and columns: every other curve column
PITCH_X, PITCH_Y = 2 * COL_W, 37  # a pill cell: two curve columns by 3 dB
PILL_W, PILL_H = PITCH_X - 4, 25
# the effects (src/peq.c FX_*): key, name, letter on the graph, colour; their range brackets sit in lanes at the bottom
# of the graph, one per effect
FX = [("rev", "REVERB", "R", "e8173a", (("SIZE", "size"), ("DAMP", "damp"))),
      ("dly", "DELAY", "D", "1f9e8a", (("TIME", "time"), ("FEEDBACK", "fb"))),
      ("cho", "CHORUS", "C", "2f74d0", (("RATE", "rate"), ("DEPTH", "depth"))),
      ("shm", "SHIMMER", "S", "b07a00", (("SIZE", "size"), ("AMOUNT", "amt")))]
FXM_COLS, LANE_H = 24, 11

# white panels, red accents (0.11's MONITOR red, back in 0.13.0) with a pink shimmer; the grid's lines red too
C = dict(page="ffffff", panel="ffffff", line="ecd6da", grid="f8f4f5",
         g_minor="f2c2ca", g_major="e8939f", g_zero="c8243f", ink="2b2728", dim="5a5052", faint="857b7d",
         accent="e8173a", deep="a50f28", shimmer="ff8c9f", foot="fbdbe1", pill_ink="ffffff",
         seg="f4eced", knob="ffffff", knob_ring="efdcdf", on_ink="ffffff", sel_wash="a0999a")


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


ACCENT, DEEP, SHIMMER = rgb(C["accent"]), rgb(C["deep"]), rgb(C["shimmer"])
GLOW, HOT = (255, 24, 60), (255, 92, 108)   # the curve's neon: its red glow and the brighter red core of the line


def font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:   # Pillow < 10.1: no sized default font
        return ImageFont.load_default()


def y_of_db(db):
    return GRAPH_H / 2 - db / DB_RANGE * (GRAPH_H / 2)


def x_of_hz(hz):
    return GRAPH_W * math.log(hz / 20.0) / math.log(1000.0)


def level_y(lv):
    """the curve's y for level lv (0..LEVELS-1, -18..+18 dB); -18 dB lands just inside the bottom edge"""
    return min(GRAPH_H - 2.0, y_of_db(lv - (LEVELS - 1) // 2))


def columns():
    """col_NNN.png, frame = level * TILTS + the tilt's index (src/peq.c): a segment from the level at the column's left
    edge to TILT[i] levels higher at its right edge; the engine chains them, so neighbouring columns join into one line.
    Drawn at SS times the size and scaled down, so a sloped line comes out smooth"""
    SS = 4
    zero = y_of_db(0)
    for f in range(LEVELS * TILTS):
        lv, i = divmod(f, TILTS)
        t = TILT[i]
        y0, y1 = level_y(lv), level_y(min(LEVELS - 1, max(0, lv + t)))
        big = Image.new("RGBA", (COL_W * SS, GRAPH_H * SS), (0, 0, 0, 0))
        d = ImageDraw.Draw(big)
        # a whisper of the accent between the line and 0 dB
        d.polygon([(0, y0 * SS), (COL_W * SS, y1 * SS), (COL_W * SS, zero * SS), (0, zero * SS)], fill=ACCENT + (22,))
        # lit up like a red neon tube (0.17.0). The tube's strokes run a little past both edges, so neighbours overlap
        seg = [(-SS, (y0 - (y1 - y0) / COL_W) * SS), ((COL_W + 1) * SS, (y1 + (y1 - y0) / COL_W) * SS)]
        # each stroke with round caps on the column's edges, where the next segment joins: a bend then glows all round
        ends = [(0, y0 * SS), (COL_W * SS, y1 * SS)]

        def stroke(col, w):
            d.line(seg, fill=col, width=w)
            for x, y in ends:
                d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=col)

        # the glow (0.18.0: brighter, wider, and blurred rather than stacked, so it has no bands): the line drawn on a
        # wider canvas, carried on past both edges along its own slope so the glow doesn't fade at the column's edge,
        # blurred, and cropped back to the column
        P = 32 * SS                      # the canvas's margin
        dx, dy = COL_W * SS, (y1 - y0) * SS
        n = math.hypot(dx, dy)
        ext = 10 * SS / n                # the line carried on ~10 px along itself past each end: about the glow's
        wide = [(P - dx * ext, y0 * SS - dy * ext), (P + dx * (1 + ext), y1 * SS + dy * ext)]   # radius, short enough not
        glow = Image.new("RGBA", big.size, (0, 0, 0, 0))                                      # to streak past a bend
        for width, blur, alpha in ((8, 9, 0.75), (4, 3, 0.9)):   # a wide soft halo, then a tighter bright one
            lay = Image.new("L", (COL_W * SS + 2 * P, GRAPH_H * SS), 0)
            ld = ImageDraw.Draw(lay)
            ld.line(wide, fill=255, width=width * SS)
            for x, y in ((P, y0 * SS), (P + dx, y1 * SS)):   # round at the joins
                ld.ellipse([x - width * SS / 2, y - width * SS / 2, x + width * SS / 2, y + width * SS / 2], fill=255)
            lay = lay.filter(ImageFilter.GaussianBlur(blur * SS)).crop((P, 0, P + COL_W * SS, GRAPH_H * SS))
            tint = Image.new("RGBA", big.size, GLOW + (0,))
            tint.putalpha(lay.point(lambda a, k=alpha: int(a * k)))
            glow = Image.alpha_composite(glow, tint)
        big = Image.alpha_composite(big, glow)   # red light over the whisper fill, not paint mixed with black
        d = ImageDraw.Draw(big)
        stroke(ACCENT + (255,), 5 * SS)   # the tube
        stroke(HOT + (255,), 2 * SS)   # and its hot core
        big.resize((COL_W, GRAPH_H), Image.LANCZOS).save(os.path.join(HERE, "col_%03d.png" % f))


def ring():
    """ring.png: the selected band's ring, over its (red) pill: a half-opaque grey wash that greys the pill out (its
    label still shows through) inside a red outline"""
    im = Image.new("RGBA", (PILL_W, PILL_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([1, 1, PILL_W - 2, PILL_H - 2], radius=10, fill=rgb(C["sel_wash"]) + (140,))
    d.rounded_rectangle([0, 0, PILL_W - 1, PILL_H - 1], radius=11, outline=ACCENT + (255,), width=3)
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


def wave_top(lv):
    """the height of spectrum level lv in the graph: 1..WLEVELS-1 is -60 + lv * W_STEP dB, 0 (below -60 dB) the bottom"""
    if lv <= 0:
        return float(GRAPH_H)
    db = -60 + lv * W_STEP
    return GRAPH_H * W_TOP + GRAPH_H * (1 - W_TOP) * -db / 60.0


def wave_columns():
    """wave_NNN.png, frame = level * len(WTILT) + the tilt's index (src/peq.c): the spectrum behind the curve, a segment
    of its outline from the level at the column's left edge to WTILT[i] levels higher at its right edge, filled to the
    bottom. The engine chains the segments, so the outline is one line (0.18.0). Kept quiet (a translucent pink fill, a
    thin soft edge) so the curve stays the boldest thing on the graph; drawn at SS times the size and scaled down"""
    SS = 4
    foot = rgb(C["foot"])
    grad = Image.new("RGBA", (WCOL_W, GRAPH_H))   # the fill: pink at the foot to the accent higher up, more opaque
    gp = grad.load()
    for y in range(GRAPH_H):
        t = (GRAPH_H - y) / GRAPH_H
        c = tuple(int(foot[i] + (ACCENT[i] - foot[i]) * min(1.0, t * 1.2)) for i in range(3))
        for x in range(WCOL_W):
            gp[x, y] = c + (int(45 + 75 * t),)
    grad = grad.resize((WCOL_W * SS, GRAPH_H * SS))
    for f in range(WFRAMES):
        lv, i = divmod(f, len(WTILT))
        to = min(WLEVELS - 1, max(0, lv + WTILT[i]))
        y0, y1 = wave_top(lv) * SS, wave_top(to) * SS
        big = Image.new("RGBA", (WCOL_W * SS, GRAPH_H * SS), (0, 0, 0, 0))
        if lv or to:   # level 0 to 0: below -60 dB all along, nothing
            mask = Image.new("L", big.size, 0)
            ImageDraw.Draw(mask).polygon([(0, y0), (WCOL_W * SS, y1), (WCOL_W * SS, GRAPH_H * SS), (0, GRAPH_H * SS)], fill=255)
            big.paste(grad, (0, 0), mask)
            d = ImageDraw.Draw(big)
            seg = [(-SS, y0 - (y1 - y0) / WCOL_W), ((WCOL_W + 1) * SS, y1 + (y1 - y0) / WCOL_W)]
            for col, w in ((SHIMMER + (40,), 7 * SS), (SHIMMER + (90,), 4 * SS), (ACCENT + (170,), 2 * SS)):   # the edge
                d.line(seg, fill=col, width=w)
                for x, y in ((0, y0), (WCOL_W * SS, y1)):   # round joins, as on the curve
                    d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=col)
        big.resize((WCOL_W, GRAPH_H), Image.LANCZOS).save(os.path.join(HERE, "wave_%03d.png" % f))

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
    for db in (-12, -6, 0, 6, 12):   # just above a pill on the line (pills reach 12 px above it), so one there can't hide it
        b.append('<text x="%d" y="%.1f" fill="#%s" %s font-size="13" text-anchor="end">%s</text>'
                 % (GRAPH_W - 8, y_of_db(db) - 15, C["faint"], FONT, "0 dB" if db == 0 else "%+d" % db))
    for hz in (50, 100, 200, 500, 1000, 2000, 5000, 10000):   # the frequencies along the bottom (the analyser strip's, 0.15.0)
        label = "%dk" % (hz // 1000) if hz >= 1000 else str(hz)
        b.append('<text x="%.1f" y="%d" fill="#%s" %s font-size="12" text-anchor="middle" letter-spacing="1">%s</text>'
                 % (x_of_hz(hz), GRAPH_H - 6, C["faint"], FONT, label))
    b.append(LOGO % (12, 36, C["accent"]))
    b.append('<text x="140" y="33" fill="#%s" %s font-size="12" letter-spacing="2">HOLD AND DRAG A PILL · FREQUENCY AND GAIN'
             '</text>' % (C["faint"], FONT))
    svg("graph.svg", GRAPH_W, GRAPH_H, b)


def divider():
    """divider.svg: the thin red line between the graph and the control panel, a pink shimmer along it"""
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


def lane_y(e):
    """effect e's bracket lane: near the bottom of the graph, clear of the frequency labels"""
    return GRAPH_Y + GRAPH_H - 24 - (len(FX) - e) * (LANE_H + 1)


def layout():
    """../layout.conf and geometry.json"""
    y0 = GRAPH_Y + GRAPH_H + 6                # the control panel fills the rest of the plugin area
    h = 712 - y0
    r1, r2 = y0 + 50, y0 + 130                # the panel's two rows
    # four groups across the panel, an even gap between them: band, readouts, Q, output
    sel_w = len(BANDS) * SEG_W + (len(BANDS) - 1) * 2
    widths = [sel_w, 240, 140, 110]
    gap = (1248 - sum(widths)) // (len(widths) - 1)
    xs, x = [], 16                            # the panel keeps the full width (the graph has a margin since 0.18.1)
    for w in widths:
        xs.append((x, x + w))
        x += w + gap
    mid = [(a + b) // 2 for a, b in xs]
    (ba, bb), rd, qx, ox = xs[0], mid[1], mid[2], mid[3]
    freq_box = (rd - 120, r1 + 4 - 21, 240, 42)   # the readouts' boxes (w 240, h 42)
    gain_box = (rd - 120, r2 + 4 - 21, 240, 42)
    pad_cy = round(GRAPH_Y + y_of_db(0) - PAD_ZERO * PITCH_Y)   # the top row's centre (+15 dB)
    cell0 = (GRAPH_X + 2, pad_cy - PILL_H // 2)                 # the first pill cell: two columns by 3 dB, 0 dB in row 5
    cols = ",".join("art/col_%03d.png" % v for v in range(LEVELS * TILTS))
    L = ["# SurgiQ. WRITTEN BY art/make_art.py: edit that script, not this file.",
         "# EQ tab: the analyser's spectrum and over it the live curve (picture columns on a log axis, both made transparent",
         "# layers by skin_post.py), the effects' range brackets, each band's pill at its frequency and gain (a button: hold",
         "# and drag it to move the band, src/peq.c touch_thread) with the selected one's ring, drag areas on the graph and the",
         "# readouts (skin_post.py makes them) and the band controls.",
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
    # the spectrum, then the curve over it: pictures whose frames skin_post.py turns into transparent layers ("lanes")
    wfiles = ",".join("art/wave_%03d.png" % v for v in range(WFRAMES))
    for i in range(WCOL):
        L.append('picture x=%d y=%d w=%d h=%d key=w%02d files="%s"' % (GRAPH_X + i * WCOL_W, GRAPH_Y, WCOL_W, GRAPH_H, i, wfiles))
    for i in range(NCOL):
        L.append('picture x=%d y=%d w=%d h=%d key=c%02d files="%s"' % (GRAPH_X + i * COL_W, GRAPH_Y, COL_W, GRAPH_H, i, cols))
    # the effects' range brackets: one lane each near the bottom of the graph, above the frequency labels (a picture per bracket)
    for e, (k, _, _, _, _) in enumerate(FX):
        ly = lane_y(e)
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
    # each band's pill, drawn as an image in the cell its pp_xx names, then the selected band's ring over it. MPC draws
    # every image before every button (the builder lists them first), so a ring over a pill button never showed
    # (0.11.0-0.15.0): since 0.15.1 the pills' look is images and their buttons are invisible touch areas on top
    for b, (_, key) in enumerate(BANDS):
        for i in range(PILL_COLS * PAD_ROWS):
            r, c = divmod(i, PILL_COLS)
            L.append('art file="art/pill_%d.png" x=%d y=%d w=%d h=%d when=pp_%s:%d'
                     % (b, cell0[0] + c * PITCH_X, cell0[1] + r * PITCH_Y, PILL_W, PILL_H, key[2:], i))
    for i in range(PILL_COLS * PAD_ROWS):   # the selected band's ring, over its pill
        r, c = divmod(i, PILL_COLS)
        L.append('art file="art/ring.png" x=%d y=%d w=%d h=%d when=pp_sel:%d'
                 % (cell0[0] + c * PITCH_X, cell0[1] + r * PITCH_Y, PILL_W, PILL_H, i))
    # ... and the pills' buttons over everything: tap to select the band, hold and drag to move it (src/peq.c touch_*)
    for b, (_, key) in enumerate(BANDS):
        for i in range(PILL_COLS * PAD_ROWS):
            r, c = divmod(i, PILL_COLS)
            L.append('button cx=%d cy=%d w=%d h=%d label="" key=pick_%s img=art/pill_clear.png img_on=art/pill_clear.png when=pp_%s:%d'
                     % (cell0[0] + c * PITCH_X + PILL_W // 2, cell0[1] + r * PITCH_Y + PILL_H // 2, PILL_W, PILL_H,
                        key[2:], key[2:], i))
    btn = [ba + (bb - ba) * k // 8 for k in (1, 3, 5, 7)]   # four buttons evenly under the band selector
    L += [# the other drag areas: thin sliders here, grown by skin_post.py to the readout boxes (the analyser strip's
          # left/right frequency drag went with the strip in 0.15.0: a pill drag moves the frequency). The GAIN box drives
          # g_rel, the relative gain control (fine steps, like its Q-Link)
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
          'knob cx=%d cy=%d r=32 label="Q" key=sel_q bw=120' % (qx, r1 - 6),
          'enum_h cx=%d cy=%d label="" key=sel_slope sw=40 when=sel_band:HP' % (qx, y0 + 154),
          'enum_h cx=%d cy=%d label="" key=sel_slope sw=40 when=sel_band:LP' % (qx, y0 + 154),
          # 4. output: on Q's row, the same size, so the two knobs line up
          'knob cx=%d cy=%d r=32 label="OUT" key=out_gain bw=120' % (ox, r1 - 6),
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

                         ["shSlider_h_%dx8" % freq_box[2], list(freq_box)],
                         ["shSlider_v_8x%d" % gain_box[3], list(gain_box)]],
               "stems": {"m_sel": "stem_sel.png"},
               # images per pill cell (option o of the parameter is cell o): each band's pill and the selected band's ring
               # over it. The pills' buttons (invisible, over them) the builder places itself
               "pills": dict([("pp_sel", "ring.png")] + [("pp_" + key[2:], "pill_%d.png" % b) for b, (_, key) in enumerate(BANDS)]),
               "cells": {"x": cell0[0], "y": cell0[1], "pitch_x": PITCH_X, "pitch_y": PITCH_Y, "w": PILL_W, "h": PILL_H,
                         "cols": PILL_COLS},
               # pictures whose frames become transparent PNGs over exactly their rect (the builder bakes them opaque, so
               # a lane's high bracket would hide its low one, and the curve the spectrum): the effects' brackets, the
               # curve's columns and the spectrum's (0.15.0)
               "lanes": dict(
                   [("fxm_%s_%s" % (k, side), {"rect": [GRAPH_X, lane_y(e), GRAPH_W, LANE_H],
                                               "file": "fxm_%s_%s_%%02d.png" % (k, side), "frames": FXM_COLS + 1})
                    for e, (k, _, _, _, _) in enumerate(FX) for side in ("lo", "hi")] +
                   [("c%02d" % i, {"rect": [GRAPH_X + i * COL_W, GRAPH_Y, COL_W, GRAPH_H], "file": "col_%03d.png",
                                   "frames": LEVELS * TILTS}) for i in range(NCOL)] +
                   [("w%02d" % i, {"rect": [GRAPH_X + i * WCOL_W, GRAPH_Y, WCOL_W, GRAPH_H], "file": "wave_%03d.png",
                                   "frames": WFRAMES}) for i in range(WCOL)])},
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
    divider()
    fx_bg()
    css()
    # the drag areas' strip and the pills' touch areas: near-invisible, alpha 1 (a fully transparent image may not take
    # the touch)
    Image.new("RGBA", (4, 8), (0, 0, 0, 1)).save(os.path.join(HERE, "clear.png"))
    Image.new("RGBA", (PILL_W, PILL_H), (0, 0, 0, 1)).save(os.path.join(HERE, "pill_clear.png"))
    y0, h, gap = layout()
    print("art: %d columns x %d levels, %d bands on %d pill columns, %d effects, analyser %dx%d, layout.conf (panel y=%d h=%d)"
          % (NCOL, LEVELS, len(BANDS), PILL_COLS, len(FX), WCOL, WLEVELS, y0, h))
