#!/bin/sh
# Offline look at a built SurgiQ screen with real engine values. Run from the repo root in the python:3.11-slim image
# (installs gcc and Pillow there):
#   docker run --rm -v <repo>:/r -w /r python:3.11-slim sh ports/peq/test/preview.sh [<Plugin Skins dir> [<name>]]
# Default: the last build's skin -> ports/peq/build/preview_demo.png (EQ tab) and preview_1.png (FX tab); with a
# name, preview_<name>.png and preview_<name>_options.png instead.
set -e
apt-get -qq update >/dev/null 2>&1
apt-get -qq install -y gcc libc6-dev >/dev/null 2>&1
pip install -q --target /tmp/p pillow >/dev/null 2>&1
S="${1:-ports/peq/build/skin/Milliano - VST - SurgiQ/Plugin Skins}"
N="${2:-}"
OUT=ports/peq/build
# studio.py preview also renders a page for every other option of every picture (thousands here). A temporary copy
# renders those mode pages only for the real panel switches (sel_band, sel_on, sel_slope, wave_view, wave_time, spec_on,
# spec_rate: indices 16, 78, 79, 103, 153, 154, 155).
sed 's/for i in range(1, n_):/for i in (range(1, n_) if p in (16, 78, 79, 103, 153, 154, 155) else ()):/' tools/studio.py > /tmp/studio_fast.py
cp tools/*.py /tmp/ 2>/dev/null; cp /tmp/studio_fast.py /tmp/studio_fast_run.py
PYTHONPATH=/tmp/p:tools python3 /tmp/studio_fast_run.py preview "$S" -o /tmp/pv_%d.png >/dev/null
gcc -O2 -Iwrapper ports/peq/test/demo_values.c ports/peq/src/peq.c -lm -o /tmp/demo
/tmp/demo > /tmp/v.txt
if [ -n "$N" ]; then
    cp /tmp/pv_1.png "$OUT/preview_${N}_options.png" 2>/dev/null || true
    PYTHONPATH=/tmp/p python3 ports/peq/test/composite.py "$S" /tmp/pv_0.png "$OUT/preview_$N.png" < /tmp/v.txt
else
    cp /tmp/pv_0.png "$OUT/preview_0.png"
    cp /tmp/pv_1.png "$OUT/preview_1.png" 2>/dev/null || true   # the FX tab
    PYTHONPATH=/tmp/p python3 ports/peq/test/composite.py "$S" "$OUT/preview_0.png" "$OUT/preview_demo.png" < /tmp/v.txt
fi
