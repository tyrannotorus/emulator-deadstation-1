// Two players on two linked Bricks (link.h): both run the same game from its start, frame by frame in
// lockstep. Each frame's pads are exchanged over IP on the link (UDP), a few frames ahead (the input delay),
// so that both Bricks run every frame with the same two pads: the host's player 1, the other's player 2.
// Every 300 frames the two compare a hash of the console's RAM (a difference is logged: DESYNC).
// Determinism: the session runs with the core's threaded dynarec compiler, SPU and GPU off (settings.h).
// Memory cards: each player brings one, player 1's in slot 1 and player 2's in slot 2 on both Bricks, each kept in its
// owner's file by its owner's Brick (main.cpp). Save states are off in a session.
#pragma once
#include <cstdint>
#include <string>

namespace deadstation::netplay
{

// a session begun on the link's page: the game starts again (the menu's Restart) as two players
void begin(bool host, const std::string& peerAddress, int inputDelay);
bool active();				// a session runs (or is about to, from the restart)
bool host();
bool heardPeer();			// the other Brick has answered, in this session
// before each frame of a session: this Brick's pad for the frame `delay` frames ahead out, the other's
// for this frame in. False while it cannot go on (the other paused, gone, or late): the frame waits.
bool step(int16_t localBits);
int16_t pad(unsigned port);	// the frame's pads, RETRO_DEVICE_ID_JOYPAD_* bits
// this Brick's menu opened or closed: the other Brick pauses too
void setPaused(bool paused);
enum class Pause { None, Menu, PeerMenu, Lost };
struct PauseState
{
	Pause pause = Pause::None;
	bool peerPresent = true;
};
PauseState pauseState();
// the session ends here: the game goes on alone, in place (the other Brick is told)
void leave();
// every 300 frames: this Brick's RAM hash, to compare with the other's
void frameDone(uint32_t frame, const uint8_t *ram, size_t size);

}
