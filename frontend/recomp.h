// Recomp modules: recompiled builds of single games, run instead of Deadstation's emulation where one is
// installed for the disc being played. Each lives in its own folder, <home>/recomp/<id>/, with its
// executable and a manifest, module.cfg:
//
//   id = rez                        the folder's name
//   name = Rez
//   version = 1.0.1
//   exec = rez                      the executable, beside the manifest
//   disc = 9711a0d0...              each disc it plays, by the SHA-1 its CHD's header records
//   setting = scale | Resolution | 1=Original, 2=1024 x 768 | 2
//                                   each setting, shown in Recomp Options: key | name | value=label, ... | default
//                                   and, if it needs saying, | what it does (the row's description)
//
// A module has no settings of its own in the game: it declares them here, Deadstation's Recomp Options
// shows them, and the store below keeps them (the module reads them from it).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace deadstation::recomp
{

// Deadstation's own folder (DEADSTATION_HOME): its settings, the modules
std::string home();

// the SHA-1 a CHD's header records (version 5), in lowercase hex; empty for anything else
std::string chdSha1(const std::string& path);

struct Setting
{
	std::string key;
	std::string name;
	std::vector<std::pair<std::string, std::string>> values;	// value, label
	std::string def;
	std::string desc;
};
struct Module
{
	std::string id;
	std::string name;
	std::string version;
	std::string exec;
	std::string dir;
	std::vector<std::string> discs;
	std::vector<Setting> settings;
};
bool readManifest(const std::string& file, Module& module);
// the module installed for a disc (its CHD's SHA-1); false if none. Only a folder named as its manifest's
// id counts: never what an install or a deletion left half done (<id>.new, <id>.old)
bool installed(const std::string& discSha1, Module& module);

// a file, or a folder and everything in it; true if gone
bool removeTree(const std::string& path);

// What of a module's folder is the player's, not the module's: its memory cards (saves/) and its controls
// (input.ini). PSXRecomp keeps them there, beside the module, so an update moves them to the new folder.
// True unless one could not be moved.
bool keepPlayerFiles(const std::string& from, const std::string& to);

// Deleting a module takes everything of it: its folder, its data (<home>/recomp-data/<id>/: saves,
// settings, cache) and what the store keeps for it. It is marked first (<home>/recomp/<id>.delete) and
// the mark goes last, so a deletion cut short is finished by tidy(). A module deleting itself while it
// runs only marks itself: tidy() deletes it at Deadstation's next start, once it has ended.
void markForDeletion(const std::string& id);
bool deleteModule(const std::string& id);	// true if all of it is gone
// At Deadstation's start, before anything: an install cut short finished or undone (<id>.old: the player's
// files moved on from it, or the module itself put back if the new one never took its place), what else it
// left behind removed (<id>.download, <id>.new), then the deletions marked done
void tidy();

// The core chosen for a disc in Options: Recomp, its module installed, runs the module in this
// process instead (exec: nothing of the caller stays), given the disc's path. Returns only if not.
void runChosen(const std::string& disc);
// A module's run ended as it should (quit, or the other core chosen): its next start runs it again.
// One that did not end so (it died, or would not start) leaves its game emulated once.
void runEnded();
constexpr const char *RunMarker = "/tmp/deadstation-recomp-run";
// The exit code that asks the launcher to start the game again, in the core now chosen
constexpr int SwitchExit = 86;

// Deadstation's store of what the menu picks for the recomps (key = value lines in <home>/recomp.cfg), each
// change saved at once: core.<module> (the game's core), <module>.<setting> (a module's settings)
std::string get(const std::string& key, const std::string& def = "");
void set(const std::string& key, const std::string& value);

}
