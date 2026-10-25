#!/usr/bin/env python3
"""Offline look at the EQ screen with real engine values: pastes the skin's mode images (curve columns, markers) and the
"on" image of selected option buttons (the tap grid's node ring) chosen by "<key> <option>" lines on stdin onto studio.py's
preview_0.png (whose pictures all sit at option 0).
    composite.py <Plugin Skins dir> <preview_0.png> <out.png> < values.txt   (needs Pillow; run from the repo root)"""
import json
import os
import re
import sys

from PIL import Image

skins, preview, out = sys.argv[1:4]
index = {p["key"]: i for i, p in enumerate(json.load(open(os.path.join(os.path.dirname(__file__), "..", "params.json")))["params"])}
want = {}
for line in sys.stdin:
    if line.strip():
        k, o = line.split()
        want[index[k]] = int(o)
tui = json.load(open(os.path.join(skins, "TUI.json")))
local = tui["pageData"]["componentDefinitions"]["localComponentDefinitions"]
defs = {d["key"]: d["value"] for d in local}
page = next(d for d in local
            if isinstance(d.get("value"), dict) and any(c["componentData"]["type"] == "Image" for c in d["value"].get("componentsData", [])))
img = Image.open(preview).convert("RGBA")
n = 0
for c in page["value"]["componentsData"]:
    cd = c["componentData"]
    hs = c["bounds"].get("additionalInvalidatingHandles") or []
    if cd["type"] != "Image" or not hs:
        continue
    m = re.match(r"IndexedEnabling/(\d+)/\d+/Parameter (\d+)", hs[0])
    if not m or want.get(int(m.group(2))) != int(m.group(1)):
        continue
    x, y, w, h = map(int, c["bounds"]["bounds"].split())
    part = Image.open(os.path.join(skins, cd["data"]["image"])).convert("RGBA")
    img.alpha_composite(part, (x, y))
    n += 1
for c in page["value"]["componentsData"]:   # option buttons (radio groups): the selected one shows its "on" image
    d = defs.get(c["componentData"]["type"])
    remap = c.get("handle remapping", {}).get("map") or []
    m = re.match(r"Parameter (\d+)$", remap[0]["value"]) if remap else None
    if not d or not m or int(m.group(1)) not in want:
        continue
    for b in d.get("componentsData", []):
        bd = b["componentData"]
        if bd["type"] == "Button" and bd["data"].get("numButtonsInGroup", 1) > 1 and bd["data"]["buttonId"] == want[int(m.group(1))]:
            x, y = map(int, c["bounds"]["bounds"].split()[:2])
            img.alpha_composite(Image.open(os.path.join(skins, bd["data"]["onImage"])).convert("RGBA"), (x, y))
            n += 1
img.save(out)
print("pasted %d mode images -> %s" % (n, out))
