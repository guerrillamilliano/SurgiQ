#!/usr/bin/env python3
"""Writes params.json for the Parametric EQ (run it after changing anything here: python3 ports/peq/gen_params.py).

VST indices are the list order and saved projects bind to them, so keys are only ever appended (params.lock checks it).
Ranges and option counts may still change while the plugin is unreleased. Must agree with src/peq.c (keys, ranges,
defaults) and art/make_art.py (NCOL, LEVELS, WCOL, WLEVELS)."""
import json
import math
import os

NCOL, LEVELS = 48, 37          # curve columns; curve levels (-18..+18 dB in 1 dB, 18 = 0 dB)
TILTS, TILT_FLAT = 9, 4         # since 0.16.0 a column is a sloped segment: a level and a tilt (src/peq.c TILT; 9 since 0.17.0)
WCOL, WLEVELS = 96, 49         # 0.15.0-0.17.0's spectrum: columns, steps (since 0.18.0 w48..w95 are unused)
SCOL, SFRAMES = 48, 25 * 9     # since 0.18.0: the spectrum's columns (the curve's) and frames (25 levels x 9 tilts, src/peq.c WTILT)
GAIN = 24.0                    # band gain range, dB
BANDS = ["HP", "LS", "1", "2", "3", "4", "5", "6", "7", "HS", "LP"]   # selector order (src/peq.c B_*; 6 and 7 since 0.17.0)


def pos_hz(hz):   # frequency knob position (0..1, log 20 Hz..20 kHz)
    return round(math.log(hz / 20.0) / math.log(1000.0), 4)


def pos_q(q):     # Q knob position (0..1, log 0.3..12)
    return round(math.log(q / 0.3) / math.log(40.0), 4)


def freq(key, name, hz, pos=None):   # pos: the default as a knob position instead (the peaks' even spread, 0.18.0)
    return {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": pos if pos is not None else pos_hz(hz) if hz else 0.0,
            "dynamic_display": True}


def gain(key, name):
    return {"key": key, "name": name, "min": -GAIN, "max": GAIN, "default": 0.0, "unit": "dB"}


def q(key, name, v=1.0):
    return {"key": key, "name": name, "min": 0.0, "max": 1.0, "default": pos_q(v), "dynamic_display": True}


def opts(key, name, options, default):
    return {"key": key, "name": name, "options": options, "default": default}


positions = [str(i) for i in range(NCOL)] + ["off"]


