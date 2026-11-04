// The PlayStation GPU drawn by Vulkan for a recomp module on the Brick (psx_gpu.cpp): PSXRecomp's renderer backend
// (its gr_* table and vk_renderer_* entry points) on Deadstation's presenter's device. VRAM is an image at the
// module's scale; frames go to the screen straight from it.
#pragma once
#include "video.h"

// host.cpp: the presenter (initialised on first use; not ready if the screen failed), and each frame shown
// (perf, MENU, screenshots)
Video& hostVideo();
void hostFrameShown(int width, int height, bool newFrame);
// host.cpp: the player's input (MENU: Deadstation's menu), on the module's thread after each frame
void hostInput();
// host.cpp, for the menu (module_menu.cpp), on the module's thread with the render thread idle:
// the module's end: its Vulkan objects gone, whatever the driver does (the caller then _exits with code)
void hostStop(int code);
// Show FPS: on or off from now on
void hostShowFps(bool on);
// the counter off for the menu's backdrop until hostMenuClosed; true if it was up, so the next frame is the first without it
bool hostHideFps();
// the guest going on after the menu: the time held is not the game's, in any count
void hostMenuClosed();
// a screenshot asked for (SIGUSR2), taken by this call
bool hostTakeShotWanted();

// module_menu.cpp: MENU (Deadstation's menu over the game, the guest held until it closes), after each frame
void moduleMenuPoll();

// the GPU's Vulkan objects destroyed (before the presenter's device): at the module's end, or the driver keeps them
void psx_gpu_release();
// the render thread (psx_gpu.cpp) stopped after the work handed to it: before any Vulkan call from another thread
void psx_gpu_stop();
// the render thread idle, everything handed to it done, until the module's thread gives it more
void psx_gpu_idle();
