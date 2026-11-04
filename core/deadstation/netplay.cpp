#include "netplay.h"
#include "log.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace deadstation::netplay
{
namespace
{

using Clock = std::chrono::steady_clock;
constexpr uint16_t Port = 19773;
constexpr int MaxAhead = 16;
constexpr auto LostAfter = std::chrono::seconds(3);
constexpr uint32_t HashEvery = 300;

// a packet: the sender's pads for frames [first, first + count), whether its menu is open or it left, and its
// last RAM hash. Every packet repeats the frames not yet behind, so a lost one costs nothing.
struct Packet
{
	char magic[4];
	uint32_t first;
	uint8_t count;
	uint8_t flags;			// Paused, Left
	int16_t bits[MaxAhead];
	uint32_t hashFrame;
	uint32_t hash;
};
enum : uint8_t { Paused = 1, Left = 2 };
constexpr char Magic[4] = { 'D', 'S', 'N', 'P' };

int sock = -1;
sockaddr_in peer{};
bool running = false;
bool isHost = false;
int delay = 2;
uint32_t frame = 0;					// the frame about to run
std::map<uint32_t, int16_t> local, remote;
int16_t pads[2] = { 0, 0 };
bool localPaused = false, peerPaused = false, peerLeft = false;
Clock::time_point lastHeard, lastSent;
std::map<uint32_t, uint32_t> myHash, peerHash;

void compare(uint32_t f)
{
	auto a = myHash.find(f), b = peerHash.find(f);
	if (a == myHash.end() || b == peerHash.end())
		return;
	if (a->second != b->second)
		DS_LOG("netplay: DESYNC at frame %u (%08x here, %08x there)", f, a->second, b->second);
	myHash.erase(a);
	peerHash.erase(b);
}

void send(uint8_t extraFlags = 0)
{
	if (sock < 0)
		return;
	Packet p{};
	memcpy(p.magic, Magic, 4);
	p.first = frame;
	for (uint32_t f = frame; f <= frame + (uint32_t)delay && p.count < MaxAhead; f++)
	{
		auto it = local.find(f);
		if (it == local.end())
			break;
		p.bits[p.count++] = it->second;
	}
	p.flags = (localPaused ? Paused : 0) | extraFlags;
	if (!myHash.empty())
	{
		p.hashFrame = myHash.rbegin()->first;
		p.hash = myHash.rbegin()->second;
	}
	sendto(sock, &p, sizeof(p), 0, (const sockaddr *)&peer, sizeof(peer));
	lastSent = Clock::now();
}

void receive()
{
	Packet p;
	while (sock >= 0 && recv(sock, &p, sizeof(p), MSG_DONTWAIT) == (ssize_t)sizeof(p))
	{
		if (memcmp(p.magic, Magic, 4) != 0)
			continue;
		lastHeard = Clock::now();
		for (int i = 0; i < p.count && i < MaxAhead; i++)
			remote.emplace(p.first + i, p.bits[i]);
		peerPaused = (p.flags & Paused) != 0;
		if (p.flags & Left)
			peerLeft = true;
		if (p.hashFrame != 0)
		{
			peerHash[p.hashFrame] = p.hash;
			compare(p.hashFrame);
		}
	}
}

void close()
{
	if (sock >= 0)
		::close(sock);
	sock = -1;
	running = false;
}

}	// namespace

void begin(bool hostSide, const std::string& peerAddress, int inputDelay)
{
	close();
	isHost = hostSide;
	delay = std::max(2, std::min(inputDelay, MaxAhead - 2));
	sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	sockaddr_in me{};
	me.sin_family = AF_INET;
	me.sin_port = htons(Port);
	me.sin_addr.s_addr = htonl(INADDR_ANY);
	peer = {};
	peer.sin_family = AF_INET;
	peer.sin_port = htons(Port);
	if (sock < 0 || bind(sock, (const sockaddr *)&me, sizeof(me)) != 0
			|| inet_pton(AF_INET, peerAddress.c_str(), &peer.sin_addr) != 1)
	{
		DS_LOG("netplay: no session (socket for %s: %s)", peerAddress.c_str(), strerror(errno));
		close();
		return;
	}
	frame = 0;
	local.clear();
	remote.clear();
	myHash.clear();
	peerHash.clear();
	// the first frames: nothing pressed on either side
	for (int f = 0; f < delay; f++)
	{
		local[f] = 0;
		remote[f] = 0;
	}
	localPaused = peerPaused = peerLeft = false;
	lastHeard = lastSent = Clock::now();
	running = true;
	DS_LOG("netplay: session with %s, %s, input delay %d", peerAddress.c_str(), isHost ? "player 1" : "player 2", delay);
}

bool active()
{
	return running;
}

bool host()
{
	return isHost;
}

bool step(int16_t localBits)
{
	if (!running)
	{
		pads[0] = localBits;
		pads[1] = 0;
		return true;
	}
	// this Brick's pad for the frame `delay` ahead: kept once sent, never changed
	local.emplace(frame + (uint32_t)delay, localBits);
	const auto start = Clock::now();
	while (true)
	{
		receive();
		if (peerLeft)
		{
			DS_LOG("netplay: the other player left at frame %u: on alone", frame);
			close();
			pads[0] = localBits;
			pads[1] = 0;
			return true;
		}
		const bool ready = remote.count(frame) != 0 && !peerPaused && !localPaused;
		if (ready)
			break;
		const auto now = Clock::now();
		if (now - lastSent > std::chrono::milliseconds(4))
			send();
		// the main loop gets control back now and then (the menu, a pause page)
		if (now - start > std::chrono::milliseconds(50))
			return false;
		pollfd pfd { sock, POLLIN, 0 };
		poll(&pfd, 1, 2);
	}
	send();
	const int16_t mine = local[frame], theirs = remote[frame];
	pads[0] = isHost ? mine : theirs;
	pads[1] = isHost ? theirs : mine;
	local.erase(local.begin(), local.lower_bound(frame > 32 ? frame - 32 : 0));
	remote.erase(remote.begin(), remote.lower_bound(frame > 32 ? frame - 32 : 0));
	frame++;
	return true;
}

int16_t pad(unsigned port)
{
	return port < 2 ? pads[port] : 0;
}

void setPaused(bool paused)
{
	localPaused = paused;
	send();
}

PauseState pauseState()
{
	PauseState s;
	if (!running)
		return s;
	receive();
	const bool lost = Clock::now() - lastHeard > LostAfter;
	s.peerPresent = !lost;
	s.pause = lost ? Pause::Lost : peerPaused ? Pause::PeerMenu : Pause::None;
	return s;
}

void leave()
{
	if (!running)
		return;
	for (int i = 0; i < 3; i++)
		send(Left);
	DS_LOG("netplay: left the session at frame %u: on alone", frame);
	close();
}

void frameDone(uint32_t f, const uint8_t *ram, size_t size)
{
	if (!running || ram == nullptr || f == 0 || f % HashEvery != 0)
		return;
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < size; i++)
		h = (h ^ ram[i]) * 16777619u;
	myHash[f] = h;
	compare(f);
	while (myHash.size() > 8)
		myHash.erase(myHash.begin());
}

}
