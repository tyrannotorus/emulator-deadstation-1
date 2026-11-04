# Tekken 3 recomp module

Tekken 3 (Namco, 1998), statically recompiled for the TrimUI Brick. Deadstation plays it in place of its
emulator when the game's Current Core is Recomp.

## Supported Formats

| Region | Format | SHA-1 |
|---|---|---|
| USA | CHD | `319ec7377eb0c3ef0b6049e440937d26ae743290` |

The SHA-1 is the CHD header's, shown by `chdman info` as **SHA1**. Other images are currently not supported.

## Features

- **Resolution:** the game drawn at its own resolution, or at twice it on the Brick's 1024x768 screen (Recomp
  Options).
- **Shadows:** the fighters' bodies cast their shadows on the floor along the stage's sun, each spot of floor
  darkened once by the sun's share of its light, in place of the game's flat black ones. Always on.
- **Lighting:** the fighters shade themselves and each other from the sun pixel by pixel (Recomp Options: Enhanced or Original).
- **Spark Lighting:** every hit spark and the game's effect light light the fighters and the floor round them in their
  colours, each casting its own planar shadows of the fighters on the floor, so a hit can throw several shadows at once.
  Recomp Options: 0 (none) to 10, applied at once.

- **Stage moods:** with Lighting Enhanced, stages get a fixed mood of their own: torches in the temple, lamps on the night
  dock, a storm with lightning over the ruin, dusk in the schoolyard.
- **Stage flavour:** something of its own on most stages: mist on Lei's, the forest and the dock, snow at the palace, embers in the temple, petals at the school, rain over the ruin, fireworks at the amusement park, cloud shadows on the rooftop and more.
- **Fight GUI:** the health bars, timer and names can be hidden (Recomp Options, applied at once).
- **Unlock Everything:** every fighter, costume and mode (Recomp Options, applied at once; saved with your game).

## Files

```
module.cfg          the manifest
run.sh              how Deadstation starts it
game.toml           the game: its disc, boot file and runtime settings (PSXRecomp)
game_options.toml   the game's own options PSXRecomp keeps
seeds/              the functions the translation starts from
symbols.toml, symbols/, psx_symbols.h   names found in the game's code
CMakeLists.txt      the module's build
src/plugin.cpp      Deadstation's additions, a plugin inside the game (its hooks: game.toml)
mods/               the plugin's package, which turns it on
```

## Building

From your own disc, with its overlay captures in `overlays/` (they hold the game's code: never in git):

```
shell/brick/recomp.sh tekken3 "/path/to/Tekken 3 (USA).chd"
```

## Credits

Built with [PSXRecomp](https://github.com/RetroPortingToolKit/psxrecomp) (PolyForm Noncommercial 1.0.0).
