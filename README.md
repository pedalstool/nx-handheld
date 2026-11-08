# nx-handheld (pedalstool)

Brick / Brick Hammer fork of [NX Redux](https://github.com/nx-redux/nx-handheld).

## What's different here

- **Brick / Hammer only** — same `tg5040` `DEVICE=brick` build; Brick Pro, Smart Pro, and Smart Pro S are not supported
- **No media apps** — Music Player, Media Player, Image Viewer, and the OSD music widget are removed
- **No screenshots or screen recording**
- **No Device Sync** — the tool that copies saves, ROMs, and BIOS between devices is not included
- **No game count under console logos**
- **Save sync** — the Brick and a Thor share saves. The newer file wins. `Golden Sun.sav` and `Golden Sun.srm` are the same cart save, and the bytes are written onto the `.sav` the game loads. ROM files such as `.gba` are left where they are. Tools → Save Sync stores the Thor's address. The menu listens while you are in it; starting a game takes a newer save from the Thor, and leaving the game sends one back. If the Thor is off, the game still starts and that save is held until you sync on purpose
- **Console text color** — Settings includes console title / text color (from console-text-color v6)
- **Faster console backgrounds** — screen-sized backgrounds are cached and shared (no copy on revisit); neighboring consoles prefetch while idle so browsing the Consoles list stays responsive. Returning to a console draws that background again
- **Console transitions** — cycling through systems eases the full-screen background from one console into the next, in time with the selection. It follows the menu animations setting
- **Consoles carousel** — the previous and next consoles are not shown beside the selected one. Only the selected console stays on screen
- **No Minimal on Consoles or game lists** — those layouts are List, Grid, or Carousel, and game lists still have Backdrop. Home, Collections, and Tools still offer Minimal. A saved Minimal setting for Consoles or a game list shows as Carousel
- **No Simple mode** — Settings no longer has it, and there is no PIN gate on Tools or in-game Options
- **Last Played** — "Recently Played" / Game Switcher "Recent" labels are "Last Played" (from [BRICK-UI](https://github.com/bebeidon/BRICK-UI))
- **Battery as plain text** — with battery percentage enabled, the status bar shows `80%` (text only)
- **Power Off in MENU menu** — MENU context menu includes Power Off (alongside Refresh Roms / Tools)
- **Updates from this fork** — in-device updater checks `pedalstool/nx-handheld` releases

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
