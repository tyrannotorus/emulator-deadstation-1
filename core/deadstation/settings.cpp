#include "settings.h"
#include "log.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

namespace deadstation::settings
{
namespace
{

// the core's options: their values (the first is the default unless it says otherwise)
struct CoreOption
{
	std::vector<std::string> values;
	std::string fallback;
};
std::map<std::string, CoreOption> coreOptions;
std::map<std::string, std::string> forced;
std::map<std::string, std::string> answers;		// GET_VARIABLE's strings, kept alive
unsigned pending = 0;
bool coreUpdate = false;

// Options' core settings: the value each of its choices stands for, in order
const std::map<std::string, std::vector<const char *>> CoreValues {
	{ "pcsx_rearmed_region", { "auto", "NTSC", "PAL" } },
	{ "pcsx_rearmed_frameskip_type", { "disabled", "auto" } },
	{ "pcsx_rearmed_dithering", { "disabled", "enabled" } },
	{ "pcsx_rearmed_spu_reverb", { "disabled", "enabled" } },
	{ "pcsx_rearmed_spu_interpolation", { "simple", "gaussian", "cubic", "off" } },
};

const options::Setting *findSetting(const char *section, const char *key)
{
	for (const options::Page& page : options::pages())
		for (const options::Setting& s : page.settings)
			if (strcmp(s.section, section) == 0 && strcmp(s.key, key) == 0)
				return &s;
	return nullptr;
}

int fileValue(const char *section, const char *key, int fallback)
{
	const options::Setting *s = findSetting(section, key);
	return s != nullptr ? options::file().get(*s) : fallback;
}

// a core option's value: forced, else Options', else the core's default
std::string coreValue(const std::string& key)
{
	if (auto f = forced.find(key); f != forced.end())
		return f->second;
	// the PlayStation's own startup screen (the BIOS's logo) always plays before the game, and its own BIOS
	// whenever there is a file of it (the core's built-in one only without): no settings
	if (key == "pcsx_rearmed_show_bios_bootlogo")
		return "enabled";
	if (key == "pcsx_rearmed_bios")
		return "auto";
	if (auto v = CoreValues.find(key); v != CoreValues.end())
		if (const options::Setting *s = findSetting("core", key.c_str()))
		{
			const int i = options::file().get(*s);
			if (i >= 0 && i < (int)v->second.size())
				return v->second[i];
		}
	// 1024 x 768: the NEON GPU draws at twice the resolution
	if (key == "pcsx_rearmed_neon_enhancement_enable")
		return nativeResolution() ? "enabled" : "disabled";
	if (auto o = coreOptions.find(key); o != coreOptions.end())
		return o->second.fallback;
	return "";
}

}	// namespace

bool environment(unsigned cmd, void *data, bool& handled)
{
	handled = true;
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
		*(unsigned *)data = 2;
		return true;
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
	{
		const auto *opts = (const retro_core_options_v2 *)data;
		for (const retro_core_option_v2_definition *d = opts->definitions; d != nullptr && d->key != nullptr; d++)
		{
			CoreOption o;
			for (const retro_core_option_value *v = d->values; v->value != nullptr; v++)
				o.values.push_back(v->value);
			o.fallback = d->default_value != nullptr ? d->default_value : (o.values.empty() ? "" : o.values[0]);
			coreOptions[d->key] = std::move(o);
		}
		return true;
	}
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
		return environment(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, (void *)((const retro_core_options_v2_intl *)data)->us, handled);
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
	case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
		return true;
	case RETRO_ENVIRONMENT_GET_VARIABLE:
	{
		auto *var = (retro_variable *)data;
		const std::string value = coreValue(var->key);
		if (value.empty())
		{
			var->value = nullptr;
			return false;
		}
		std::string& kept = answers[var->key];
		kept = value;
		var->value = kept.c_str();
		return true;
	}
	case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
		*(bool *)data = coreUpdate;
		coreUpdate = false;
		return true;
	default:
		handled = false;
		return false;
	}
}

bool showFps()
{
	return fileValue("deadstation", "ShowFPS", 0) != 0;
}

bool nativeResolution()
{
	// tests: DEADSTATION_RESOLUTION=1024 or 640, over emu.cfg
	if (const char *test = getenv("DEADSTATION_RESOLUTION"); test != nullptr && *test != '\0')
		return strcmp(test, "1024") == 0;
	return fileValue("deadstation", "Resolution", 0) != 0;
}

bool smoothing()
{
	return fileValue("deadstation", "Smoothing", 1) != 0;
}

void force(const std::string& key, const std::string& value)
{
	forced[key] = value;
	coreUpdate = true;
}

options::Store store()
{
	options::Store file = options::file();
	return {
		file.get,
		[file](const options::Setting& setting, int value) {
			file.set(setting, value);
			if (strcmp(setting.section, "core") == 0)
			{
				pending |= CoreOptions;
				coreUpdate = true;
			}
			else if (strcmp(setting.key, "Resolution") == 0)
			{
				pending |= Resolution | CoreOptions;
				coreUpdate = true;
			}
			else if (strcmp(setting.key, "Smoothing") == 0)
				pending |= Smoothing;
		},
	};
}

unsigned changed()
{
	const unsigned c = pending;
	pending = 0;
	return c;
}

}
