// Deadstation: PCSX ReARMed's core (its libretro interface, linked in) run on the Brick, with the shared
// menu (ui/). Pictures go through Vulkan to fb0 (video.h); SDL2 reads the pad and plays the sound; nothing else
// of SDL runs (its video would take fb0).
//   deadstation <game>        a .chd, .cue, .pbp or an .m3u of them
// Environment (launch.sh): DEADSTATION_HOME (settings, memory cards, save states), DEADSTATION_BIOS (the
// folder with the BIOS), DEADSTATION_ROM (the file NextUI started, for the menu's title).
// Tests: DEADSTATION_STATE=<file> loads that save state once the game is loaded; DEADSTATION_SAVE=<file>
// saves one there as the game quits; DEADSTATION_CORE="key=value,..." sets core options for the run;
// DEADSTATION_RESOLUTION=640|1024 picks the resolution; DEADSTATION_PERF=1 logs a perf line a second;
// DEADSTATION_TEST_SESSION=host|guest starts a two-player session, the other Brick's part played by another program on
// this one, on port DEADSTATION_TEST_PEER_PORT, its memory card from file DEADSTATION_TEST_PEER_CARD.
#include "controls.h"
#include "link.h"
#include "netplay.h"
#include "log.h"
#include "menu.h"
#include "dead_ui.h"
#include "osd.h"
#include "settings.h"
#include "states.h"
#include "video.h"

#include <libretro.h>
#include <SDL.h>
#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace deadstation;

