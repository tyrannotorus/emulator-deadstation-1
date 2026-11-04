# Deadstation 1
A PlayStation 1 emulator targeting optimal performance on the Trimui Brick/Hammer.

# Features
## Emulator
- Demanding games like Bloody Roar II play at full speed at 1024x768.
- USB-C / Bluetooth device-to-device two-player.
- In-app Recomp store for game mods

# Supported Devices
## Trimui Brick/Hammer
1. Install Deadstation via the [NextUI](https://github.com/LoveRetro/NextUI) [Pak Store](https://github.com/LoveRetro/nextui-pak-store).
2. Put PlayStation roms in a `/Roms/PlayStation (PS-DEADSTATION1)/` folder on your SD Card.
3. Put a PlayStation BIOS (such as `scph1001.bin`) in the `/Emus/tg5040/PS-DEADSTATION1.pak/bios/` folder on your SD Card.

# Credits
- [PCSX ReARMed](https://github.com/libretro/pcsx_rearmed): the emulator Deadstation is built from.
- [PSXRecomp](https://github.com/RetroPortingToolKit/psxrecomp): the PlayStation recompiler the recomp modules build on.

---

## AI Section

Notes for agents working on this repo. Everything above the line is the maintainer's: leave it as written.

Deadstation 1 is a PlayStation emulator for the TrimUI Brick (NextUI, tg5040) only, as `Emus/tg5040/PS-DEADSTATION1.pak`, built from PCSX ReARMed and
shaped like [Deadcast](https://github.com/tyrannotorus/emulator-dreamcast-deadcast), the reference for its architecture. It draws with Vulkan on the Brick's PowerVR GE8300
straight to the panel through fb0; SDL2 reads the pad and plays the audio. A recomp module is a separate executable that Deadstation hosts: it starts the module as a child process and
sleeps until MENU, and the menu, the module's options and the store stay Deadstation's.

### Layout

- `core/`: PCSX ReARMed (libretro/pcsx_rearmed; pruned to what the Brick builds). Its libretro interface is linked in, not loaded. Deadstation's own code is in `core/deadstation/`: the frame loop
  (`main.cpp`), the Vulkan presenter and fb0 panel (`video.*`, `panel.*`), the on-screen text (`osd.*`), settings, controls, save states, the menu's emulator side, the link between two Bricks and the two-player lockstep (`netplay.*`).
- `ui/`: the menu library, a submodule: pages, the module host, the store, and the tg5040 toolchain. Everything on the screen but the game is drawn on the CPU by it.
- `store/`: the recomp modules the store lists (`registry.txt`). An Action rebuilds the list on the `store` branch, hourly if GitHub keeps to its schedule, so run it by hand after publishing a module.
- `shell/brick/`: build and package scripts. `pak.json`: the Pak Store's entry.

Only source, build scripts and READMEs go in git; never disc images or anything made from them.

An NTSC game runs at 59.85 Hz and the panel with it: the DSI's frame is 789 lines instead of 784 while it plays (`panel.cpp`, `FPS_NTSC` in `psxcounters.c`), so no frame is shown twice. Recomp modules keep the panel's own rate. The kernel's console is off while playing (`launch.sh`): its messages hold up the display's flip.

### Build

On an x86-64 Linux host with clang, lld, CMake, Ninja, Docker (only to copy the toolchain image's sysroot), `vulkan-headers`, `glslang` and `curl` (headers).

```
git submodule update --init ui
ui/toolchain/toolchain.sh                # the tg5040 sysroot, out of ghcr.io/loveretro/tg5040-toolchain
ui/toolchain/devlibs.sh SERIAL           # the Brick's libcurl.so.4, over adb
shell/brick/build.sh                     # build-brick/deadstation/core/deadstation/deadstation
shell/brick/package.sh                   # build-brick/PS-DEADSTATION1.pak and PS-DEADSTATION1.pak.zip
```

The core's sources and flags in `CMakeLists.txt` follow upstream's `Makefile.libretro` for aarch64.

### Recomp modules

- Each is its own repository, built on the [recomp SDK](https://github.com/tyrannotorus/emulator-deadstation-1-recomp-sdk): the rules, the folder and `module.cfg` are in its README.
- Listing one: add its repository to `store/registry.txt`. The store then offers its latest release.
- In the menu a module goes by its manifest's `title` (`Tekken 3 Remastered`); in the docs it is the "<game title> recomp module". Never the game's title alone.
- A module's options, in Options with Core on it: Show FPS first, the module's settings after, then its own Controls if it declares any (nothing is shared with the emulator's). They are kept in its own
  `recomp-data/<id>/settings.cfg`, and go when it is deleted. Getting, updating and deleting a module are the Recomp Store's (on the main page).
- Any error of a module's shows the error with one button, `A TRY EMULATOR CORE`: Core becomes the emulator and the game starts again in it. The module stays installed, to switch back to in Options or to update in the store.

### Releasing

The version is `project(deadstation VERSION ...)` in `CMakeLists.txt` and `version` in `pak.json`; the release's tag is the version, and its asset is `build-brick/PS-DEADSTATION1.pak.zip`. The Pak Store reads
`pak.json` from `main`, so the release must exist before a new version reaches `main` once it is listed. Recomp modules have their own releases.

### Install

Copy `build-brick/PS-DEADSTATION1.pak` to the SD card's `Emus/tg5040/`. Games go in a folder such as `Roms/PlayStation (PS-DEADSTATION1)/`: a disc (`.chd`, `.cue`, `.pbp`) or an `.m3u`. The BIOS goes in the pak's
`bios/`, else `Bios/PS`; it always plays, the PlayStation's startup screen first (no setting). Without one the core's built-in BIOS plays, with no startup screen. Settings, memory cards and save states are kept
in `.userdata/tg5040/PS-DEADSTATION1/`.

### Controls

MENU opens Deadstation's menu (`ui/`, `core/deadstation/menu.cpp`): Continue, Save and Load (ten slots, with pictures; the emulator's only), Options, Recomp Store and Quit. Options holds Core and, under it, the options of the
core picked: the emulator's Console, Video, Audio, Controls and Local Multiplayer (each setting is saved as it changes), or the module's settings. Recomp Store lists every recomp module: download, update, delete. Updating the
module being played says it requires a restart, and A restarts the game in the new version. Going back from Options with another core picked asks to restart: Okay starts the game again in it, Cancel puts the core back. Each core's
options can be set from either core; Local Multiplayer, which plays in the emulator, asks for that restart first. FN2 saves a state, FN1 loads it; L1, L2, R1, R2, Select and Start are the PlayStation's. Controls remaps them all.

### Two players

Two Bricks over a USB-C cable or Bluetooth (`core/deadstation/link*`): both start the game again from its start and run it in lockstep (`netplay.*`). Each player brings a memory card: player 1's in slot 1, player 2's in slot 2 on both Bricks, each kept in its owner's file by its
owner's Brick. Leaving lets the other Brick go on alone. Recomp modules don't play two-player.