def curve(i):   # a curve column: option = level * TILTS + tilt's index, flat at 0 dB by default
    return opts("c%02d" % i, "Curve %d" % i, [str(v) for v in range(LEVELS * TILTS)], (LEVELS // 2) * TILTS + TILT_FLAT)
params = [
    # 0-15: the first release's parameters, in their original order
    freq("hp_freq", "HP Freq", 0), freq("ls_freq", "Low Freq", 100), gain("ls_gain", "Low Gain"),
    freq("p1_freq", "Band 1 Freq", 0, 0.125), gain("p1_gain", "Band 1 Gain"), q("p1_q", "Band 1 Q"),
    freq("p2_freq", "Band 2 Freq", 0, 0.25), gain("p2_gain", "Band 2 Gain"), q("p2_q", "Band 2 Q"),
    freq("p3_freq", "Band 3 Freq", 0, 0.375), gain("p3_gain", "Band 3 Gain"), q("p3_q", "Band 3 Q"),
    freq("hs_freq", "High Freq", 8000), gain("hs_gain", "High Gain"),
    dict(freq("lp_freq", "LP Freq", 0), default=1.0), {"key": "out_gain", "name": "Output", "min": -24.0, "max": 12.0, "default": 0.0, "unit": "dB"},
    # 16-19: the selected band (the screen and the Q-Links edit this)
    dict(opts("sel_band", "Band", BANDS, 4), refresh_display=True),
    freq("sel_freq", "Freq", 1000), {"key": "sel_gain", "name": "Gain", "min": -GAIN, "max": GAIN, "default": 0.0, "dynamic_display": True},
    q("sel_q", "Q"),
]
params += [opts(k, n, positions, NCOL) for k, n in
           (("m_hp", "HP Marker"), ("m_ls", "LS Marker"), ("m_p1", "B1 Marker"), ("m_p2", "B2 Marker"), ("m_p3", "B3 Marker"),
            ("m_hs", "HS Marker"), ("m_lp", "LP Marker"))]                                         # 20-26
params += [curve(i) for i in range(32)]   # 27-58
# 59 on: added in 0.3.0
params += [freq("p4_freq", "Band 4 Freq", 0, 0.5), gain("p4_gain", "Band 4 Gain"), q("p4_q", "Band 4 Q"),
           freq("p5_freq", "Band 5 Freq", 0, 0.625), gain("p5_gain", "Band 5 Gain"), q("p5_q", "Band 5 Q"),
           q("hp_q", "HP Resonance", 0.7071), q("lp_q", "LP Resonance", 0.7071),
           opts("hp_slope", "HP Slope", ["12", "24", "48"], 0), opts("lp_slope", "LP Slope", ["12", "24", "48"], 0)]
# since 0.18.0 the HP, LS, HS and LP start off (the peaks on)
params += [opts("on_" + b, b.upper() + " On", ["OFF", "ON"], 0 if b in ("hp", "ls", "hs", "lp") else 1)
           for b in ("hp", "ls", "p1", "p2", "p3", "p4", "p5", "hs", "lp")]
params += [opts("sel_on", "Band On", ["OFF", "ON"], 1), opts("sel_slope", "Slope", ["12", "24", "48"], 0),
           {"key": "band_prev", "name": "Prev Band", "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True,
            "step_of": "sel_band", "step_delta": -1},
           {"key": "band_next", "name": "Next Band", "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True,
            "step_of": "sel_band", "step_delta": 1},
           {"key": "band_reset", "name": "Reset Band", "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True},
           opts("m_p4", "B4 Marker", positions, NCOL), opts("m_p5", "B5 Marker", positions, NCOL),
           opts("m_sel", "Selected Marker", positions, NCOL)]
params += [curve(i) for i in range(32, NCOL)]
# 102 on: added in 0.4.0, the captured waveform
params += [{"key": "wave_cap", "name": "Capture", "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True},
           opts("wave_view", "Wave View", ["PRE", "POST"], 1),
           {"key": "wave_status", "name": "Wave Status", "min": 0.0, "max": 1.0, "default": 0.0, "display": "string"}]
params += [opts("w%02d" % i, "Wave %d" % i, [str(v) for v in range(SFRAMES)], 0) for i in range(SCOL)]
# 153 on: added in 0.5.0, the live spectrum analyser. wNN are its columns, wave_view its PRE/POST, wave_time its fall-off;
# wave_cap / wave_status above are unused since then (kept so no index moves)
params += [opts("wave_time", "Analyser Speed", ["SLOW", "MED", "FAST"], 1),
           opts("spec_on", "Analyser", ["OFF", "ON"], 1)]
# 155 on: added in 0.6.0
params += [opts("spec_rate", "Analyser Rate", ["LOW", "MED", "HIGH"], 1)]   # the screen's budget of changes a second (0.18.3; 4/8/15 and 8/15/30 updates a second before)
# 156 on: added in 0.7.0, the tap grid over the graph: option = row * PAD_COLS + column; a tap moves the selected band
# there (rows every 3 dB, +15 at the top, 0 dB in the middle), or on another band's pill (0 dB row) selects that band
PAD_COLS, PAD_ROWS = NCOL, 11
params += [dict(opts("pad", "Band Position", [str(i) for i in range(PAD_COLS * PAD_ROWS)], 5 * PAD_COLS + 27),
                refresh_display=True)]

# 157 on: added in 0.9.0. The Q-Link versions of the short option lists: continuous knobs split into zones (src/peq.c
# QKEYS), so a Q-Link registers every event; and each band's pill: its tap-grid cell (the last option: not shown)
params += [{"key": k, "name": n, "min": 0.0, "max": 1.0, "default": 0.5, "dynamic_display": True} for k, n in
           (("q_view", "Analyser PRE/POST"), ("q_speed", "Analyser Speed"), ("q_rate", "Analyser Rate"),
            ("q_spec", "Analyser On"), ("q_on", "Band On"), ("q_slope", "Filter Slope"))]
PILL_COLS = 24   # since 0.12.0 the pills (and the ring) sit on every other column: option = row * PILL_COLS + column
pill_cells = [str(i) for i in range(PILL_COLS * PAD_ROWS)] + ["off"]
params += [opts("pp_" + b, b.upper() + " Pill", pill_cells, PILL_COLS * PAD_ROWS)
           for b in ("hp", "ls", "p1", "p2", "p3", "p4", "p5", "hs", "lp")]
# 172 on: added in 0.10.0. The relative gain control (the GAIN box's drag and its Q-Link): nudges the selected band's gain
params += [{"key": "g_rel", "name": "Gain (fine)", "min": 0.0, "max": 1.0, "default": 0.5, "dynamic_display": True}]
# 173 on: added in 0.11.0. The selected band's ring (a cell, like the pills); theme_* are unused since 0.12.0 (one skin)
params += [opts("pp_sel", "Selected Ring", pill_cells, PILL_COLS * PAD_ROWS),
           opts("theme_sel", "Theme", ["MONITOR", "LAVENDER", "MIDNIGHT"], 0),
           {"key": "theme_apply", "name": "Apply Theme", "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True},
           {"key": "theme_status", "name": "Theme Status", "min": 0.0, "max": 1.0, "default": 0.0, "display": "string"}]
# 177 on: added in 0.12.0, the FX tab: reverb, delay, chorus and shimmer as sends after the EQ (src/peq.c FXP order:
# on, mix, two settings, FULL/BAND, band low, band high)
FX = [("rev", "Reverb", ("size", "Size", 0.6), ("damp", "Damp", 0.4)),
      ("dly", "Delay", ("time", "Time", 0.5), ("fb", "Feedback", 0.35)),
      ("cho", "Chorus", ("rate", "Rate", 0.4), ("depth", "Depth", 0.5)),
      ("shm", "Shimmer", ("size", "Size", 0.7), ("amt", "Amount", 0.5))]
for k, n, (k1, n1, d1), (k2, n2, d2) in FX:
    p = "fx_" + k + "_"
    params += [opts(p + "on", n + " On", ["OFF", "ON"], 0),
               {"key": p + "mix", "name": n + " Mix", "min": 0.0, "max": 100.0, "default": 30.0, "dynamic_display": True},
               {"key": p + k1, "name": n + " " + n1, "min": 0.0, "max": 1.0, "default": d1, "dynamic_display": True},
               {"key": p + k2, "name": n + " " + n2, "min": 0.0, "max": 1.0, "default": d2, "dynamic_display": True},
               opts(p + "full", n + " Range", ["FULL", "BAND"], 0),
               freq(p + "lo", n + " Low", 100), freq(p + "hi", n + " High", 8000)]
# each effect's range brackets on the graph: a column, the last option hidden
FXM_COLS = 24
params += [opts("fxm_%s_%s" % (k, s), "%s %s Mark" % (n, s.title()), [str(i) for i in range(FXM_COLS)] + ["off"], FXM_COLS)
           for k, n, _, _ in FX for s in ("lo", "hi")]
# 213 on: added in 0.12.1. The pills are buttons: a tap on one selects its band (momentary)
params += [{"key": "pick_" + b, "name": "Select " + b.upper(), "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}
           for b in ("hp", "ls", "p1", "p2", "p3", "p4", "p5", "hs", "lp")]
# 222-269: added in 0.13.0, the analyser's columns 48..95 (below)
# 222 on: added in 0.13.0. The analyser's columns 48..95: twice as many, half as wide (w00..w47 keep their indices)
params += [opts("w%02d" % i, "Wave %d" % i, [str(v) for v in range(WLEVELS)], 0) for i in range(48, WCOL)]
# 270 on: added in 0.17.0, peak bands 6 and 7 (flat; since 0.18.0 at 3.6 and 8.4 kHz, the peaks' even spread): settings, on/off, pill cell and
# pill tap, in the order the other bands' came
params += [freq("p6_freq", "Band 6 Freq", 0, 0.75), gain("p6_gain", "Band 6 Gain"), q("p6_q", "Band 6 Q"),
           freq("p7_freq", "Band 7 Freq", 0, 0.875), gain("p7_gain", "Band 7 Gain"), q("p7_q", "Band 7 Q"),
           opts("on_p6", "P6 On", ["OFF", "ON"], 1), opts("on_p7", "P7 On", ["OFF", "ON"], 1)]
params += [opts("pp_" + b, b.upper() + " Pill", pill_cells, PILL_COLS * PAD_ROWS) for b in ("p6", "p7")]
params += [{"key": "pick_" + b, "name": "Select " + b.upper(), "min": 0.0, "max": 1.0, "default": 0.0, "momentary": True}
           for b in ("p6", "p7")]
HERE =os.path.dirname(os.path.abspath(__file__))
with open(os.path.join(HERE, "params.json"), "w") as f:
    f.write('{\n "name": "SurgiQ",\n "params": [\n')
    f.write(",\n".join("  " + json.dumps(p) for p in params))
    f.write("\n ]\n}\n")
print("params.json: %d params" % len(params))

# ---- presets.json: MPC's PRESET menu. Every preset sets all of the EQ's own parameters (so it starts from flat), in
# real units here: Hz, dB, Q, slope in dB/oct; unnamed bands stay flat and on.
REAL = ["hp_freq", "ls_freq", "ls_gain", "p1_freq", "p1_gain", "p1_q", "p2_freq", "p2_gain", "p2_q", "p3_freq", "p3_gain",
        "p3_q", "hs_freq", "hs_gain", "lp_freq", "out_gain", "p4_freq", "p4_gain", "p4_q", "p5_freq", "p5_gain", "p5_q",
        "p6_freq", "p6_gain", "p6_q", "p7_freq", "p7_gain", "p7_q",
        "hp_q", "lp_q", "hp_slope", "lp_slope", "on_hp", "on_ls", "on_p1", "on_p2", "on_p3", "on_p4", "on_p5", "on_hs", "on_lp",
        "on_p6", "on_p7"]
by_key = {p["key"]: p for p in params}


def preset(name, **bands):
    """bands: hp=(Hz, slope[, Q]), lp=(Hz, slope[, Q]), ls/hs=(Hz, dB), p1..p7=(Hz, dB, Q), out=dB"""
    v = {k: by_key[k]["default"] for k in REAL}
    for k in REAL:   # option params take their label
        if by_key[k].get("options"):
            v[k] = by_key[k]["options"][int(v[k])]
    for b in ("hp", "lp"):
        if b in bands:
            hz, slope = bands[b][:2]
            v[b + "_freq"] = pos_hz(hz)
            v[b + "_slope"] = str(slope)
            if len(bands[b]) > 2:
                v[b + "_q"] = pos_q(bands[b][2])
    for b in ("ls", "hs"):
        if b in bands:
            v[b + "_freq"], v[b + "_gain"] = pos_hz(bands[b][0]), float(bands[b][1])
    for b in ("p1", "p2", "p3", "p4", "p5", "p6", "p7"):
        if b in bands:
            hz, db, qv = bands[b]
            v[b + "_freq"], v[b + "_gain"], v[b + "_q"] = pos_hz(hz), float(db), pos_q(qv)
    if "out" in bands:
        v["out_gain"] = float(bands["out"])
    for b in bands:   # a band the preset uses is on (since 0.18.0 the HP, LS, HS and LP start off)
        if "on_" + b in v:
            v["on_" + b] = "ON"
    return {"name": name, "values": v}


presets = [
    preset("Init (Flat)"),
    # drums
    preset("Kick - Punch", hp=(30, 24), p1=(60, 4, 1.0), p2=(250, -3, 1.2), p3=(400, -4, 1.6), p4=(3500, 4, 1.2), hs=(10000, -2)),
    preset("Kick - Sub Boom", hp=(25, 24), ls=(55, 4), p1=(90, 2, 0.8), p2=(300, -5, 1.0), p4=(2000, -2, 1.0), hs=(8000, -3),
           lp=(12000, 12), out=-2),
    preset("Kick - Click", hp=(35, 24), p1=(70, 2, 1.0), p2=(350, -4, 1.4), p4=(4500, 5, 1.4), p5=(9000, 2, 1.0)),
    preset("Snare - Crack", hp=(80, 12), p1=(200, 3, 1.2), p2=(450, -3, 2.0), p3=(900, -2, 2.0), p4=(5000, 4, 1.0), hs=(10000, 2)),
    preset("Snare - Fat Body", hp=(70, 12), p1=(180, 4, 1.0), p2=(600, -3, 1.5), p4=(3000, 2, 1.0), hs=(12000, -2)),
    preset("Clap - Bright", hp=(150, 24), p2=(500, -3, 1.2), p4=(2500, 3, 1.0), hs=(9000, 3)),
    preset("Hi-Hats - Crisp", hp=(300, 24), p3=(1000, -3, 1.0), p4=(6000, 2, 1.0), hs=(10000, 3)),
    preset("Hi-Hats - Tame Harsh", hp=(250, 24), p4=(3500, -3, 1.5), p5=(8000, -2, 2.0), hs=(12000, 1)),
    preset("Toms - Full", hp=(60, 12), p1=(100, 3, 1.0), p2=(400, -4, 1.5), p4=(4000, 3, 1.2)),
    preset("Cymbals - Air", hp=(200, 24), p3=(2000, -2, 1.0), hs=(12000, 3)),
    preset("Drum Bus - Punch", hp=(30, 12), ls=(80, 2), p2=(400, -2, 0.8), p4=(4000, 2, 0.7), hs=(12000, 2), out=-1),
    preset("808 - Tight", hp=(30, 24), ls=(55, 3), p2=(250, -3, 1.0), p4=(1500, 2, 1.0), lp=(8000, 12), out=-2),
    # instruments
    preset("Bass - Definition", hp=(35, 24), p1=(80, 2, 1.0), p2=(250, -3, 1.0), p3=(900, 3, 1.2), lp=(6000, 12)),
    preset("Vocal - Presence", hp=(100, 24), p1=(200, -2, 1.0), p2=(400, -2, 1.5), p4=(3500, 3, 1.0), p5=(7000, -2, 3.0),
           hs=(12000, 3)),
    preset("Vocal - Warm", hp=(80, 12), ls=(150, 2), p4=(3000, 1, 1.0), p5=(6500, -3, 3.0), hs=(10000, -1)),
    preset("Piano - Clarity", hp=(60, 12), p2=(300, -2, 1.0), p4=(2500, 2, 1.0), hs=(10000, 2)),
    preset("Guitar - Bite", hp=(90, 12), p2=(250, -2, 1.0), p4=(2500, 3, 1.0), lp=(10000, 12)),
    preset("Pad - Airy", hp=(120, 12), p2=(400, -3, 0.8), hs=(10000, 4), out=-1),
    preset("Synth Lead - Cut", hp=(150, 12), p2=(350, -2, 1.0), p4=(3000, 3, 1.0), hs=(9000, 2)),
    preset("Lo-Fi - Telephone", hp=(300, 24), p3=(1500, 4, 1.0), lp=(3500, 24, 1.5), out=-3),
    preset("Mix - Gentle Smile", ls=(80, 1.5), p3=(1000, -1, 0.7), hs=(10000, 1.5)),
    # basic filter shapes, each on its own (SurgiQ has no notch or band-pass type: Notch is a deep narrow peak cut,
    # Band Pass a high-pass and a low-pass two octaves apart)
    preset("Filter - High Pass", hp=(120, 24)),
    preset("Filter - Low Pass", lp=(6000, 24)),
    preset("Filter - Bell", p3=(1000, 6, 1.0)),
    preset("Filter - Notch", p3=(1000, -24, 10.0)),
    preset("Filter - Low Shelf", ls=(150, 6)),
    preset("Filter - High Shelf", hs=(6000, 6)),
    preset("Filter - Band Pass", hp=(500, 24), lp=(2000, 24)),
]
with open(os.path.join(HERE, "presets.json"), "w") as f:
    json.dump({"presets": presets}, f, indent=1)
print("presets.json: %d presets" % len(presets))
