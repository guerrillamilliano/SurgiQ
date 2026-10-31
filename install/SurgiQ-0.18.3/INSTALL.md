# SurgiQ 0.18.3

SurgiQ: visual 11-band EQ with floating bands, a neon line curve over a live spectrum line, and FX sends (reverb, delay, chorus, shimmer)

A native MPC OS plugin (effect) with its own MPC screen skin, loaded by MPC's built-in plugin host.

## Requirements

- A first-generation MPC OS standalone device (32-bit ARM, like the Force, MPC Live / Live II, One, X and Key 61).
  The installer refuses anything else. Newer models are untested.
- **Root shell access** (SSH) to the device. Stock MPC OS doesn't offer this; you need a modded unit.
- Tested on MPC OS with a Force. Installing plugins this way is unofficial: back up first, use at your own risk.

## Install (scripted)

1. Unzip, then copy the whole folder to the device, e.g. `scp -r SurgiQ-0.18.3 root@<device-ip>:/tmp/`
2. Run it: `ssh root@<device-ip> sh /tmp/SurgiQ-0.18.3/install.sh`

The installer checks the device, **stops MPC** (save your project first), copies `portable/Milliano - VST - SurgiQ/` (the plugin, its skin
and its data, as one folder) into `/sdcard/Synths`, backs up `MPC.settings`, adds the plugin to MPC's plugin list from the
folder's `plugin-meta.xml` and starts MPC again. Running it again upgrades in place, keeping your own files.
An older install of this plugin (the previous layout, with the `.so` in `/sdcard/vst`) is replaced, not duplicated.
`-y` skips the confirmation prompt; `-t <folder>` installs into another Synths folder (for example on a card).

Then add **SurgiQ** to a track from the plugin browser (Insert effects). Its screen appears in the plugin view,
and the Q-Links follow the page.

## Uninstall

`ssh root@<device-ip> sh /tmp/SurgiQ-0.18.3/uninstall.sh` removes the plugin folder and the plugin-list entry (it also stops and
restarts MPC). Projects that use the plugin will load without it. Files you added yourself are kept.

## Install by hand

1. Copy `portable/Milliano - VST - SurgiQ/` to `/sdcard/Synths/Milliano - VST - SurgiQ/`. It holds `peq.so`, `plugin-meta.xml`, `version.xml`, `Plugin Skins/`
   and the data:
2. Stop MPC: `systemctl stop acvs`. If there is no `acvs` service (`systemctl cat acvs` fails, as on some MPC OS 2.x versions and on Hakai-enabled systems), use `systemctl stop inmusic-mpc`.
3. Back up the settings file, `MPC.settings` (on a Force: `/media/az01-internal/Settings/MPC/MPC.settings`).
4. In `MPC.settings`, inside `<VALUE name="pluginList-arm"><KNOWNPLUGINS>`, add the line from `plugin-meta.xml` with
   `%payload-path%` replaced by `/sdcard/Synths`. If there is no `pluginList-arm` value yet, add one just before `</PROPERTIES>`:
   ```xml
   <VALUE name="pluginList-arm">
     <KNOWNPLUGINS>
       (the line from plugin-meta.xml)
     </KNOWNPLUGINS>
   </VALUE>
   ```
5. Start MPC: `systemctl start acvs`, or `systemctl start inmusic-mpc` if that is the service you stopped. If MPC shows default settings, restore your backup (the XML was malformed).

Edit `MPC.settings` only while MPC is stopped: MPC can save its own copy over a change made while it runs. Tools that
rebuild the whole plugin list from the plugin folders in `Synths` keep this plugin, because it is such a folder.

## Files

See `SHA256SUMS`. Made with [mpc-vst-plugins](https://github.com/sd88me/mpc-vst-plugins).
