#include "menu.h"
#include "controls.h"
#include "dead_ui.h"
#include "link.h"
#include "netplay.h"
#include "log.h"
#include "settings.h"
#include "states.h"
#include "video.h"

#include <chrono>
#include <fstream>
#include <memory>
#include <thread>

// Deadstation's side of the menu (ui/src/ui.h draws it): its settings for Options (settings.h), its own
// pages (Controls, Local Multiplayer and the link, a two-player session's), its save states, the start page,
// and the screen and pad.
namespace deadstation::menu
{
namespace
{

using ui::Item;
using ui::Row;
using ui::Screen;
using ui::entry;
using ui::S;

// the pages on the screen: the panel's pictures
class PanelDisplay : public ui::Display
{
public:
	Video *video = nullptr;
	bool begin(int& width, int& height) override
	{
		return video != nullptr && video->ready() && video->panel().beginPictures(width, height);
	}
	void show(const u32 *pixels) override
	{
		video->panel().showPicture(pixels);
	}
	void end(After after) override
	{
		video->panel().endPictures(after == After::LastFrame ? Panel::After::LastFrame
				: after == After::NextFrame ? Panel::After::NextFrame : Panel::After::Stay);
	}
};
PanelDisplay display;

struct State
{
	std::string rom;				// the file NextUI started
	std::string disc;				// the disc played
	std::vector<u8> frame;			// the game's, when the menu opened (a saved state's picture)
	int frameWidth = 0;
	int frameHeight = 0;
	int slot = 0;
	int previewSlot = -1;			// the slot shown in the preview
	bool stateExists = false;
	std::vector<u8> preview;		// its picture
	int previewWidth = 0;
	int previewHeight = 0;
	int awaiting = -1;				// a Controls row waiting for a control (its function)
	std::string card;				// this player's memory card file
	std::function<void()> saveCard;
	bool link = false;				// the link's page open
	u32 linkVersion = 0;			// the link's status it shows
	std::string linkFailure;		// why the link cannot go on, once it cannot
	std::chrono::steady_clock::time_point connectedAt;
	bool connected = false;
	netplay::PauseState shownPause;	// a session's menu: the pause it shows
};
State st;

std::string fileName(const std::string& path)
{
	const size_t slash = path.find_last_of('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string displayName()
{
	const std::string name = ui::gameName(st.rom);
	return name.empty() ? fileName(st.rom) : name;
}

// the Controls page's rows: what each is bound to now
void refreshBindings(Screen& screen)
{
	const auto& list = controls::list();
	for (size_t i = 0; i < screen.items.size() && i < list.size(); i++)
	{
		screen.items[i].values = { controls::inputName(controls::binding(list[i].function)) };
		screen.items[i].value = 0;
	}
}

Screen bindingsScreen()
{
	Screen screen;
	screen.title = "Controls";
	for (const controls::Control& control : controls::list())
	{
		Item item;
		item.name = control.name;
		item.desc = control.desc;
		item.row = Row::Binding;
		const controls::Function f = control.function;
		item.confirm = [f] {
			st.awaiting = f;
			ui::awaitControl(true);
		};
		item.clear = [f] {
			controls::unbind(f);
			refreshBindings(ui::page());
		};
		screen.items.push_back(std::move(item));
	}
	refreshBindings(screen);
	return screen;
}

void updatePreview()
{
	if (st.previewSlot == st.slot)
		return;
	st.previewSlot = st.slot;
	st.preview.clear();
	st.stateExists = states::exists(st.slot);
	if (st.stateExists && !states::picture(st.slot, st.preview, st.previewWidth, st.previewHeight))
		st.preview.clear();
}

// the selected slot's saved state, at the right of a page drawn at ox, the slots under it
void drawPreview(int ox)
{
	Canvas& c = ui::canvas();
	const int hw = c.width() / 2;
	const int hh = c.height() / 2;
	const int pw = hw + S(4 * 2);
	const int ph = hh + S(4 * 2 + 6 + 4);
	int x = ox + c.width() - pw - ui::Padding;
	int y = (c.height() - ph) / 2;
	c.roundRect(x, y, pw, ph, S(4), ui::White);
	x += S(4);
	y += S(4);
	c.fill(x, y, hw, hh, ui::Black);
	updatePreview();
	if (!st.preview.empty())
		c.picture(st.preview.data(), st.previewWidth, st.previewHeight, x, y, hw, hh);
	else
		ui::drawMessage(ui::fonts().large, st.stateExists ? "No Preview" : "Empty Slot", x, y, hw, hh);
	// the slots: the one picked a black dot, the others small gray ones
	const int px = x + (hw - S(15 * states::SlotCount)) / 2;
	const int py = y + hh + S(4);
	for (int i = 0; i < states::SlotCount; i++)
	{
		const int cx = px + S(15 * i) + S(15) / 2;
		const int cy = py + S(3);
		if (i == st.slot)
			c.circle(cx, cy, S(3), ui::Black);
		else
			c.circle(cx, cy, S(1), ui::LightGray);
	}
}


//
// Local Multiplayer: the link's page
//

// linked, tested, IP up, the same game and build on both: both start it again as two players
bool canStart(const link::Status& status)
{
	return status.state == link::State::Tested && status.role != link::Role::None && !status.address.empty()
			&& status.sameGame && status.sameVersion;
}

// why the link cannot go on (empty while it can)
std::string linkFailure(const link::Status& s)
{
	if (s.state == link::State::Failed)
		return s.error;
	if (s.role != link::Role::None && !s.sameVersion)
		return "Both Bricks require the same Deadstation version.\nPlease update Deadstation to the latest version.";
	if (s.role != link::Role::None && !s.sameGame)
		return "Both Bricks must be playing the same game.";
	return "";
}

std::string linkHeadline(const link::Status& s)
{
	if (s.role != link::Role::None)
		return canStart(s) ? "Connected" : "Connecting to device";
	if (s.activity == link::Activity::TurningOn)
		return "Turning on Bluetooth";
	if (s.activity == link::Activity::Connecting)
		return "Connecting to device";
	return "Locating device";
}

std::string linkHint(link::Via via)
{
	if (via == link::Via::Bluetooth)
		return "Open Bluetooth on both Bricks and keep them close.";
	return "Connect the Bricks with a USB-C cable: the top port of one to the bottom port of the other, "
			"or bottom to bottom (not top to top). Open USB Link on both.";
}

void drawLink(int ox)
{
	Canvas& c = ui::canvas();
	Font& large = ui::fonts().large;
	Font& small = ui::fonts().small;
	const link::Status s = link::status();
	const bool failed = !st.linkFailure.empty();
	const std::string title = failed ? "Connection failed" : linkHeadline(s);
	const std::string under = failed ? st.linkFailure : linkHint(s.via);
	const bool shown = failed || title != "Connected";
	const int x = ox + ui::Padding + ui::ButtonPadding;
	const int width = c.width() - (ui::Padding + ui::ButtonPadding) * 2;
	const std::vector<std::string> lines = ui::wrap(large, title, width);
	std::vector<std::string> wrapped;
	for (size_t start = 0, end; !under.empty() && start <= under.size(); start = end + 1)
	{
		end = under.find('\n', start);
		if (end == std::string::npos)
			end = under.size();
		for (const std::string& l : ui::wrap(small, under.substr(start, end - start), width))
			wrapped.push_back(l);
	}
	const int gap = wrapped.empty() ? 0 : S(4);
	const int height = (int)lines.size() * large.lineHeight() + gap + (int)wrapped.size() * small.lineHeight();
	const int top = ui::Padding + ui::PillSize;
	int y = top + (c.height() - ui::Padding - ui::PillSize - top - height) / 2;
	for (const std::string& l : lines)
	{
		c.text(large, l, x, y + large.ascent(), ui::White);
		y += large.lineHeight();
	}
	y += gap;
	for (const std::string& l : wrapped)
	{
		if (shown)
			c.text(small, l, x, y + small.ascent(), ui::TextGray);
		y += small.lineHeight();
	}
}

void closeLink()
{
	if (!link::kept())
		link::stop();
	st.link = false;
	ui::pop();
}

// the disc's identity, for the other Brick to compare: its CHD's SHA-1, else its file's name
std::string gameId()
{
	const std::string sha = recomp::chdSha1(st.disc);
	return sha.empty() ? fileName(st.disc) : sha;
}

void openLink(link::Via via)
{
	Screen screen;
	screen.title = via == link::Via::Bluetooth ? "Bluetooth" : "USB Link";
	screen.body = drawLink;
	screen.buttons = [] {
		return st.linkFailure.empty() ? ui::Buttons { { "B", "BACK" } } : ui::Buttons { { "A", "OKAY" } };
	};
	screen.back = closeLink;
	screen.act = [](ui::Button code) {
		if (code == ui::Button::A && !st.linkFailure.empty())
			closeLink();
	};
	screen.tick = [] {
		using namespace std::chrono;
		const link::Status s = link::status();
		if (s.version != st.linkVersion)
		{
			st.linkVersion = s.version;
			if (st.linkFailure.empty())
				st.linkFailure = linkFailure(s);
			ui::redraw();
		}
		if (!st.linkFailure.empty() || !canStart(s))
		{
			st.connected = false;
			return;
		}
		if (!st.connected)
		{
			st.connected = true;
			st.connectedAt = steady_clock::now();
		}
		else if (steady_clock::now() - st.connectedAt > seconds(1))
		{
			// the game again on both, as two players, from its start (main.cpp starts the session)
			link::setKept(true);
			netplay::begin(s.role == link::Role::Host, s.peerAddress, s.inputDelay);
			ui::finish(ui::Result::Restart);
		}
	};
	ui::push(std::move(screen));
	st.link = true;
	st.linkVersion = ~0u;
	st.linkFailure.clear();
	st.connected = false;
	// for the other Brick to check (two players only in the same game), and this player's card, as it is now, to swap
	st.saveCard();
	link::setGame(gameId(), st.card);
	link::start(via);
}

Screen localMultiplayerScreen()
{
	Screen screen;
	screen.title = "Local Multiplayer";
	screen.items.push_back(entry("USB Link", "Play with another Brick over a USB cable. In testing.", [] { openLink(link::Via::Usb); }));
	screen.items.push_back(entry("Bluetooth", "Play with another Brick nearby, no cable. In testing.", [] { openLink(link::Via::Bluetooth); }));
	return screen;
}

//
// A paused two-player session
//

std::string sessionTitle()
{
	switch (st.shownPause.pause)
	{
	case netplay::Pause::PeerMenu:
		return "Paused by the other player";
	case netplay::Pause::Lost:
		return st.shownPause.peerPresent ? "The other player is back" : "Lost the other Brick";
	default:
		return "Two players: paused";
	}
}

bool canContinue()
{
	return st.shownPause.peerPresent;
}

Screen sessionScreen()
{
	Screen screen;
	screen.title = sessionTitle();
	screen.back = [] {
		if (canContinue())
			ui::finish(ui::Result::Resume);
	};
	Item resume = entry("Continue", "", [] {
		if (canContinue())
			ui::finish(ui::Result::Resume);
	});
	resume.available = canContinue;
	screen.items.push_back(std::move(resume));
	// on alone, the game as it is (the other Brick told)
	screen.items.push_back(entry("Leave two-player game", "", [] {
		netplay::leave();
		ui::finish(ui::Result::Resume);
	}));
	screen.items.push_back(entry("Quit", "", [] { ui::finish(ui::Result::Quit); }));
	screen.tick = [] {
		const netplay::PauseState now = netplay::pauseState();
		if (!netplay::active())
			ui::finish(ui::Result::Resume);
		else if (now.pause != st.shownPause.pause || now.peerPresent != st.shownPause.peerPresent)
		{
			st.shownPause = now;
			ui::page().title = sessionTitle();
			ui::redraw();
		}
	};
	return screen;
}

bool saveState()
{
	if (netplay::active())
	{
		ui::message("No save states in a two-player game.\nThe game's own saves work.");
		return false;
	}
	if (!states::save(states::slotPath(st.slot), st.frame, st.frameWidth, st.frameHeight))
	{
		ui::message("The game could not be saved.");
		return false;
	}
	st.previewSlot = -1;
	return true;
}

bool loadState()
{
	if (netplay::active())
	{
		ui::message("No save states in a two-player game.");
		return false;
	}
	if (!states::load(states::slotPath(st.slot)))
	{
		ui::message("The game could not be loaded.");
		return false;
	}
	return true;
}

// the main page's Save and Load, on the slot picked with Left/Right, its saved state beside them
ui::Host host()
{
	ui::Host host;
	host.disc = st.disc;
	host.save = entry("Save", "", [] {
		ui::work("Saving...", fileName(st.rom), [] {
			if (saveState())
				ui::finish(ui::Result::Resume);
		});
	});
	host.load = entry("Load", "", [] {
		updatePreview();
		if (st.stateExists)
			ui::work("Loading...", fileName(st.rom), [] {
				if (loadState())
					ui::finish(ui::Result::Resume);
			});
	});
	for (Item *item : { &host.save, &host.load })
	{
		item->shift = [](int step) { st.slot = (st.slot + step + states::SlotCount) % states::SlotCount; };
		item->aside = drawPreview;
	}
	host.version = "Deadstation " DEADSTATION_VERSION;
	host.emulatorSettings = settings::store();
	host.controls = [] { ui::push(bindingsScreen()); };
	host.localMultiplayer = [] { ui::push(localMultiplayerScreen()); };
	return host;
}

ui::Button menuButton(int code)
{
	switch (code)
	{
	case controls::HatUp: return ui::Button::Up;
	case controls::HatDown: return ui::Button::Down;
	case controls::HatLeft: return ui::Button::Left;
	case controls::HatRight: return ui::Button::Right;
	case 1: return ui::Button::A;			// the Brick's A (east)
	case 0: return ui::Button::B;			// its B (south)
	case 3: return ui::Button::X;			// its X (north)
	case controls::MenuButton: return ui::Button::Menu;
	default: return ui::Button::None;
	}
}

}	// namespace

void init(Video *video, const std::string& rom, const std::string& disc, const std::string& card, std::function<void()> saveCard)
{
	st.saveCard = std::move(saveCard);
	display.video = video;
	ui::setDisplay(&display);
	st.rom = rom;
	st.disc = disc;
	st.card = card;
}

bool open()
{
	if (ui::isOpen())
		return true;
	if (!display.video->lastFrame(st.frame, st.frameWidth, st.frameHeight))
		st.frame.clear();
	st.previewSlot = -1;
	st.awaiting = -1;
	if (!ui::open(st.frame, st.frameWidth, st.frameHeight, ui::mainScreen(displayName(), host())))
	{
		st.frame.clear();
		return false;
	}
	return true;
}

bool openSession()
{
	if (ui::isOpen())
		return true;
	if (!display.video->lastFrame(st.frame, st.frameWidth, st.frameHeight))
		st.frame.clear();
	st.shownPause = netplay::pauseState();
	st.link = false;
	if (!ui::open(st.frame, st.frameWidth, st.frameHeight, sessionScreen()))
	{
		st.frame.clear();
		return false;
	}
	return true;
}

bool isOpen()
{
	return ui::isOpen();
}

void input(int code, bool pressed)
{
	if (!ui::isOpen())
		return;
	if (pressed && st.awaiting >= 0 && code != controls::MenuButton)
	{
		controls::bind((controls::Function)st.awaiting, code);
		st.awaiting = -1;
		ui::awaitControl(false);
		refreshBindings(ui::page());
		ui::move(ui::page(), 1);
		return;
	}
	// a recomp module's Controls, set from the emulator
	if (pressed && ui::awaitingControl())
	{
		ui::controlPressed(controls::inputName(code));
		return;
	}
	ui::button(menuButton(code), pressed);
}

Result frame()
{
	const bool linkPage = st.link;
	const ui::Result result = ui::frame();
	if (result == ui::Result::Open)
		return Result::Open;
	if (linkPage && !link::kept())
		link::stop();
	st.link = false;
	st.frame.clear();
	st.preview.clear();
	switch (result)
	{
	case ui::Result::Quit:
		return Result::Quit;
	case ui::Result::Restart:
		return Result::Restart;
	case ui::Result::Switch:
		return Result::Switch;
	default:
		return Result::Resume;
	}
}

bool showStart()
{
	return ui::showStart("Loading...", fileName(st.rom));
}

bool starting()
{
	return ui::starting();
}

void endStart()
{
	ui::endStart();
}

void showStartError(const std::string& what)
{
	if (!ui::starting())
		return;
	ui::startLine("Cannot start " + displayName(), what);
	std::this_thread::sleep_for(std::chrono::seconds(3));
}

bool showStartTwoPlayers()
{
	return ui::showStart("Starting two players...", "Waiting for the other Brick");
}

bool saveSlot()
{
	std::vector<u8> rgb;
	int w = 0, h = 0;
	if (!display.video->lastFrame(rgb, w, h))
		rgb.clear();
	const bool ok = states::save(states::slotPath(st.slot), rgb, w, h);
	DS_LOG("state: slot %d %s", st.slot + 1, ok ? "saved" : "not saved");
	return ok;
}

bool slotExists()
{
	return states::exists(st.slot);
}

void openHostedControls(std::function<bool()> openPad, std::function<void()> closePad,
		std::function<std::vector<std::pair<int, bool>>()> events)
{
	// its own reading of the pad takes over: an arrow held is let go of, either way
	const auto letGo = [] {
		for (ui::Button arrow : { ui::Button::Up, ui::Button::Down, ui::Button::Left, ui::Button::Right })
			ui::button(arrow, false);
	};
	struct PadOnly
	{
		std::function<void()> close, letGo;
		~PadOnly()
		{
			close();
			letGo();
		}
	};
	letGo();
	const bool opened = openPad();
	auto padOnly = std::make_shared<PadOnly>(PadOnly { closePad, letGo });
	if (!opened)
	{
		ui::message("The pad could not be opened.");
		return;
	}
	st.awaiting = -1;
	Screen screen = bindingsScreen();
	screen.readsPad = true;
	screen.tick = [padOnly, events] {
		for (const auto& [code, pressed] : events())
			input(code, pressed);
	};
	ui::push(std::move(screen));
}

bool loadSlot()
{
	const bool ok = states::exists(st.slot) && states::load(states::slotPath(st.slot));
	DS_LOG("state: slot %d %s", st.slot + 1, ok ? "loaded" : "not loaded");
	return ok;
}

}
