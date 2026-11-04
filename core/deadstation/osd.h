// What the emulator says over the game: the frame counter (Show FPS) and its messages, in the menu's font. Each line is
// a small picture (ui::label, from characters drawn in advance) copied onto every frame just before it is shown
// (Panel::setCorners), at the bottom left: the counter, with any message above it. A hosted module's counter looks the same.
#pragma once
#include <string>

class Video;

namespace deadstation::osd
{

// the screen it writes on
void init(Video *video);
// the screen was made again: what it shows, again
void again();
// a message for this long, with an optional line under it; from any thread
void notify(const std::string& text, int durationMs, const std::string& details = "");
// each frame of the core while the game plays, with whether it gave a new picture. A game may give none for a
// while (a still screen): text that changes then shows the last frame again.
void frame(bool rendered, bool showFps, bool fastForward);

}
