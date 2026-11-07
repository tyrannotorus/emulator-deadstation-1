#include "disc.h"
#include "log.h"

#include <libchdr/chd.h>
#include <libchdr/cdrom.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <strings.h>
#include <vector>

namespace deadstation::disc
{
namespace
{

// a 2048-byte data sector by its number (LBA): false past the image or unreadable
using Reader = std::function<bool(uint32_t lba, uint8_t *out)>;

uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

bool endsWith(const std::string& s, const char *tail)
{
	const size_t n = std::strlen(tail);
	return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, tail) == 0;
}

// a raw 2352-byte sector's data: after the header, and a mode 2 sector's subheader
const uint8_t *rawData(const uint8_t *sector)
{
	return sector + (sector[15] == 2 ? 24 : 16);
}

// SYSTEM.CNF's BOOT line, its program's name: its letters and digits up to ';', 9 at most (as the core's CdromId)
std::string serial(const std::string& cnf)
{
	const size_t boot = cnf.find("BOOT");
	if (boot == std::string::npos)
		return "";
	const size_t cdrom = cnf.find("cdrom:", boot);
	if (cdrom == std::string::npos)
		return "";
	size_t end = cnf.find_first_of(";\r\n", cdrom);
	if (end == std::string::npos)
		end = cnf.size();
	const std::string program = cnf.substr(cdrom + 6, end - cdrom - 6);
	const size_t slash = program.find_last_of("\\/");
	std::string id;
	for (char c : slash == std::string::npos ? program : program.substr(slash + 1))
		if (std::isalnum((unsigned char)c) && id.size() < 9)
			id += (char)std::toupper((unsigned char)c);
	return id;
}

// the ISO 9660 file system's SYSTEM.CNF, in the root directory
std::string fromFileSystem(const Reader& read, std::string& why)
{
	uint8_t sector[2048];
	if (!read(16, sector) || sector[0] != 1 || std::memcmp(sector + 1, "CD001", 5) != 0)
	{
		why = "no ISO 9660 volume";
		return "";
	}
	const uint32_t rootAt = le32(sector + 156 + 2);
	const uint32_t rootSize = le32(sector + 156 + 10);
	for (uint32_t done = 0; done < rootSize && done < 64 * 2048; done += 2048)
	{
		if (!read(rootAt + done / 2048, sector))
			break;
		for (uint32_t at = 0; at < 2048 && sector[at] != 0; at += sector[at])
		{
			const uint8_t *record = sector + at;
			const std::string name((const char *)record + 33, record[32]);
			if (strncasecmp(name.c_str(), "SYSTEM.CNF", 10) != 0 || (name.size() > 10 && name[10] != ';'))
				continue;
			const uint32_t fileAt = le32(record + 2);
			const uint32_t size = std::min<uint32_t>(le32(record + 10), 4 * 2048);
			std::string cnf;
			for (uint32_t got = 0; got < size; got += 2048)
			{
				if (!read(fileAt + got / 2048, sector))
				{
					why = "SYSTEM.CNF unreadable";
					return "";
				}
				cnf.append((const char *)sector, std::min<uint32_t>(2048, size - got));
			}
			const std::string id = serial(cnf);
			if (id.empty())
				why = "no BOOT in SYSTEM.CNF";
			return id;
		}
	}
	why = "no SYSTEM.CNF";
	return "";
}

std::string fromChd(const std::string& path, std::string& why)
{
	FILE *file = std::fopen(path.c_str(), "rb");
	chd_file *chd = nullptr;
	if (file == nullptr || chd_open_file(file, CHD_OPEN_READ, nullptr, &chd) != CHDERR_NONE)
	{
		if (file != nullptr)
			std::fclose(file);
		why = "not a CHD it can open";
		return "";
	}
	const chd_header *header = chd_get_header(chd);
	const uint32_t perHunk = header->hunkbytes / CD_FRAME_SIZE;
	// the first track's frames start after its pregap, when the file holds it (as the core reads it)
	uint32_t base = 0;
	char meta[256];
	uint32_t size = 0;
	int track = 0, frames = 0, pregap = 0, postgap = 0;
	char type[64] = "", sub[32] = "", pgtype[32] = "", pgsub[32] = "";
	if (chd_get_metadata(chd, CDROM_TRACK_METADATA2_TAG, 0, meta, sizeof(meta) - 1, &size, nullptr, nullptr) == CHDERR_NONE)
	{
		meta[std::min<uint32_t>(size, sizeof(meta) - 1)] = '\0';
		if (std::sscanf(meta, CDROM_TRACK_METADATA2_FORMAT, &track, type, sub, &frames, &pregap, pgtype, pgsub, &postgap) >= 5)
			base = (uint32_t)std::max(pregap, 0);
	}
	std::vector<uint8_t> hunk(header->hunkbytes);
	uint32_t loaded = UINT32_MAX;
	const std::string id = perHunk == 0 ? "" : fromFileSystem([&](uint32_t lba, uint8_t *out) {
		const uint32_t frame = base + lba;
		const uint32_t want = frame / perHunk;
		if (want >= header->totalhunks)
			return false;
		if (want != loaded)
		{
			if (chd_read(chd, want, hunk.data()) != CHDERR_NONE)
				return false;
			loaded = want;
		}
		std::memcpy(out, rawData(hunk.data() + (frame % perHunk) * CD_FRAME_SIZE), 2048);
		return true;
	}, why);
	chd_close(chd);
	std::fclose(file);
	return id;
}

// a file of sectors: 2352 bytes raw (from its sync bytes), else 2048
std::string fromImage(const std::string& path, std::string& why)
{
	std::ifstream in(path, std::ios::binary);
	uint8_t first[16] = {};
	if (!in.read((char *)first, sizeof(first)))
	{
		why = "image unreadable";
		return "";
	}
	static const uint8_t sync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
	const bool raw = std::memcmp(first, sync, 12) == 0;
	return fromFileSystem([&](uint32_t lba, uint8_t *out) {
		uint8_t sector[2352];
		const size_t bytes = raw ? 2352 : 2048;
		in.clear();
		in.seekg((std::streamoff)lba * bytes);
		if (!in.read((char *)sector, bytes))
			return false;
		std::memcpy(out, raw ? rawData(sector) : sector, 2048);
		return true;
	}, why);
}

// a cue sheet's first file, beside it
std::string fromCue(const std::string& path, std::string& why)
{
	std::ifstream in(path);
	for (std::string line; std::getline(in, line); )
	{
		const size_t file = line.find("FILE");
		if (file == std::string::npos)
			continue;
		const size_t open = line.find('"', file), close = open == std::string::npos ? open : line.find('"', open + 1);
		if (close == std::string::npos)
			break;
		std::string bin = line.substr(open + 1, close - open - 1);
		if (bin.empty() || bin[0] != '/')
		{
			const size_t slash = path.find_last_of('/');
			bin = slash == std::string::npos ? bin : path.substr(0, slash + 1) + bin;
		}
		return fromImage(bin, why);
	}
	why = "no FILE in the cue sheet";
	return "";
}

// a PBP's PARAM.SFO: its DISC_ID
std::string fromPbp(const std::string& path, std::string& why)
{
	std::ifstream in(path, std::ios::binary);
	uint8_t head[12];
	if (!in.read((char *)head, sizeof(head)) || std::memcmp(head, "\0PBP", 4) != 0)
	{
		why = "not a PBP";
		return "";
	}
	const uint32_t sfoAt = le32(head + 8);
	std::vector<uint8_t> sfo(4096);
	in.seekg(sfoAt);
	in.read((char *)sfo.data(), sfo.size());
	sfo.resize(in.gcount());
	if (sfo.size() < 20 || std::memcmp(sfo.data(), "\0PSF", 4) != 0)
	{
		why = "no PARAM.SFO";
		return "";
	}
	const uint32_t keys = le32(&sfo[8]), data = le32(&sfo[12]), count = le32(&sfo[16]);
	for (uint32_t i = 0; i < count && 20 + i * 16 + 16 <= sfo.size(); i++)
	{
		const uint8_t *entry = &sfo[20 + i * 16];
		const uint32_t key = keys + (entry[0] | entry[1] << 8), value = data + le32(entry + 12), length = le32(entry + 4);
		if (key + 8 > sfo.size() || value + length > sfo.size() || std::memcmp(&sfo[key], "DISC_ID", 8) != 0)
			continue;
		std::string id;
		for (uint32_t c = 0; c < length && sfo[value + c] != 0; c++)
			if (std::isalnum(sfo[value + c]) && id.size() < 9)
				id += (char)std::toupper(sfo[value + c]);
		if (id.empty())
			why = "an empty DISC_ID";
		return id;
	}
	why = "no DISC_ID in PARAM.SFO";
	return "";
}

}	// namespace

std::string id(const std::string& path)
{
	std::string why;
	std::string id;
	if (endsWith(path, ".chd"))
		id = fromChd(path, why);
	else if (endsWith(path, ".cue"))
		id = fromCue(path, why);
	else if (endsWith(path, ".pbp"))
		id = fromPbp(path, why);
	else if (endsWith(path, ".bin") || endsWith(path, ".img") || endsWith(path, ".iso"))
		id = fromImage(path, why);
	else
		why = "a kind of image it cannot read";
	if (id.empty())
		DS_LOG("disc: no id (%s): its settings are the defaults", why.c_str());
	return id;
}

}
