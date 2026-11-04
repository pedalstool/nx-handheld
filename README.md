# nx-handheld (pedalstool)

Brick / Brick Hammer fork of [NX Redux](https://github.com/nx-redux/nx-handheld).

## What's different here

- **Brick / Hammer only** — same `tg5040` `DEVICE=brick` build; Brick Pro, Smart Pro, and Smart Pro S are not supported
- **No media apps** — Music Player, Media Player, Image Viewer, and the OSD music widget are removed
- **Console text color** — Settings includes console title / text color (from console-text-color v6)
- **Faster console backgrounds** — screen-sized backgrounds are cached and shared (no copy on revisit); neighboring consoles prefetch while idle so browsing the Consoles list stays responsive. Returning to a console draws that background again
- **Last Played** — "Recently Played" / Game Switcher "Recent" labels are "Last Played" (from [BRICK-UI](https://github.com/bebeidon/BRICK-UI))
- **Battery as plain text** — with battery percentage enabled, the status bar shows `80%` (text only)
- **Power Off in MENU menu** — MENU context menu includes Power Off (alongside Refresh Roms / Tools)
- **Updates from this fork** — in-device updater checks `pedalstool/nx-handheld` releases

## Releases

Download **`NXRedux-<tag>-brick.zip`** from [Releases](https://github.com/pedalstool/nx-handheld/releases). That zip is both the full SD install and the OTA package (complete system zip — required by the boot installer).

### Changelog

**v1.1.2**

- Sleeping while plugged in no longer hangs. The power button wakes the Brick again. A full battery on the charger used to enter deep sleep, and that sleep does not resume.

**v1.1.1**

- Console backgrounds show again when you return to a console. A failed background load no longer blocks later ones.
- Nearby console backgrounds prefetch while you browse. Power Off is in the MENU menu.
- The update zip is a full system package. v1.1.0 shipped a partial zip that could leave the Brick unable to boot.
- The unused charging-battery graphic is gone. With battery percentage on, the status bar is plain text (`80%`).

**v1.1.0**

- Battery percentage shows as plain text.
- "Last Played" labels, and folder backgrounds are cached.

**v1.0.0**

- First Brick / Hammer build of this fork.

## Install

1. Update stock TrimUI firmware to **1.1.1** for the Brick first.
2. Unzip the release onto the SD card root (so you get `MinUI.zip` / `.system` as shipped).
3. Boot the device and let it install.
4. Later updates: **Settings → About → Check for updates**.

## Credits

- [nx-redux](https://github.com/nx-redux) / NX Redux maintainers for the base firmware
- [bebeidon](https://github.com/bebeidon/BRICK-UI) for Last Played naming
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
