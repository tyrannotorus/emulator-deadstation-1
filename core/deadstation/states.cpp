#include "states.h"
#include "log.h"
#include "recomp.h"

#include <libretro.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <unistd.h>

namespace deadstation::states
{
namespace
{

// the file: "DSST", version, the picture's width and height, its RGB24 rows, the state's size and bytes
constexpr char Magic[4] = { 'D', 'S', 'S', 'T' };
constexpr uint32_t Version = 1;
std::string game = "game";

struct Header
{
	char magic[4];
	uint32_t version;
	uint32_t width;
	uint32_t height;
};

bool readHeader(std::ifstream& in, Header& h)
{
	return in.read((char *)&h, sizeof(h)) && memcmp(h.magic, Magic, 4) == 0 && h.version == Version
			&& h.width <= 1024 && h.height <= 1024;
}

}	// namespace

void setGame(const std::string& romPath)
{
	std::string name = romPath.substr(romPath.find_last_of('/') + 1);
	const size_t dot = name.find_last_of('.');
	if (dot != std::string::npos && dot > 0)
		name.resize(dot);
	game = name;
}

std::string slotPath(int slot)
{
	return recomp::home() + "/" + game + (slot == 0 ? "" : "_" + std::to_string(slot)) + ".state";
}

bool save(const std::string& path, const std::vector<uint8_t>& rgb, int width, int height)
{
	const size_t size = retro_serialize_size();
	std::vector<uint8_t> data(size);
	if (size == 0 || !retro_serialize(data.data(), size))
	{
		DS_LOG("state: the core did not serialize");
		return false;
	}
	if ((size_t)width * height * 3 != rgb.size())
		width = height = 0;
	const std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		Header h;
		memcpy(h.magic, Magic, 4);
		h.version = Version;
		h.width = (uint32_t)width;
		h.height = (uint32_t)height;
		const uint64_t bytes = size;
		out.write((const char *)&h, sizeof(h));
		if (width > 0)
			out.write((const char *)rgb.data(), (std::streamsize)rgb.size());
		out.write((const char *)&bytes, sizeof(bytes));
		out.write((const char *)data.data(), (std::streamsize)size);
		if (!out)
		{
			DS_LOG("state: %s not written", tmp.c_str());
			unlink(tmp.c_str());
			return false;
		}
	}
	if (std::rename(tmp.c_str(), path.c_str()) != 0)
		return false;
	sync();
	return true;
}

bool load(const std::string& path)
{
	std::ifstream in(path, std::ios::binary);
	Header h;
	if (!readHeader(in, h))
		return false;
	in.seekg((std::streamoff)h.width * h.height * 3, std::ios::cur);
	uint64_t bytes = 0;
	if (!in.read((char *)&bytes, sizeof(bytes)) || bytes == 0 || bytes > (64u << 20))
		return false;
	std::vector<uint8_t> data(bytes);
	if (!in.read((char *)data.data(), (std::streamsize)bytes))
		return false;
	if (!retro_unserialize(data.data(), data.size()))
	{
		DS_LOG("state: the core did not take %s", path.c_str());
		return false;
	}
	return true;
}

bool exists(int slot)
{
	return access(slotPath(slot).c_str(), R_OK) == 0;
}

bool picture(int slot, std::vector<uint8_t>& rgb, int& width, int& height)
{
	std::ifstream in(slotPath(slot), std::ios::binary);
	Header h;
	if (!readHeader(in, h) || h.width == 0 || h.height == 0)
		return false;
	rgb.resize((size_t)h.width * h.height * 3);
	if (!in.read((char *)rgb.data(), (std::streamsize)rgb.size()))
		return false;
	width = (int)h.width;
	height = (int)h.height;
	return true;
}

}
