
---

## AI Section

Notes for agents working on this repo. Everything above the line is the maintainer's: leave it as written.

Deadstation 1 is a PlayStation emulator for the TrimUI Brick (NextUI, tg5040) only, as
`Emus/tg5040/PS-DEADSTATION1.pak`, built from PCSX ReARMed and shaped like Deadcast (the maintainer's Dreamcast
emulator): the same menu, two-Brick play and recomp modules. It draws with Vulkan on the Brick's PowerVR GE8300
straight to the panel through fb0; SDL2 reads the pad and plays the audio.

### Layout

- `core/`: PCSX ReARMed (libretro/pcsx_rearmed, a git subtree; pruned to what the Brick builds). Its libretro
  interface is linked in, not loaded. Deadstation's own code is in `core/deadstation/`: the frame loop
  (`main.cpp`), the Vulkan presenter and fb0 panel (`video.*`, `panel.*`), settings, controls, save states,
  the menu's emulator side, the link between two Bricks and the two-player lockstep (`netplay.*`).
- `frontend/`: the menu library the emulator and every recomp module share (from Deadcast): pages, Emulator
  Options' settings (`options.cpp`), the module store and the catalog of downloadable modules (`catalog.h`).
- `recomp/`: recomp modules. `recomp/psxrecomp` is PSXRecomp (a submodule at its audited pin, PolyForm
  Noncommercial), brought to its patches by `recomp/prepare-psxrecomp.sh` (`recomp/patches/psxrecomp/`); every
  step runs without network (`recomp/sbx`). `recomp/brick/` puts a module's frames on Deadstation's presenter.
  `recomp/modules/<id>/`: each module's project, manifest and README.
- `shell/brick/`: build, package and release scripts. `pak.json`: the Pak Store's entry.

Only source, build scripts and READMEs go in git; never disc images or anything made from them (staged
discs, generated game C, overlay captures).

### Build

On an x86-64 Linux host with clang, lld, CMake, Ninja, Docker (only to copy the toolchain image's sysroot),
`vulkan-headers`, `glslang` and `curl` (headers); for recomp modules also gcc, Python 3, bubblewrap and SDL3.

```
shell/brick/toolchain.sh                 # the tg5040 sysroot, out of ghcr.io/loveretro/tg5040-toolchain
shell/brick/devlibs.sh SERIAL            # the Brick's libcurl.so.4, over adb
shell/brick/build.sh                     # build-brick/deadstation/core/deadstation/deadstation
shell/brick/package.sh                   # build-brick/PS-DEADSTATION1.pak and PS-DEADSTATION1.pak.zip
```

The core's sources and flags in `CMakeLists.txt` follow upstream's `Makefile.libretro` for aarch64; after a
subtree pull, compare with a dry run (`make -n -f Makefile.libretro platform=unix ARCH=arm64 DYNAREC=ari64
BUILTIN_GPU=neon HAVE_NEON_ASM=0 STATIC_LINKING=1 ...`).

### Recomp modules

- Building one: `git submodule update --init recomp/psxrecomp` (never recursive), then
  `shell/brick/recomp.sh <id> <disc.chd>` makes `build-brick/recomp/<id>/`.
- Releasing one: `shell/brick/recomp-release.sh <id> <version>` packs it and writes its size and SHA-256
  into `frontend/catalog.h`; build Deadstation after.
- A module's enhancements are a PSXRecomp plugin inside the game, `recomp/modules/<id>/src/plugin.cpp`, hooked
  at the game's own functions (listed in its `game.toml`'s `mod_function_entry_funcs`), turned on by its
  package in `mods/` and switched by its Recomp Options settings through `run.sh` (environment variables).
- PSXRecomp's local patches (`recomp/patches/psxrecomp/`) include fixes the modules need (e.g. 0008: the SPU's
  key-on as the hardware's, without which Rally Cross hangs on its attract screen).
- In the menu and the docs a module is always the "<game title> recomp module".
- A module has no menu of its own. MENU opens Deadstation's over the held game (`recomp/brick/module_menu.cpp`, on `frontend/`):
  Save and Load are disabled; Recomp Options is Check for Update, Show FPS, the settings its `module.cfg` declares, then
  Delete Recomp Module. A setting the plugin can't apply while playing is read by `run.sh` at the module's start: changing one
  makes Continue read Restart. One it can is registered with `deadstation_live_settings` (`recomp/brick/psx_sdk.h`).
- A module keeps its memory cards and controls in its own folder (`saves/`, `input.ini`): an update moves them to the new
  folder (`frontend/recomp.h`, `keepPlayerFiles`); Delete Recomp Module takes them too, after asking.

### Install

Copy `build-brick/PS-DEADSTATION1.pak` to the SD card's `Emus/tg5040/`. Games go in a folder such as
`Roms/PlayStation (PS-DEADSTATION1)/`: a disc (`.chd`, `.cue`, `.pbp`) or an `.m3u`. The BIOS goes in the pak's
`bios/`, else `Bios/PS`; it always plays, the PlayStation's startup screen first (no setting). Without one the
core's built-in BIOS plays, with no startup screen. Settings, memory cards and save states are kept
in `.userdata/tg5040/PS-DEADSTATION1/`.

### Controls

MENU opens the menu, in the emulator and in a recomp module: Continue, Save and Load (ten slots, with pictures; the
emulator's only), Options and Quit. Options holds Current Core, Emulator Options (Console, Video, Audio, Controls, Local
Multiplayer) and Recomp Options (Download Recomp Module, or the installed module's update check and settings). FN2
saves a state, FN1 loads it; L1, L2, R1, R2, Select and Start are the PlayStation's. Controls remaps them all.
