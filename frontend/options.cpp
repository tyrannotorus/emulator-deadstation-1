#include "options.h"
#include "recomp.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <strings.h>
#include <unistd.h>

namespace deadstation::options
{
namespace
{

std::string trim(const std::string& s)
{
	const size_t a = s.find_first_not_of(" \t\r");
	if (a == std::string::npos)
		return "";
	return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
}

std::string path()
{
	return recomp::home() + "/emu.cfg";
}

std::vector<std::string> read()
{
	std::vector<std::string> lines;
	std::ifstream in(path());
	for (std::string line; std::getline(in, line); )
		lines.push_back(line);
	return lines;
}

// where a setting is in the file: its line, else the line after its section's last (-1: no section)
struct Place
{
	int line = -1;
	int end = -1;
};

Place find(const std::vector<std::string>& lines, const Setting& setting)
{
	Place place;
	bool in = false;
	for (int i = 0; i < (int)lines.size(); i++)
	{
		const std::string t = trim(lines[i]);
		if (t.empty() || t[0] == ';')
			continue;
		if (t[0] == '[')
		{
			const size_t close = t.find(']');
			in = close != std::string::npos && t.substr(1, close - 1) == setting.section;
			if (in)
				place.end = i + 1;
			continue;
		}
		if (!in)
			continue;
		place.end = i + 1;
		const size_t eq = t.find('=');
		if (eq != std::string::npos && trim(t.substr(0, eq)) == setting.key)
			place.line = i;
	}
	return place;
}

int get(const Setting& setting)
{
	const std::vector<std::string> lines = read();
	const Place place = find(lines, setting);
	if (place.line < 0)
		return setting.fallback;
	const std::string& line = lines[place.line];
	const std::string value = trim(line.substr(line.find('=') + 1));
	if (setting.flag)
		return strcasecmp(value.c_str(), "yes") == 0 || strcasecmp(value.c_str(), "true") == 0
				|| strcasecmp(value.c_str(), "on") == 0 || value == "1";
	// as the emulator reads a number: hexadecimal after 0x, else decimal
	const bool hex = value.size() > 1 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X');
	return (int)std::strtoul(value.c_str(), nullptr, hex ? 16 : 10);
}

// the file again, the setting's line changed or added; written beside it, then in its place
void set(const Setting& setting, int value)
{
	std::vector<std::string> lines = read();
	const std::string line = std::string(setting.key) + " = "
			+ (setting.flag ? (value != 0 ? "yes" : "no") : std::to_string(value));
	const Place place = find(lines, setting);
	if (place.line >= 0)
		lines[place.line] = line;
	else if (place.end >= 0)
		lines.insert(lines.begin() + place.end, line);
	else
	{
		if (!lines.empty() && !trim(lines.back()).empty())
			lines.push_back("");
		lines.push_back(std::string("[") + setting.section + "]");
		lines.push_back(line);
	}
	const std::string temp = path() + ".new";
	FILE *out = std::fopen(temp.c_str(), "w");
	if (out == nullptr)
	{
		std::printf("Deadstation: %s cannot be written\n", temp.c_str());
		return;
	}
	for (const std::string& l : lines)
		std::fprintf(out, "%s\n", l.c_str());
	const bool written = std::fflush(out) == 0 && fsync(fileno(out)) == 0;
	std::fclose(out);
	if (!written || std::rename(temp.c_str(), path().c_str()) != 0)
	{
		std::remove(temp.c_str());
		std::printf("Deadstation: %s cannot be written\n", path().c_str());
	}
}

}	// namespace

const std::vector<Page>& pages()
{
	static const std::vector<Page> list {
		// no BIOS row: the PlayStation's own BIOS always plays (its startup screen too), the core's built-in one
		// only when there is no BIOS file (the core's "auto")
		{ "Console", "The PlayStation's region.", {
			{ "core", "pcsx_rearmed_region", "Region", "The console's region. Takes effect on restart.",
					{ { "Auto", 0 }, { "NTSC", 1 }, { "PAL", 2 } }, false, 0 },
		} },
		{ "Video", "", {
			{ "deadstation", "ShowFPS", "Show FPS", "Shows the frame rate.", { { "Off", 0 }, { "On", 1 } }, true, 0 },
			{ "deadstation", "Resolution", "Resolution", "The picture's resolution: the PlayStation's, or the screen's own with the game drawn at twice its resolution.",
					{ { "640x480", 0 }, { "1024x768", 1 } }, false, 0 },
			{ "deadstation", "Smoothing", "Smoothing", "How the picture is scaled to the screen.",
					{ { "Sharp", 0 }, { "Smooth", 1 } }, false, 1 },
			{ "core", "pcsx_rearmed_dithering", "Dithering", "The PlayStation's dithering of colours.",
					{ { "Off", 0 }, { "On", 1 } }, false, 1 },
			{ "core", "pcsx_rearmed_frameskip_type", "Frame Skip", "Skips drawing frames when the game falls behind.",
					{ { "Off", 0 }, { "Auto", 1 } }, false, 0 },
		} },
		{ "Audio", "", {
			{ "core", "pcsx_rearmed_spu_reverb", "Reverb", "The sound chip's reverb.", { { "Off", 0 }, { "On", 1 } }, false, 1 },
			{ "core", "pcsx_rearmed_spu_interpolation", "Interpolation", "How the sound chip's samples are smoothed.",
					{ { "Simple", 0 }, { "Gaussian", 1 }, { "Cubic", 2 }, { "Off", 3 } }, false, 0 },
		} },
	};
	return list;
}

Store file()
{
	return { get, set };
}

}