namespace
{
Video video;
Video::Format pixelFormat = Video::Format::RGB565;
std::string homeDir = ".";
std::string biosDir = ".";
SDL_AudioDeviceID audioDevice = 0;
uint32_t audioLimit = 0;		// bytes queued at most (more is dropped)
SDL_Joystick *joystick = nullptr;
int16_t padBits = 0;			// RETRO_DEVICE_ID_JOYPAD_* bits of port 0
bool fastForward = false;
unsigned presented = 0;			// frames drawn (Show FPS)
std::atomic<bool> quit { false };
std::atomic<bool> shotWanted { false };		// SIGUSR1 (tests): the page on show into /tmp/ds-shot.ppm

void logCallback(enum retro_log_level level, const char *fmt, ...)
{
	if (level == RETRO_LOG_DEBUG)
		return;
	va_list args;
	va_start(args, fmt);
	std::fputs("pcsx: ", stderr);
	std::vfprintf(stderr, fmt, args);
	va_end(args);
}

bool environment(unsigned cmd, void *data)
{
	bool handled = false;
	const bool result = settings::environment(cmd, data, handled);
	if (handled)
		return result;
	switch (cmd)
	{
	case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
		*(const char **)data = biosDir.c_str();
		return true;
	case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
		*(const char **)data = homeDir.c_str();
		return true;
	case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
	{
		const auto fmt = *(const retro_pixel_format *)data;
		if (fmt == RETRO_PIXEL_FORMAT_RGB565)
			pixelFormat = Video::Format::RGB565;
		else if (fmt == RETRO_PIXEL_FORMAT_XRGB8888)
			pixelFormat = Video::Format::XRGB8888;
		else
			return false;
		return true;
	}
	case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
		((retro_log_callback *)data)->log = logCallback;
		return true;
	case RETRO_ENVIRONMENT_GET_CAN_DUPE:
	case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
		if (data != nullptr)
			*(bool *)data = true;
		return true;
	case RETRO_ENVIRONMENT_SET_MESSAGE:
	{
		const auto *message = (const retro_message *)data;
		DS_LOG("core: %s", message->msg);
		osd::notify(message->msg, (int)(message->frames * 1000 / 60));
		return true;
	}
	case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
	{
		const auto *message = (const retro_message_ext *)data;
		DS_LOG("core: %s", message->msg);
		if (message->target != RETRO_MESSAGE_TARGET_LOG)
			osd::notify(message->msg, (int)message->duration);
		return true;
	}
	default:
		return false;
	}
}

void videoRefresh(const void *data, unsigned width, unsigned height, size_t pitch)
{
	// a two-player start: its page stays up until the other Brick is heard (the first frames run without it)
	if (netplay::active() && !netplay::heardPeer())
		return;
	// the first frame takes the start page down
	if (data != nullptr && menu::starting())
		menu::endStart();
	// data == nullptr: the same frame again; the screen keeps the last one
	if (data != nullptr)
	{
		static unsigned lastW, lastH;
		if (width != lastW || height != lastH)
		{
			DS_LOG("frames %u x %u", width, height);
			lastW = width;
			lastH = height;
		}
		video.frame(data, width, height, pitch, pixelFormat);
		presented++;
	}
}

void audioSample(int16_t left, int16_t right)
{
	const int16_t frame[2] = { left, right };
	if (audioDevice != 0 && !fastForward && SDL_GetQueuedAudioSize(audioDevice) < audioLimit)
		SDL_QueueAudio(audioDevice, frame, sizeof(frame));
}

size_t audioBatch(const int16_t *data, size_t frames)
{
	if (audioDevice != 0 && !fastForward && SDL_GetQueuedAudioSize(audioDevice) < audioLimit)
		SDL_QueueAudio(audioDevice, data, (Uint32)(frames * 4));
	return frames;
}

void inputPoll()
{
	// a two-player session's pads were settled for this frame by netplay::step
	if (!netplay::active())
		padBits = controls::padBits();
}

int16_t inputState(unsigned port, unsigned device, unsigned, unsigned id)
{
	if (port > 1 || device != RETRO_DEVICE_JOYPAD)
		return 0;
	const int16_t bits = netplay::active() ? netplay::pad(port) : port == 0 ? padBits : 0;
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
		return bits;
	return id < 16 ? (bits >> id) & 1 : 0;
}


// the first disc of an .m3u (relative to it), else the file itself: what the disc's identity is read from
std::string firstDisc(const std::string& path)
{
	if (path.size() < 4 || strcasecmp(path.c_str() + path.size() - 4, ".m3u") != 0)
		return path;
	std::ifstream in(path);
	for (std::string line; std::getline(in, line); )
	{
		while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		if (line.empty() || line[0] == '#')
			continue;
		if (line[0] == '/')
			return line;
		const size_t slash = path.find_last_of('/');
		return slash == std::string::npos ? line : path.substr(0, slash + 1) + line;
	}
	return path;
}

// the second memory card's memory (the core's): a two-player session's, in slot 2
extern "C" char Mcd2Data[];

// memory card 1: the core's save RAM, kept as <home>/<game>.srm (card 2 is the core's own file)
std::string memcardPath(const std::string& rom)
{
	std::string name = rom.substr(rom.find_last_of('/') + 1);
	const size_t dot = name.find_last_of('.');
	if (dot != std::string::npos)
		name.resize(dot);
	return homeDir + "/" + name + ".srm";
}

void loadMemcard(const std::string& path)
{
	auto *data = (char *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	const size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	std::ifstream in(path, std::ios::binary);
	if (data != nullptr && size != 0 && in)
		in.read(data, (std::streamsize)size);
}

// a card's memory into its file, in one go
void saveCard(const std::string& path, const char *data, size_t size)
{
	if (data == nullptr || size == 0)
		return;
	const std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out.write(data, (std::streamsize)size);
		if (!out)
		{
			DS_LOG("memory card not saved to %s", path.c_str());
			return;
		}
	}
	std::rename(tmp.c_str(), path.c_str());
}

void saveMemcard(const std::string& path)
{
	saveCard(path, (const char *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM), retro_get_memory_size(RETRO_MEMORY_SAVE_RAM));
}

// a two-player session starting (the link's page, then the menu's Restart): the same settings on both Bricks
// for determinism, the game loaded again from nothing (a reset keeps RAM), both pads in. As on a PlayStation with two
// controllers, each player brings a card: player 1's in slot 1, player 2's in slot 2 on both Bricks (the link swaps
// them), kept in its owner's file by its owner's Brick. Slot 2 stays in memory, whatever the core's own card file holds.
std::string gamePath;
std::string memcardFile;

void startSession()
{
	settings::force("pcsx_rearmed_drc_thread", "disabled");
	settings::force("pcsx_rearmed_spu_thread", "disabled");
	settings::force("pcsx_rearmed_gpu_thread_rendering", "disabled");
	settings::force("pcsx_rearmed_region", "auto");
	settings::force("pcsx_rearmed_frameskip_type", "disabled");
	settings::force("pcsx_rearmed_memcard2", "libretro");
	saveMemcard(memcardFile);
	retro_unload_game();
	retro_deinit();
	retro_init();
	retro_game_info info = { gamePath.c_str(), nullptr, 0, nullptr };
	if (!retro_load_game(&info))
	{
		DS_LOG("netplay: the game could not be loaded again");
		quit = true;
		return;
	}
	retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
	std::vector<u8> peerCard = link::peerVmu();
	if (const char *test = getenv("DEADSTATION_TEST_PEER_CARD"))
	{
		std::ifstream file(test, std::ios::binary);
		peerCard.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}
	auto *slot1 = (u8 *)retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	const size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	if (netplay::host())
	{
		loadMemcard(memcardFile);
		if (!peerCard.empty())
			memcpy(Mcd2Data, peerCard.data(), std::min(size, peerCard.size()));
	}
	else
	{
		if (slot1 != nullptr && !peerCard.empty())
			memcpy(slot1, peerCard.data(), std::min(size, peerCard.size()));
		std::ifstream own(memcardFile, std::ios::binary);
		if (own)
			own.read(Mcd2Data, (std::streamsize)size);
	}
	DS_LOG("netplay: the game from its start, as player %d; the cards' CRCs: slot 1 %08lx, slot 2 %08lx", netplay::host() ? 1 : 2,
			slot1 != nullptr ? crc32(0, slot1, (uInt)size) : 0ul, crc32(0, (const Bytef *)Mcd2Data, (uInt)size));
}

// a PNG of the last frame in the SD card's Screenshots folder
void screenshot(const std::string& rom)
{
	std::vector<uint8_t> rgb;
	int w = 0, h = 0;
	if (!video.lastFrame(rgb, w, h))
		return;
	const char *sd = getenv("SDCARD_PATH");
	const std::string dir = std::string(sd != nullptr ? sd : "/mnt/SDCARD") + "/Screenshots";
	mkdir(dir.c_str(), 0755);
	char stamp[32];
	const time_t now = time(nullptr);
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
	const std::string path = dir + "/" + ui::gameName(rom) + "." + stamp + ".png";
	std::vector<uint8_t> raw;
	raw.reserve((size_t)(w * 3 + 1) * h);
	for (int y = 0; y < h; y++)
	{
		raw.push_back(0);
		raw.insert(raw.end(), rgb.begin() + (size_t)y * w * 3, rgb.begin() + (size_t)(y + 1) * w * 3);
	}
	uLongf zsize = compressBound((uLong)raw.size());
	std::vector<uint8_t> z(zsize);
	if (compress2(z.data(), &zsize, raw.data(), (uLong)raw.size(), 6) != Z_OK)
		return;
	z.resize(zsize);
	std::ofstream out(path, std::ios::binary);
	const auto be = [&](uint32_t v) { const uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; out.write((const char *)b, 4); };
	const auto chunk = [&](const char *type, const std::vector<uint8_t>& data) {
		be((uint32_t)data.size());
		uLong crc = crc32(0, (const Bytef *)type, 4);
		crc = crc32(crc, data.data(), (uInt)data.size());
		out.write(type, 4);
		out.write((const char *)data.data(), (std::streamsize)data.size());
		be((uint32_t)crc);
	};
	out.write("\x89PNG\r\n\x1a\n", 8);
	std::vector<uint8_t> ihdr = { (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
			(uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0 };
	chunk("IHDR", ihdr);
	chunk("IDAT", z);
	chunk("IEND", {});
	DS_LOG("screenshot: %s", path.c_str());
	osd::notify("Screenshot saved", 2000);
}

void addNs(timespec& t, long ns)
{
	t.tv_nsec += ns;
	while (t.tv_nsec >= 1000000000L)
	{
		t.tv_nsec -= 1000000000L;
		t.tv_sec++;
	}
}

double secondsBetween(const timespec& a, const timespec& b)
{
	return (double)(b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) * 1e-9;
}

bool ntscGame = false;		// the panel at its rate, whenever the screen is (re)made

void matchPanel()
{
	if (ntscGame && !video.panel().matchRate())
		osd::notify("Panel rate not matched", 3000);
}

bool startVideo()
{
	std::string error;
	if (video.init(settings::nativeResolution(), settings::smoothing(), error))
	{
		matchPanel();
		return true;
	}
	DS_LOG("the screen: %s", error.c_str());
	return false;
}

// the pad's changes since the last call: (input, pressed) pairs
std::vector<std::pair<int, bool>> padEvents()
{
	std::vector<std::pair<int, bool>> events;
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		switch (event.type)
		{
		case SDL_QUIT:
			quit = true;
			break;
		case SDL_JOYDEVICEADDED:
			if (joystick == nullptr)
			{
				joystick = SDL_JoystickOpen(event.jdevice.which);
				controls::open(joystick);
			}
			break;
		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			events.push_back({ event.jbutton.button, event.type == SDL_JOYBUTTONDOWN });
			break;
		case SDL_JOYHATMOTION:
		{
			static Uint8 lastHat = 0;
			static const Uint8 Bits[] = { SDL_HAT_UP, SDL_HAT_DOWN, SDL_HAT_LEFT, SDL_HAT_RIGHT };
			for (int i = 0; i < 4; i++)
				if ((event.jhat.value & Bits[i]) != (lastHat & Bits[i]))
					events.push_back({ controls::HatUp + i, (event.jhat.value & Bits[i]) != 0 });
			lastHat = event.jhat.value;
			break;
		}
		case SDL_JOYAXISMOTION:
		{
			static bool down[8];
			if (event.jaxis.axis < 8)
			{
				const bool now = event.jaxis.value > 16384;
				if (now != down[event.jaxis.axis])
					events.push_back({ controls::Axis + event.jaxis.axis, now });
				down[event.jaxis.axis] = now;
			}
			break;
		}
		default:
			break;
		}
	}
	return events;
}
}	// namespace

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--restore-panel") == 0)
	{
		Panel::restoreLeftRate();
		return 0;
	}
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: deadstation <game>\n");
		return 2;
	}
	const std::string game = argv[1];
	const char *romEnv = getenv("DEADSTATION_ROM");
	const std::string rom = romEnv != nullptr && *romEnv != '\0' ? romEnv : game;
	if (const char *home = getenv("DEADSTATION_HOME"))
		homeDir = home;
	if (const char *bios = getenv("DEADSTATION_BIOS"))
		biosDir = bios;
	DS_LOG("%s, game %s, BIOS from %s", DEADSTATION_VERSION, game.c_str(), biosDir.c_str());

	// recomp modules marked for deletion deleted, an install cut short tidied away; then the disc's recomp module
	// hosted, if that is its chosen core (no emulator started)
	recomp::tidy();
	if (recomp::get("app.version") != DEADSTATION_APP_VERSION)
		recomp::set("app.version", DEADSTATION_APP_VERSION);
	const std::string disc = firstDisc(game);
	recomp::runChosen(disc, [] {
		menu::openHostedControls(
				[] {
					if (SDL_Init(SDL_INIT_JOYSTICK) != 0)
						return false;
					if (SDL_NumJoysticks() > 0)
						joystick = SDL_JoystickOpen(0);
					controls::open(joystick);
					controls::load();
					return joystick != nullptr;
				},
				[] {
					if (joystick != nullptr)
						SDL_JoystickClose(joystick);
					joystick = nullptr;
					controls::open(nullptr);
					SDL_Quit();
				},
				padEvents);
	});

	signal(SIGTERM, [](int) { quit = true; });
	signal(SIGUSR1, [](int) { shotWanted = true; });
	signal(SIGINT, [](int) { quit = true; });
	if (const char *core = getenv("DEADSTATION_CORE"))
	{
		std::string list = core;
		for (size_t pos = 0; pos <= list.size(); )
		{
			const size_t comma = std::min(list.find(',', pos), list.size());
			const std::string pair = list.substr(pos, comma - pos);
			const size_t eq = pair.find('=');
			if (eq != std::string::npos)
				settings::force(pair.substr(0, eq), pair.substr(eq + 1));
			pos = comma + 1;
		}
	}

	if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0)
	{
		DS_LOG("SDL: %s", SDL_GetError());
		return 1;
	}
	if (SDL_NumJoysticks() > 0)
		joystick = SDL_JoystickOpen(0);
	controls::open(joystick);
	controls::load();
	states::setGame(rom);
	const std::string memcard = memcardPath(rom);
	gamePath = game;
	memcardFile = memcard;

	if (!startVideo())
	{
		SDL_Quit();
		return 1;
	}
	menu::init(&video, rom, disc, memcard, [] { saveMemcard(memcardFile); });
	osd::init(&video);
	menu::showStart();

	retro_set_environment(environment);
	retro_set_video_refresh(videoRefresh);
	retro_set_audio_sample(audioSample);
	retro_set_audio_sample_batch(audioBatch);
	retro_set_input_poll(inputPoll);
	retro_set_input_state(inputState);
	retro_init();

	retro_game_info info = { game.c_str(), nullptr, 0, nullptr };
	if (!retro_load_game(&info))
	{
		DS_LOG("the core could not load %s", game.c_str());
		menu::showStartError("The game could not be loaded.");
		retro_deinit();
		video.term();
		SDL_Quit();
		return 1;
	}
	retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
	loadMemcard(memcard);
	if (const char *state = getenv("DEADSTATION_STATE"); state != nullptr && *state != '\0')
		DS_LOG("test: state %s %s", state, states::load(state) ? "loaded" : "NOT loaded");

	retro_system_av_info av{};
	retro_get_system_av_info(&av);
	const double fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
	const int rate = av.timing.sample_rate > 0 ? (int)av.timing.sample_rate : 44100;
	DS_LOG("core: %.3f fps, %d Hz sound, %u x %u", fps, rate, av.geometry.base_width, av.geometry.base_height);
	// (a PAL game's 50 Hz is too far from the panel's to match)
	ntscGame = fps > 55.0;
	matchPanel();
	SDL_AudioSpec want{}, have{};
	want.freq = rate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 512;
	audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (audioDevice == 0)
		DS_LOG("no sound: %s", SDL_GetError());
	else
	{
		audioLimit = (uint32_t)(have.freq * 4 / 10);		// 100 ms
		SDL_PauseAudioDevice(audioDevice, 0);
	}

	// one core frame per frame period; the presents wait only when the GPU or the panel falls behind
	const long period = (long)(1e9 / fps);
	timespec next{};
	clock_gettime(CLOCK_MONOTONIC, &next);
	timespec statFrom = next;
	unsigned frames = 0;
	unsigned shownFrom = 0;
	// perf: the core's time per frame (retro_run), its worst, and the frames it took longer than one period
	const bool perfEverySecond = getenv("DEADSTATION_PERF") != nullptr;
	double runTime = 0, runWorst = 0;
	unsigned over = 0;
	int exitCode = 0;
	bool showFps = settings::showFps();	// as Options has it, read when the menu closes
	bool session = false;			// this run became a two-player session (save states off, card rules)
	uint32_t sessionFrame = 0;
	const char *testSession = getenv("DEADSTATION_TEST_SESSION");
	while (!quit)
	{
		if (testSession != nullptr && presented >= 60)
		{
			netplay::begin(strcmp(testSession, "host") == 0, "127.0.0.1", 2);
			testSession = nullptr;
			if (menu::open())
				ui::finish(ui::Result::Restart);
		}
		const auto events = padEvents();
		if (menu::isOpen())
		{
			for (const auto& [code, pressed] : events)
				menu::input(code, pressed);
			const menu::Result result = menu::frame();
			if (result == menu::Result::Open)
			{
				SDL_Delay(16);
				continue;
			}
			if (result == menu::Result::Quit)
				break;
			if (result == menu::Result::Switch)
			{
				exitCode = recomp::SwitchExit;
				break;
			}
			if (result == menu::Result::Restart)
			{
				if (netplay::active())
				{
					menu::showStartTwoPlayers();
					startSession();
					session = true;
					sessionFrame = 0;
				}
				else
					retro_reset();
			}
			else if (netplay::active())
				netplay::setPaused(false);
			showFps = settings::showFps();
			// back in the game: settings that remake the screen, then the clock from now
			if (settings::changed() & (settings::Resolution | settings::Smoothing))
			{
				video.term();
				if (!startVideo())
					break;
				osd::again();
				video.again();
			}
			if (audioDevice != 0)
			{
				SDL_ClearQueuedAudio(audioDevice);
				SDL_PauseAudioDevice(audioDevice, 0);
			}
			clock_gettime(CLOCK_MONOTONIC, &next);
			continue;
		}
		bool openMenu = false;
		for (const auto& [code, pressed] : events)
		{
			if (!pressed)
				continue;
			if (code == controls::MenuButton)
				openMenu = true;
			else if (netplay::active())
				continue;		// no save states or screenshots' worth of pause in a session
			else if (code == controls::binding(controls::SaveState))
				osd::notify(menu::saveSlot() ? "State saved" : "The game could not be saved", 2000);
			else if (code == controls::binding(controls::LoadState))
			{
				if (!menu::slotExists())
					osd::notify("Save state not found", 2000);
				else if (!menu::loadSlot())
					osd::notify("The game could not be loaded", 2000);
			}
			else if (code == controls::binding(controls::Screenshot))
				screenshot(rom);
		}
		if (openMenu)
		{
			if (audioDevice != 0)
				SDL_PauseAudioDevice(audioDevice, 1);
			if (netplay::active())
			{
				netplay::setPaused(true);
				if (menu::openSession())
					continue;
				netplay::setPaused(false);
			}
			else if (menu::open())
				continue;
			if (audioDevice != 0)
				SDL_PauseAudioDevice(audioDevice, 0);
		}
		fastForward = !netplay::active() && controls::held(controls::FastForward);
		// a session: this frame's pads from both Bricks; the frame waits for the other's
		if (netplay::active())
		{
			if (!netplay::step(controls::padBits()))
			{
				if (netplay::pauseState().pause != netplay::Pause::None)
				{
					if (audioDevice != 0)
						SDL_PauseAudioDevice(audioDevice, 1);
					menu::openSession();
				}
				continue;
			}
			padBits = netplay::pad(0);
		}
		if (shotWanted.exchange(false))
		{
			// what the display shows, read back from fb0 uncached (a read() of /dev/fb0 can be stale)
			std::vector<uint8_t> rgb;
			int w = 0, h = 0;
			video.panel().drain();
			if (video.panel().readShown(rgb, w, h))
			{
				std::ofstream out("/tmp/ds-shot.ppm.tmp", std::ios::binary);
				out << "P6\n" << w << " " << h << "\n255\n";
				out.write((const char *)rgb.data(), (std::streamsize)rgb.size());
				out.close();
				std::rename("/tmp/ds-shot.ppm.tmp", "/tmp/ds-shot.ppm");
			}
		}

		timespec before{};
		clock_gettime(CLOCK_MONOTONIC, &before);
		const unsigned presentedBefore = presented;
		retro_run();
		// (not over the start page: the text's last frame again would take its place)
		if (!menu::starting())
			osd::frame(presented != presentedBefore, showFps, fastForward);
		frames++;
		if (netplay::active())
			netplay::frameDone(++sessionFrame, (const uint8_t *)retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM),
					retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM));
		timespec now{};
		clock_gettime(CLOCK_MONOTONIC, &now);
		const double took = secondsBetween(before, now);
		runTime += took;
		runWorst = std::max(runWorst, took);
		if (took * 1e9 > period)
			over++;
		if (!fastForward)
		{
			addNs(next, period);
			const double behind = secondsBetween(next, now);
			if (behind > 0.1)
				next = now;		// too far behind: start counting again
			else if (behind < 0)
				clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);
		}
		else
			next = now;
		const double span = secondsBetween(statFrom, now);
		if (span >= 1.0)
		{
			const double shown = (presented - shownFrom) / span;
			static int perfCount = 0;
			if (perfEverySecond || ++perfCount % 10 == 0)
			{
				const double waited = video.takeWaited();
				DS_LOG("perf: %.1f frames/s run, %.1f drawn, core %.2f ms a frame, worst %.2f, %u over %.2f, present wait %.2f ms a frame",
						frames / span, shown, frames != 0 ? (runTime - waited) * 1e3 / frames : 0.0, runWorst * 1e3, over, period * 1e-6,
						frames != 0 ? waited * 1e3 / frames : 0.0);
			}
			runTime = runWorst = 0;
			over = 0;
			frames = 0;
			shownFrom = presented;
			statFrom = now;
		}
	}

	DS_LOG("quitting");
	if (const char *save = getenv("DEADSTATION_SAVE"); save != nullptr && *save != '\0')
		DS_LOG("test: state %s %s", save, states::save(save, {}, 0, 0) ? "saved" : "NOT saved");
	// each player's card to its file, by its own Brick: a session's player 2's is in slot 2
	if (session && !netplay::host())
		saveCard(memcard, Mcd2Data, retro_get_memory_size(RETRO_MEMORY_SAVE_RAM));
	else
		saveMemcard(memcard);
	netplay::leave();
	link::stop();
	link::finish();
	if (audioDevice != 0)
		SDL_CloseAudioDevice(audioDevice);
	retro_unload_game();
	retro_deinit();
	video.term();
	if (joystick != nullptr)
		SDL_JoystickClose(joystick);
	SDL_Quit();
	return exitCode;
}
