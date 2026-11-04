// Deadstation's menu in a recomp module: MENU opens the same menu as the emulator's (frontend/ui.h) over the game, on the
// panel's pictures as the emulator's (core/deadstation/menu.cpp). While it is open the module's thread stays here, so the
// guest is held where it was (its clock, its game logic), the render thread has nothing to do and the audio is paused; on
// Continue all of it carries on. Its pages are Deadstation's: Save and Load (the emulator's save states) disabled, Emulator
// Options' settings kept in emu.cfg for the emulator's next start, Recomp Options the module's. A module has no menu of its
// own: its settings are Recomp Options', declared in its manifest (frontend/recomp.h).
//
// Show FPS is applied at once, and whatever setting the module's plugin applies itself (deadstation_live_settings). Its other
// settings (Resolution, Lighting) are read by its run.sh as it starts; changing one makes the main page's Continue
// read Restart.
#include "log.h"
#include "psx_gpu.h"
#include "recomp.h"
#include "ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern "C" void psx_host_audio_hold(int held);	// patch 0029

namespace
{

namespace ui = deadstation::ui;
namespace options = deadstation::options;
namespace recomp = deadstation::recomp;

// The Brick's pad, read here as well as by the runtime's SDL (which only sees it while the guest runs)
int openPad()
{
	char path[32], name[128];
	for (int i = 0; i < 16; i++)
	{
		std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
		const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && std::strcmp(name, "TRIMUI Player1") == 0)
			return fd;
		close(fd);
	}
	DS_LOG("recomp module: no pad to read for the menu");
	return -1;
}

// the pages on the screen: the panel's pictures, as the emulator's
class PanelDisplay : public ui::Display
{
public:
	bool begin(int& width, int& height) override
	{
		return hostVideo().ready() && hostVideo().panel().beginPictures(width, height);
	}
	void show(const deadstation::u32 *pixels) override
	{
		hostVideo().panel().showPicture(pixels);
	}
	void end(After after) override
	{
		hostVideo().panel().endPictures(after == After::LastFrame ? Panel::After::LastFrame
				: after == After::NextFrame ? Panel::After::NextFrame : Panel::After::Stay);
	}
};
PanelDisplay display;

enum class State { Closed, Hiding, Open };
State state = State::Closed;
int pad = -1;
bool padTried = false;
int hatX = 0, hatY = 0;

std::string discPath()
{
	const char *disc = std::getenv("DEADSTATION_DISC");
	return disc != nullptr ? disc : "";
}

std::string title()
{
	const char *rom = std::getenv("DEADSTATION_ROM");
	return ui::gameName(rom != nullptr && *rom != '\0' ? rom : discPath());
}

bool (*liveSetting)(const char *key, const char *value) = nullptr;

// the module's settings as it started: what a change is measured against
recomp::Module module;
std::map<std::string, std::string> started;
bool startedKept = false;

void keepStarted()
{
	if (startedKept)
		return;
	startedKept = true;
	if (!recomp::installed(recomp::chdSha1(discPath()), module))
		return;
	for (const recomp::Setting& s : module.settings)
		started[s.key] = recomp::get(module.id + "." + s.key, s.def);
}

bool settingsChanged()
{
	for (const recomp::Setting& s : module.settings)
		if ((liveSetting == nullptr || !liveSetting(s.key.c_str(), nullptr)) && recomp::get(module.id + "." + s.key, s.def) != started[s.key])
			return true;
	return false;
}

ui::Host host()
{
	ui::Host h;
	h.core = ui::Core::Recomp;
	h.disc = discPath();
	h.emulatorSettings = options::file();
	h.recompSetting = [](const std::string& key, const std::string& value) {
		if (key == "fps")
			hostShowFps(value == "on");
		else if (liveSetting != nullptr)
			liveSetting(key.c_str(), value.c_str());
	};
	h.settingsChanged = settingsChanged;
	h.settingLive = [](const std::string& key) { return liveSetting != nullptr && liveSetting(key.c_str(), nullptr); };
	return h;
}

