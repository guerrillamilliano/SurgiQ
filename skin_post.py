#!/usr/bin/env python3
"""vst.json "skin_post": touch-ups of the built skin that the layout can't express. Run by gen_vst.py as
`python3 skin_post.py <skin dir>`; fails when a target is missing. Geometry comes from art/geometry.json (make_art.py).

1. The drag areas. MPC's sliders are FilmStrip Knobs, and MPC decodes every filmstrip frame into /var/tmp each time the
   screen opens, so a big slider would cost tens of MB per open. The layout asks for thin sliders instead (cheap to
   build), grown here to their areas (geometry.json "drags": the analyser and the FREQUENCY box for sel_freq, the GAIN
   box for sel_gain), pointed at a 2-frame near-invisible strip, without their value text, and their big strips deleted.
2. The band indicators. The builder bakes each when= image opaque (the background redrawn under it), which would blank
   the curve behind a band's stem. Their mode images are replaced by the stem PNGs themselves (transparent around the
   stem and pill; MPC honours PNG alpha), at exactly their column."""
import json
import os
import re
import shutil
import sys

STRIP = "peq_drag.png"
HERE = os.path.dirname(os.path.abspath(__file__))
geo = json.load(open(os.path.join(HERE, "art", "geometry.json")))
keys = [p["key"] for p in json.load(open(os.path.join(HERE, "params.json")))["params"]]


def skin_rect(r):   # layout coordinates -> skin coordinates (the plugin area starts at y = 86)
    return (r[0], r[1] - 86, r[2], r[3])


skins = os.path.join(sys.argv[1], "Plugin Skins")
path = os.path.join(skins, "TUI.json")
tui = json.load(open(path))
local = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
placed_all = [c for d in local if isinstance(d.get("value"), dict) for c in d["value"].get("componentsData", [])]

# 1. drag areas
removed = set()
for prefix, r in geo["drags"]:
    rect = skin_rect(r)
    defs = [d for d in local if d["key"].startswith(prefix + "_") or d["key"] == prefix]
    if len(defs) != 1:
        sys.exit("skin_post: expected one %s* definition, found %d" % (prefix, len(defs)))
    d = defs[0]
    x, y, w, h = rect
    kept = []
    for c in d["value"]["componentsData"]:
        cd = c["componentData"]
        if cd["type"] == "Label":   # the value (and any name) text under the slider
            continue
        if cd["type"] == "Knob":
            removed.add(cd["data"]["filmStrip"])
            cd["data"]["filmStrip"] = STRIP
            cd["data"]["numFrames"] = 2
        c["bounds"]["bounds"] = "0 0 %d %d" % (w, h)
        kept.append(c)
    d["value"]["componentsData"] = kept
    placed = [c for c in placed_all if c["componentData"]["type"] == d["key"]]
    if len(placed) != 1:
        sys.exit("skin_post: expected %s placed once, found %d" % (d["key"], len(placed)))
    placed[0]["bounds"]["bounds"] = "%d %d %d %d" % (x, y, w, h)

# 2. band indicators: Image components that show while a marker parameter (m_*) is at a column
gx, gy, _, gh = skin_rect(geo["graph"])
cw = geo["col_w"]
stem_of = {keys.index(k): f for k, f in geo["stems"].items()}
n = 0
for c in placed_all:
    hs = c["bounds"].get("additionalInvalidatingHandles") or []
    m = re.match(r"IndexedEnabling/(\d+)/\d+/Parameter (\d+)$", hs[0]) if len(hs) == 1 else None
    if c["componentData"]["type"] != "Image" or not m or int(m.group(2)) not in stem_of:
        continue
    removed.add(c["componentData"]["data"]["image"])
    c["componentData"]["data"]["image"] = stem_of[int(m.group(2))]
    c["bounds"]["bounds"] = "%d %d %d %d" % (gx + int(m.group(1)) * cw, gy, cw, gh)
    n += 1
