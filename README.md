# SurgiQ

A visual 9-band parametric EQ with effects, made as a native plugin for the **Akai MPC Live II** (MPC OS 3.x, armv7).

- **Live EQ curve** on a log frequency graph, with a numbered pill for each band (high-pass, low shelf, five peaks,
  high shelf, low-pass) at its frequency and gain. Tap a pill to select its band, then drag on the graph for gain or
  on the analyser for frequency. All of it is also on the Q-Links.
- **Filters:** 12, 24 or 48 dB/oct high- and low-pass with resonance. Every band can be switched off.
- **Live spectrum analyser** (PRE or POST), styled after FL Studio's Parametric EQ 2.
- **FX tab:** reverb, delay, chorus and shimmer as sends after the EQ. Each has ON, MIX and two settings, and runs on
  the whole signal (FULL) or on one frequency band (BAND). Each active effect's range shows as colored brackets on the
  EQ graph.
- **29 presets** for drums, instruments and basic filter shapes, in MPC's PRESET menu.
- White and purple look, built for the MPC's touchscreen.

## Install

Everything needed to install is in [`install/`](install):

- `install/SurgiQ-0.12.1-mpc-armv7.zip`: the installer package (also on the [Releases](../../releases) page)
- `install/SurgiQ-0.12.1/`: the same package unpacked:
  - `install.sh` and `uninstall.sh`
  - `INSTALL.md`
  - `SHA256SUMS`
  - `portable/Milliano - VST - SurgiQ/` (the plugin `peq.so` and its skin)

Copy the zip to the MPC (for example with `scp`), then on the MPC as root:

```sh
unzip SurgiQ-0.12.1-mpc-armv7.zip && cd SurgiQ-0.12.1
sha256sum -c SHA256SUMS              # optional: check the files
sh install.sh -t /media/SSD/Synths   # or /sdcard/Synths; this restarts MPC (save your project first)
```

To remove it: `sh uninstall.sh -t /media/SSD/Synths` from the same folder.

Plugins on an external drive need a drive-exec patch (for example "Auto exec" from the mpc-vst-plugins patches).

## Build

SurgiQ is a port for [sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (MIT), which provides the
VST2 wrapper, skin builder and toolchain. It is not included here.

1. Clone mpc-vst-plugins and copy this repo's files (except `windows/`) into `ports/peq/`.
2. After changing parameters or the screen, regenerate:
   `python3 ports/peq/gen_params.py` and `python3 ports/peq/art/make_art.py` (needs Pillow).
3. Build: `tools/build_port.sh ports/peq/vst.json`. On Windows with Docker Desktop only, use
   `windows/mpc-build.ps1` (build the helper image first: `docker build -t mpc-builder windows/mpc-builder`).
   A full build takes about 10-15 minutes.

| Path | What |
|---|---|
| `src/peq.c` | the engine: EQ, effects, analyser, and everything the screen shows |
| `gen_params.py` | writes `params.json` and `presets.json` (edit the script, not the JSON) |
| `art/make_art.py` | draws the artwork and writes `layout.conf` and `art/geometry.json` |
| `skin_post.py` | post-build fixes to the skin (drag areas, transparent overlays) |
| `test/` | offline checks: `view.c` (screen model and effects), `response.c` (frequency response), `fx_cost.c` (CPU), `preview.sh` (screen preview) |

Engine tests: `gcc -O2 -Wall -Iwrapper ports/peq/test/view.c ports/peq/src/peq.c -lm -o /tmp/view && /tmp/view`