// the pad's buttons and d-pad as the menu's
void press(const input_event& ev)
{
	if (ev.type == EV_KEY && ev.value != 2)
	{
		const ui::Button b = ev.code == BTN_EAST ? ui::Button::A : ev.code == BTN_SOUTH ? ui::Button::B
				: ev.code == BTN_WEST ? ui::Button::X : ev.code == BTN_MODE ? ui::Button::Menu : ui::Button::None;
		if (b != ui::Button::None)
			ui::button(b, ev.value == 1);
	}
	else if (ev.type == EV_ABS && (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y))
	{
		int& was = ev.code == ABS_HAT0X ? hatX : hatY;
		const ui::Button less = ev.code == ABS_HAT0X ? ui::Button::Left : ui::Button::Up;
		const ui::Button more = ev.code == ABS_HAT0X ? ui::Button::Right : ui::Button::Down;
		if (was != 0)
			ui::button(was < 0 ? less : more, false);
		if (ev.value != 0)
			ui::button(ev.value < 0 ? less : more, true);
		was = ev.value;
	}
}

// a screenshot asked for (SIGUSR2) while the menu is up: what it drew
void shotOfMenu()
{
	if (!hostTakeShotWanted())
		return;
	const deadstation::Canvas& c = ui::canvas();
	FILE *f = std::fopen("/tmp/ds-shot.ppm.tmp", "wb");
	if (f == nullptr)
		return;
	std::fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
	std::vector<uint8_t> row((size_t)c.width() * 3);
	const deadstation::u32 *p = c.data();
	for (int y = 0; y < c.height(); y++)
	{
		for (int x = 0; x < c.width(); x++, p++)
			row[(size_t)x * 3] = (uint8_t)(*p >> 16), row[(size_t)x * 3 + 1] = (uint8_t)(*p >> 8), row[(size_t)x * 3 + 2] = (uint8_t)*p;
		std::fwrite(row.data(), 1, row.size(), f);
	}
	std::fclose(f);
	std::rename("/tmp/ds-shot.ppm.tmp", "/tmp/ds-shot.ppm");
}

// the menu, the guest held, until the player leaves it
void runMenu()
{
	psx_gpu_idle();
	Video& video = hostVideo();
	video.panel().drain();
	// the game's picture, the menu's backdrop (without the corner's counter: hidden a frame ago)
	std::vector<uint8_t> frame;
	int width = 0, height = 0;
	if (!video.panel().readShown(frame, width, height))
		frame.clear();
	keepStarted();
	ui::setDisplay(&display);
	psx_host_audio_hold(1);
	if (!ui::open(frame, width, height, ui::mainScreen(title(), host())))
	{
		DS_LOG("recomp module: the menu cannot open");
		psx_host_audio_hold(0);
		hostMenuClosed();
		state = State::Closed;
		return;
	}
	state = State::Open;
	DS_LOG("recomp module: the menu open, the game held");
	for (;;)
	{
		input_event ev;
		while (pad >= 0 && read(pad, &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
			press(ev);
		const ui::Result result = ui::frame();
		if (result == ui::Result::Open)
		{
			shotOfMenu();
			usleep(16000);
			continue;
		}
		if (result == ui::Result::Quit || result == ui::Result::Switch)
		{
			// this run ended as it should (frontend/recomp.h). Switch: the launcher starts the game again, in the core now
			// chosen, with the settings now chosen; the page left up says Loading... until then
			const int code = result == ui::Result::Switch ? recomp::SwitchExit : 0;
			DS_LOG("recomp module: leaving by the menu (%d)", code);
			recomp::runEnded();
			hostStop(code);
			_exit(code);
		}
		break;
	}
	psx_host_audio_hold(0);
	hostMenuClosed();
	state = State::Closed;
	DS_LOG("recomp module: the menu closed, the game goes on");
}

}

extern "C" void deadstation_live_settings(bool (*apply)(const char *key, const char *value))
{
	liveSetting = apply;
}

void moduleMenuPoll()
{
	if (!padTried)
	{
		padTried = true;
		pad = openPad();
	}
	bool menu = false;
	input_event ev;
	while (pad >= 0 && read(pad, &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
		if (ev.type == EV_KEY && ev.code == BTN_MODE && ev.value == 1)
			menu = true;
		else if (ev.type == EV_ABS && ev.code == ABS_HAT0X)
			hatX = ev.value;
		else if (ev.type == EV_ABS && ev.code == ABS_HAT0Y)
			hatY = ev.value;
	if (state == State::Hiding)
		runMenu();
	else if (menu && state == State::Closed)
	{
		// the corner's counter off the picture the menu is drawn over: one more frame without it, if it was up
		psx_gpu_idle();
		if (hostHideFps())
			state = State::Hiding;
		else
			runMenu();
	}
}
