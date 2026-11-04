#include "controls.h"
#include "recomp.h"

#include <libretro.h>

#include <cstdlib>
#include <fstream>
#include <map>

namespace deadstation::controls
{
namespace
{

// the Brick's buttons as SDL numbers them
enum : int { PadB = 0, PadA = 1, PadY = 2, PadX = 3, PadL1 = 4, PadR1 = 5, PadSelect = 6, PadStart = 7,
	PadFn1 = 9, PadFn2 = 10, PadMinus = 13, PadPlus = 14 };
enum : int { AxisL2 = 2, AxisR2 = 5 };

const std::vector<Control> Controls {
	{ Up, "Up", "" }, { Down, "Down", "" }, { Left, "Left", "" }, { Right, "Right", "" },
	{ Cross, "Cross", "" }, { Circle, "Circle", "" }, { Square, "Square", "" }, { Triangle, "Triangle", "" },
	{ L1, "L1", "" }, { R1, "R1", "" }, { L2, "L2", "" }, { R2, "R2", "" },
	{ Select, "Select", "" }, { Start, "Start", "" },
	{ SaveState, "Save State", "Saves the game in the slot picked last." },
	{ LoadState, "Load State", "Loads the game from the slot picked last." },
	{ Screenshot, "Screenshot", "Saves a picture of the screen in Screenshots." },
	{ FastForward, "Fast Forward", "While held, the game runs as fast as it can." },
};

// by position, as the PlayStation's pad: the Brick's B (south) is Cross
const int Defaults[Count] = {
	HatUp, HatDown, HatLeft, HatRight, PadB, PadA, PadY, PadX, PadL1, PadR1, Axis + AxisL2, Axis + AxisR2,
	PadSelect, PadStart, PadFn2, PadFn1, None, None,
};

int bindings[Count];
SDL_Joystick *pad = nullptr;

std::string path()
{
	return recomp::home() + "/controls.cfg";
}

void save()
{
	std::ofstream out(path(), std::ios::trunc);
	for (const Control& c : Controls)
		out << c.name << " = " << bindings[c.function] << "\n";
}

}	// namespace

const std::vector<Control>& list()
{
	return Controls;
}

void load()
{
	for (int i = 0; i < Count; i++)
		bindings[i] = Defaults[i];
	std::ifstream in(path());
	std::map<std::string, int> read;
	for (std::string line; std::getline(in, line); )
	{
		const size_t eq = line.find(" = ");
		if (eq != std::string::npos)
			read[line.substr(0, eq)] = std::atoi(line.c_str() + eq + 3);
	}
	for (const Control& c : Controls)
		if (auto r = read.find(c.name); r != read.end())
			bindings[c.function] = r->second;
}

int binding(Function f)
{
	return bindings[f];
}

std::string inputName(int input)
{
	switch (input)
	{
	case None: return "NONE";
	case PadB: return "B";
	case PadA: return "A";
	case PadY: return "Y";
	case PadX: return "X";
	case PadL1: return "L1";
	case PadR1: return "R1";
	case PadSelect: return "SELECT";
	case PadStart: return "START";
	case MenuButton: return "MENU";
	case PadFn1: return "FN1";
	case PadFn2: return "FN2";
	case PadMinus: return "MINUS";
	case PadPlus: return "PLUS";
	case HatUp: return "UP";
	case HatDown: return "DOWN";
	case HatLeft: return "LEFT";
	case HatRight: return "RIGHT";
	case Axis + AxisL2: return "L2";
	case Axis + AxisR2: return "R2";
	default:
		return input >= Axis ? "AXIS " + std::to_string(input - Axis) : "BUTTON " + std::to_string(input);
	}
}

void bind(Function f, int input)
{
	for (int& b : bindings)
		if (b == input)
			b = None;
	bindings[f] = input;
	save();
}

void unbind(Function f)
{
	bindings[f] = None;
	save();
}

void open(SDL_Joystick *joystick)
{
	pad = joystick;
}

bool pressed(int input)
{
	if (pad == nullptr || input == None)
		return false;
	if (input >= Axis)
		return SDL_JoystickGetAxis(pad, input - Axis) > 16384;
	if (input >= HatUp)
	{
		const Uint8 hat = SDL_JoystickNumHats(pad) > 0 ? SDL_JoystickGetHat(pad, 0) : 0;
		static const Uint8 Bits[] = { SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT };
		return (hat & Bits[input - HatUp]) != 0;
	}
	return SDL_JoystickGetButton(pad, input) != 0;
}

bool held(Function f)
{
	return pressed(bindings[f]);
}

int16_t padBits()
{
	static const unsigned Ids[] = {
		RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
		RETRO_DEVICE_ID_JOYPAD_B, RETRO_DEVICE_ID_JOYPAD_A, RETRO_DEVICE_ID_JOYPAD_Y, RETRO_DEVICE_ID_JOYPAD_X,
		RETRO_DEVICE_ID_JOYPAD_L, RETRO_DEVICE_ID_JOYPAD_R, RETRO_DEVICE_ID_JOYPAD_L2, RETRO_DEVICE_ID_JOYPAD_R2,
		RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_START,
	};
	int16_t bits = 0;
	for (int f = Up; f <= Start; f++)
		if (held((Function)f))
			bits |= (int16_t)(1 << Ids[f]);
	return bits;
}

}
