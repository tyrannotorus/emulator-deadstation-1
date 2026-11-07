// A PlayStation disc's id, as its SYSTEM.CNF names its program (BOOT = cdrom:\SLUS_005.94;1: SLUS00594, as the core's
// CdromId), read from the image before the core starts: its settings are the game's own by it (options.h, game()).
#pragma once
#include <string>

namespace deadstation::disc
{

// a CHD, a cue sheet (its first file), a .bin or .img (2352-byte sectors), an .iso (2048), or a PBP (its PARAM.SFO's
// DISC_ID): "" without one, why logged
std::string id(const std::string& path);

}
