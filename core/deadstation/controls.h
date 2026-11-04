// The Brick's controls (trimui_inputd's Xbox 360 pad through SDL's joystick API) and what each does:
// the PlayStation's buttons and Deadstation's own functions, remapped in Options > Controls and
// kept in <home>/controls.cfg. MENU is always the menu.
#pragma once
#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace deadstation::controls
{

// an input: a button (its number), a d-pad direction (HatUp..HatRight) or a trigger (Axis + its axis)
enum : int { None = -1, HatUp = 256, HatDown, HatLeft, HatRight, Axis = 512 };
constexpr int MenuButton = 8;

enum Function : int
{
	Up, Down, Left, Right, Cross, Circle, Square, Triangle, L1, R1, L2, R2, Select, Start,
	SaveState, LoadState, Screenshot, FastForward, Count
};
struct Control
{
	Function function;
	const char *name;
	const char *desc;
};
const std::vector<Control>& list();

void load();				// <home>/controls.cfg, else the defaults
int binding(Function f);
std::string inputName(int input);
// one control does one thing: taken from whatever had it; saved at once
void bind(Function f, int input);
void unbind(Function f);

// the pad's state now
void open(SDL_Joystick *joystick);
bool pressed(int input);
bool held(Function f);
// RETRO_DEVICE_ID_JOYPAD_* bits for the PlayStation's buttons
int16_t padBits();

}
