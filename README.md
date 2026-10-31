# SurgiQ

A visual 11-band parametric EQ with effects, made as a native plugin for the **Akai MPC Live II** (MPC OS 3.x, armv7).

- **Floating bands:** hold a band's pill on the graph and drag it. Left and right set its frequency, up and down its
  gain (or the resonance of the high- and low-pass), both at once. A tap selects the band. Dragging has been tested with
  a Bluetooth mouse; touch input is wired up the same way but not yet tested. The FREQUENCY and GAIN boxes and the
  Q-Links edit the selected band too.
- **11 bands:** high-pass, low shelf, seven peaks, high shelf, low-pass. A new instance starts with the seven peaks flat
  and evenly spaced, and the high-pass, low shelf, high shelf and low-pass off.
- **Live EQ curve:** a glowing red line that stays connected through sharp cuts and steep filters.
- **Filters:** 12, 24 or 48 dB/oct high- and low-pass with resonance. Every band can be switched off.
- **Live spectrum analyser** behind the curve (PRE or POST), drawn as one connected shape. UPDATE RATE (LOW, MED, HIGH)
  sets how many changes a second it sends to MPC: MPC falls behind if it gets too many, so pick the highest setting that
  stays in time with the music. Fast moves show as a line that reaches a peak or dip a column or two early.
- **FX tab:** reverb, delay, chorus and shimmer as sends after the EQ. Each has ON, MIX and two settings, and runs on
  the whole signal (FULL) or on one frequency band (BAND). Each active effect's range shows as colored brackets on the
  EQ graph.
- **29 presets** for drums, instruments and basic filter shapes, in MPC's PRESET menu.
- White and red look.

**Good to know**

- Saved projects from older versions still load. A project keeps its own settings, including UPDATE RATE, which now
  reads as LOW, MED or HIGH in the position its old rate was saved in.
- To drag freely, SurgiQ reads pointer positions from `/dev/input` itself, read-only, because MPC skins have no XY
  control. Nothing moves unless MPC also reports a press on one of SurgiQ's pills. Without access to those devices the
  pills still select bands, and the rest of the plugin works normally.
- The screen is heavy: about 30,000 image layers, though it has run well on one Live II so far.

## Install

Everything needed to install is in [`install/`](install):

- `install/SurgiQ-0.18.3-mpc-armv7.zip`: the installer package (also on the [Releases](../../releases) page)
- `install/SurgiQ-0.18.3/`: the same package unpacked:
  - `install.sh` and `uninstall.sh`
  - `INSTALL.md`
  - `SHA256SUMS`
  - `portable/Milliano - VST - SurgiQ/` (the plugin `peq.so` and its skin)

Copy the zip to the MPC (for example with `scp`), then on the MPC as root:

```sh
unzip SurgiQ-0.18.3-mpc-armv7.zip && cd SurgiQ-0.18.3
sha256sum -c SHA256SUMS   # optional: check the files
sh install.sh             # installs to the internal drive (/sdcard/Synths); restarts MPC, so save your project first
```

The internal drive is the default and needs no patch. To install on an external drive instead, add
`-t <its Synths folder>` (for example `-t /media/SSD/Synths`; that needs a drive-exec patch, such as "Auto exec" from the
mpc-vst-plugins patches). To remove it: `sh uninstall.sh` from the same folder, with the same `-t` if you used one.

## Build

SurgiQ is a port for [sd88me/mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins) (MIT), which provides the
VST2 wrapper, skin builder and toolchain. It is not included here.

1. Clone mpc-vst-plugins and copy this repo's files (except `windows/`) into `ports/peq/`.
2. After changing parameters or the screen, regenerate:
   `python3 ports/peq/gen_params.py` and `python3 ports/peq/art/make_art.py` (needs Pillow).
3. Build: `tools/build_port.sh ports/peq/vst.json`. On Windows with Docker Desktop only, use
   `windows/mpc-build.ps1` (build the helper image first: `docker build -t mpc-builder windows/mpc-builder`).
   A full build takes about an hour, nearly all of it drawing the skin's layers.

| Path | What |
|---|---|
| `src/peq.c` | the engine: EQ, effects, analyser, and everything the screen shows |
| `gen_params.py` | writes `params.json` and `presets.json` (edit the script, not the JSON) |
| `art/make_art.py` | draws the artwork and writes `layout.conf` and `art/geometry.json` |
| `skin_post.py` | post-build fixes to the skin (drag areas, transparent layers) |
| `test/` | offline checks: `view.c` (screen model and effects), `response.c` (frequency response), `fx_cost.c` (CPU), `preview.sh` (screen preview) |

Engine tests: `gcc -O2 -Wall -Iwrapper ports/peq/test/view.c ports/peq/src/peq.c -lm -lpthread -o /tmp/view && /tmp/view`
