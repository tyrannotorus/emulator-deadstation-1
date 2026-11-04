// The in-game menu (MENU button), styled after NextUI's minarch: Continue, Save, Load, Options, Quit, as in
// Deadcast. Drawn on the CPU by the frontend shared with the recomp modules (frontend/ui.h), shown through
// the panel's pictures (fb0 in NextUI's mode). The game is held while it is open; everything runs on the
// main thread, where the pad is read too.
#pragma once
#include <string>

class Video;

namespace deadstation::menu
{

// Restart: the game again from its start. Switch: the game again in the other core (Options): Deadstation
// ends and the launcher starts it again.
enum class Result { Open, Resume, Quit, Restart, Switch };

// the screen it draws on; the file NextUI started (its name is the menu's title), the disc played and this
// player's memory card file (the other Brick gets it in a two-player game: link.h)
void init(Video *video, const std::string& rom, const std::string& disc, const std::string& card);
bool open();
// the menu of a paused two-player session (netplay.h): Continue, Leave two-player game, Quit, why it is
// paused on top; it closes by itself when the session goes on
bool openSession();
bool isOpen();
// raw pad input (controls.h's input numbers) while it is open
void input(int code, bool pressed);
// every main loop iteration while open: repeats held directions, redraws when needed
Result frame();

// the start page, "Loading..." over the game's file name, until its first frame
bool showStart();
bool starting();
void endStart();
void showStartError(const std::string& what);

// the save state slot picked last (FN2/FN1 use it), saved and loaded from outside the menu
bool saveSlot();
bool loadSlot();

}