if n != len(stem_of) * 48:   # every (band, column) pair, the selected one's included
    sys.exit("skin_post: expected %d band indicator images, found %d" % (len(stem_of) * 48, n))

# 2b. each band's pill: an image per grid cell (pp_xx option o = cell o), transparent around the pill, at exactly its cell
cells = geo["cells"]
pill_of = {keys.index(k): f for k, f in geo["pills"].items()}
npill = 0
for c in placed_all:
    hs = c["bounds"].get("additionalInvalidatingHandles") or []
    m = re.match(r"IndexedEnabling/(\d+)/\d+/Parameter (\d+)$", hs[0]) if len(hs) == 1 else None
    if c["componentData"]["type"] != "Image" or not m or int(m.group(2)) not in pill_of:
        continue
    o = int(m.group(1))
    removed.add(c["componentData"]["data"]["image"])
    c["componentData"]["data"]["image"] = pill_of[int(m.group(2))]
    c["bounds"]["bounds"] = "%d %d %d %d" % (cells["x"] + (o % cells["cols"]) * cells["pitch_x"],
                                             cells["y"] - 86 + (o // cells["cols"]) * cells["pitch_y"], cells["w"], cells["h"])
    npill += 1
want_pills = len(pill_of) * cells["cols"] * 11
if npill != want_pills:
    sys.exit("skin_post: expected %d pill images, found %d" % (want_pills, npill))

# 2c. the effects' range brackets (pictures: an image per frame): each frame's transparent PNG, over exactly its lane
lanes = {keys.index(k): v for k, v in geo.get("lanes", {}).items()}
nlane, lane_files = 0, set()
for c in placed_all:
    hs = c["bounds"].get("additionalInvalidatingHandles") or []
    m = re.match(r"IndexedEnabling/(\d+)/\d+/Parameter (\d+)$", hs[0]) if len(hs) == 1 else None
    if c["componentData"]["type"] != "Image" or not m or int(m.group(2)) not in lanes:
        continue
    lane = lanes[int(m.group(2))]
    f = lane["file"] % int(m.group(1))
    removed.add(c["componentData"]["data"]["image"])
    c["componentData"]["data"]["image"] = f
    c["bounds"]["bounds"] = "%d %d %d %d" % skin_rect(lane["rect"])
    lane_files.add(f)
    nlane += 1
want_lanes = sum(v["frames"] for v in lanes.values())
if nlane != want_lanes:
    sys.exit("skin_post: expected %d bracket images, found %d" % (want_lanes, nlane))

# 3. a tap grid, if the layout has one (0.7.0-0.10.0; geometry.json "pad"): each cell is an option button the builder
# drew opaque (with its option number on it); it becomes a near-invisible cell, and the selected one the node ring
pad = geo.get("pad")
ncell = 0
for d in local if pad else []:
    if not d["key"].startswith("shSeg_%s_" % pad["key"]):
        continue
    for c in d["value"]["componentsData"]:
        if c["componentData"]["type"] == "Button":
            data = c["componentData"]["data"]
            removed.update((data["onImage"], data["offImage"]))
            data["onImage"], data["offImage"] = pad["on"], pad["off"]
            ncell += 1
if pad and ncell == 0:
    sys.exit("skin_post: no tap grid cells (shSeg_%s_*)" % pad["key"])

json.dump(tui, open(path, "w"), indent=1)
shutil.copy(os.path.join(HERE, "art", "clear.png"), os.path.join(skins, STRIP))   # 4x8 = two 4x4 frames
keep = set(geo["stems"].values()) | set(geo["pills"].values()) | lane_files | ({pad["on"], pad["off"]} if pad else set())
for f in removed - keep:
    if os.path.exists(os.path.join(skins, f)):
        os.remove(os.path.join(skins, f))
for f in keep:
    shutil.copy(os.path.join(HERE, "art", f), os.path.join(skins, f))
print("skin_post: %d drag areas, %d stem images, %d pill images, %d bracket images, %d tap grid cells"
      % (len(geo["drags"]), n, npill, nlane, ncell))
