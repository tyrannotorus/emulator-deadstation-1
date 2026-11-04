#include "osd.h"
#include "dead_ui.h"
#include "video.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>

namespace deadstation::osd
{

namespace
{

constexpr size_t MaxMessageLines = 4;

Video *screen = nullptr;
std::mutex mutex;
std::string message;		// under mutex
std::string details;
uint64_t messageEnd = 0;

// the main thread's
uint64_t lastFrameAt = 0;
uint64_t countedAt = 0;
uint64_t countedFrames = 0;
uint64_t repeatedFrames = 0;	// shown only for text to change: not the game's
float framesPerSecond = -1.f;
std::string shownCounter;
std::string shownMessage;

uint64_t nowMs()
{
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// frames shown each second, from what reached the screen, as a module's counter is
std::string counter(uint64_t now, bool showFps, bool fastForward)
{
	if (!showFps)
	{
		countedAt = 0;
		framesPerSecond = -1.f;
		return fastForward ? ">>" : "";
	}
	const uint64_t shown = screen->panel().framesShown();
	const uint64_t frames = shown > repeatedFrames ? shown - repeatedFrames : 0;
	// the time in the menu is not the game's
	if (countedAt == 0 || now - lastFrameAt > 500)
	{
		countedAt = now;
		countedFrames = frames;
	}
	else if (now - countedAt >= 1000)
	{
		framesPerSecond = (float)(frames > countedFrames ? frames - countedFrames : 0) * 1000.f / (float)(now - countedAt);
		countedAt = now;
		countedFrames = frames;
	}
	if (framesPerSecond < 0.f)
		return fastForward ? ">>" : "";
	char text[32];
	std::snprintf(text, sizeof(text), "%4.1f%s", framesPerSecond, fastForward ? " >>" : "");
	return text;
}

}	// namespace

void init(Video *video)
{
	screen = video;
}

void again()
{
	// its frame count starts again with the screen
	countedAt = 0;
	repeatedFrames = 0;
	shownCounter.clear();
	shownMessage.clear();
}

void notify(const std::string& text, int durationMs, const std::string& under)
{
	std::lock_guard<std::mutex> lock(mutex);
	message = text;
	details = under;
	messageEnd = nowMs() + durationMs;
}

void frame(bool rendered, bool showFps, bool fastForward)
{
	if (screen == nullptr || !screen->ready())
		return;
	const uint64_t now = nowMs();
	const std::string count = counter(now, showFps, fastForward);
	lastFrameAt = now;
	std::string text, under;
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!message.empty() && now >= messageEnd)
			message.clear();
		text = message;
		under = details;
	}
	const std::string all = text.empty() ? "" : text + "\n" + under;
	if (count == shownCounter && all == shownMessage)
		return;
	shownCounter = count;
	shownMessage = all;

	const int maxWidth = (int)screen->panel().extent().width;
	const bool panelSize = maxWidth > 640;
	std::vector<Panel::Corner> corners;
	int bottom = 0;
	const auto add = [&](const std::string& line) {
		Panel::Corner corner;
		corner.pixels = ui::label(line, corner.width, corner.height, panelSize);
		corner.bottom = bottom;
		bottom += corner.height;
		corners.push_back(std::move(corner));
	};
	if (!count.empty())
		add(count);
	if (!text.empty())
	{
		std::vector<std::string> lines = ui::labelLines(text, maxWidth, panelSize);
		const std::vector<std::string> more = ui::labelLines(under, maxWidth, panelSize);
		lines.insert(lines.end(), more.begin(), more.end());
		if (lines.size() > MaxMessageLines)
			lines.resize(MaxMessageLines);
		for (auto line = lines.rbegin(); line != lines.rend(); ++line)
			add(*line);
	}
	screen->panel().setCorners(std::move(corners));
	if (!rendered)
	{
		screen->again();
		repeatedFrames++;
	}
}

}
