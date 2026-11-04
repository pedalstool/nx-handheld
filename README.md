# nx-handheld (pedalstool)

Brick / Brick Hammer fork of [NX Redux](https://github.com/nx-redux/nx-handheld).

## What's different here

- **Brick / Hammer only** — same `tg5040` `DEVICE=brick` build; Brick Pro, Smart Pro, and Smart Pro S are not supported
- **No media apps** — Music Player, Media Player, Image Viewer, and the OSD music widget are removed
- **Console text color** — Settings includes console title / text color (from console-text-color v6)
- **Faster folder/console backgrounds** — backgrounds stay on screen while the next one loads, and recent folders are cached so revisiting a console does not wait on a full decode again (from [BRICK-UI](https://github.com/bebeidon/BRICK-UI))
- **Last Played** — "Recently Played" / Game Switcher "Recent" labels are "Last Played" (from [BRICK-UI](https://github.com/bebeidon/BRICK-UI))
- **Battery as plain text** — with battery percentage enabled, the status bar shows `80%` (text only, including while charging)
- **Updates from this fork** — in-device updater checks `pedalstool/nx-handheld` releases

## Releases

Each release publishes two zips:

| Asset | Use |
|---|---|
| `NXRedux-<tag>-brick.zip` | **OTA** — Settings → About → Check for updates (changed system files only; usually a few MB) |
| `NXRedux-<tag>-brick-full.zip` | **Full install** — extract to a blank / clean SD card |

## Install

1. Update stock TrimUI firmware to **1.1.1** for the Brick first.
2. First time: download **`brick-full`**, unzip onto the SD card root, boot and let it install.
3. Later updates: **Settings → About → Check for updates** (downloads the slim `brick.zip` OTA).

## Credits

- [nx-redux](https://github.com/nx-redux) / NX Redux maintainers for the base firmware
- [bebeidon](https://github.com/bebeidon/BRICK-UI) for folder background caching and Last Played naming
- [ro8inmorgan](https://github.com/ro8inmorgan), [frysee](https://github.com/frysee) and all contributors for developing NextUI
- [clintonium-119](https://github.com/clintonium-119) for the original [RetroAchievements integration](https://github.com/LoveRetro/NextUI/pull/633) in NextUI
- [carroarmato0](https://github.com/carroarmato0) for the [minarch modularization](https://github.com/LoveRetro/NextUI/pull/721)
- [sinedied](https://github.com/sinedied/perfect-retroshaders) for the shaders originally proposed for NextUI
- [KrutzOtrem](https://github.com/KrutzOtrem/Trimui-Brick-Overlays) for the overlays
- [ben16w](https://github.com/ben16w/minui-portmaster) for Minui-Portmaster, and [PortMaster](https://portmaster.games/)
- The authors of every emulator core, and [RetroAchievements](https://retroachievements.org/) / [rcheevos](https://github.com/RetroAchievements/rcheevos)
- Dan Patrick for the [redrawn console logos](https://archive.org/details/console-logos-professionally-redrawn-plus-official-versions), Xiaomi for [MiSans](https://hyperos.mi.com/font/en/download/), and the [M+ FONTS Project](https://mplusfonts.github.io/) / Jikasei Font Koubou for [Rounded M+ 1c](http://jikasei.me/font/rounded-mplus/)

## License

GNU GPL v3.0 — see [LICENSE](LICENSE).

> Independent fork; not affiliated with NextUI or the NX Redux project beyond using their GPL codebase as upstream.
