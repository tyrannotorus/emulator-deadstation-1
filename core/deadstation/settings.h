// Deadstation's settings: the core's options (as it declares them, with their defaults) and Deadstation's
// own, as Options shows them (ui/src/options.h) and the files keep them: the game's own values (games/<disc id>.cfg),
// else emu.cfg's, the defaults. The core reads its
// options through the libretro environment (GET_VARIABLE); a change is picked up between frames.
#pragma once
#include "dead_ui.h"

#include <libretro.h>

#include <string>

namespace deadstation::settings
{

// the environment's option calls: true if cmd was one
bool environment(unsigned cmd, void *data, bool& handled);

// Deadstation's own settings ([deadstation] in emu.cfg)
bool showFps();
bool nativeResolution();	// 1024 x 768: fb0 at the panel's size and the core's 2x rendering
bool smoothing();			// the frame filtered as it is scaled to the screen

// a core option set for this run only, over emu.cfg (tests, a two-player session's pinned settings)
void force(const std::string& key, const std::string& value);

// Options' values: the game's, else the default; each change kept for the game and applied at once (changed() then
// says what to apply)
options::Store store();
enum Change : unsigned { CoreOptions = 1, Resolution = 2, Smoothing = 4 };
unsigned changed();		// and cleared

}
