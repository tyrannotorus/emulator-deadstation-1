// Deadstation's settings, as Options shows them (ui/src/options.h): built into the shared UI as DEADUI_OPTIONS_SOURCE.
// No BIOS row: the PlayStation's own BIOS always plays (its startup screen too), the core's built-in one only when there
// is no BIOS file.
#include "options.h"

namespace deadui::options
{

const std::vector<Page>& pages()
{
	static const std::vector<Page> list {
		{ "Console", "PlayStation settings.", {
			{ "core", "pcsx_rearmed_region", "Region", "Applies on restart.",
					{ { "Auto", 0 }, { "NTSC", 1 }, { "PAL", 2 } }, false, 1 },
			{ "core", "pcsx_rearmed_cd_turbo", "Fast CD-ROM", "Faster disc loading.", { { "Off", 0 }, { "On", 1 } }, false, 1 },
		} },
		{ "Emulator", "Deadstation settings.", {
			{ "deadstation", "ShowFPS", "Show FPS", "Frame rate overlay.", { { "Off", 0 }, { "On", 1 } }, true, 0 },
			{ "deadstation", "Resolution", "Resolution", "Rendering resolution.",
					{ { "640x480", 0 }, { "1024x768", 1 } }, false, 1 },
			{ "deadstation", "Smoothing", "Smoothing", "Picture scaling.",
					{ { "Sharp", 0 }, { "Smooth", 1 } }, false, 1 },
			{ "core", "pcsx_rearmed_dithering", "Dithering", "Color dithering.",
					{ { "Off", 0 }, { "On", 1 } }, false, 1 },
			{ "core", "pcsx_rearmed_spu_reverb", "Audio Reverb", "Sound reverb.", { { "Off", 0 }, { "On", 1 } }, false, 1 },
			{ "core", "pcsx_rearmed_spu_interpolation", "Audio Interpolation", "Sound smoothing.",
					{ { "Simple", 0 }, { "Gaussian", 1 }, { "Cubic", 2 }, { "Off", 3 } }, false, 0 },
		} },
	};
	return list;
}

}
