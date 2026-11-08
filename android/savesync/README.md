# Save Sync (Thor)

Moves SRAM between a TrimUI Brick running NX Redux and this phone. The newer file wins. A file with the same timestamp is left alone, and a 0-byte file never replaces a real save.

The folder you pick is the Brick's `Saves` folder, not a parent of it. `GB/Tetris.srm` on the Brick is `GB/Tetris.srm` here. RetroArch on the Thor usually keeps that layout in `/storage/emulated/0/RetroArch/saves`.

## Build

Open this directory in Android Studio (SDK 33) and let it install the Gradle wrapper. Build the debug or release APK and install it on the Thor (Android 13).

## Set up

1. On the Thor, open Save Sync. Allow all-files access, notifications, and unrestricted battery when asked. Tap Start.
2. The screen shows this Thor's IP. On the Brick, open Tools, then Save Sync, and enter that IP. A syncs once. B closes and keeps the address.
3. On the Thor, enter the IP shown on the Brick. Point the folder at the same layout as the Brick's Saves folder.
4. Leave Save Sync running. It listens the whole time the Brick is in the menu, and it starts again on boot. Force-stopping the app in Android settings turns that off until you open it and tap Start.

Both clocks have to be right. The Brick can set its clock from the network in Settings. If the times match, neither side overwrites the other.

A save written on the Thor is sent after its timestamp stops changing, a few seconds later. Leaving a game on the Brick uploads a save that is newer than the Thor. Starting a game pulls a save that is newer on the Thor. If the Thor cannot be reached when the game starts, that session is not uploaded on the way out, so an older copy cannot replace the Thor's file. The Brick then shows HELD for that save until you sync on purpose: open Save Sync on the Brick and press A, or tap Sync now here, once both are on the network. That sends the newer file.
