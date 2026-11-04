// The recomp modules this Deadstation build offers (frontend/download.h): each a .tar.gz attached to the
// Deadstation release named here, installed only if its SHA-256 is the one below. Written when a release
// is prepared (shell/brick/recomp-release.sh MODULE VERSION), never by hand: the module's file is built
// first, then this, then Deadstation.
#pragma once
#include <cstdint>

namespace deadstation::recomp
{

struct CatalogEntry
{
	const char *id;
	const char *name;
	const char *version;
	const char *file;			// in the release
	uint64_t size;				// its bytes
	const char *sha256;
	const char *discs[4];		// the discs it plays (their CHDs' header SHA-1), as its manifest
};

// where the release's files are (until the repository is public: a server on the PC, through adb reverse)
constexpr const char *Release = "http://127.0.0.1:8799/";

constexpr CatalogEntry Catalog[] = {
	{ "tekken3", "Tekken 3", "0.1.0", "recomp-tekken3-0.1.0.tar.gz", 6646397, "0a27eb00e4b2647bd24adf0506cee1b82d62728c5ca4deeadca0d6cbb7e490e6",
		{ "319ec7377eb0c3ef0b6049e440937d26ae743290" } },
	{ "rallycross", "Rally Cross", "0.1.0", "recomp-rallycross-0.1.0.tar.gz", 4397109, "09fe1d4488733f2ff86571b50dedbd4c95bd448aef831f43b352bf2f9c277586",
		{ "62c54f5e1f989126a1f034c39646a0336c6c8321" } },
};

}
