#include "soundCache_n64.h"
#include "debug_n64.h"
#include <TFE_FileSystem/paths.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_Game/igame.h>
#include <TFE_System/system.h>
#include <cstdlib>
#include <cstring>

#include <libdragon.h>

namespace SoundCache_N64
{
	enum
	{
		CACHE_BUDGET = 512 * 1024,	// RAM reserved for decoded sound data.
		MAX_ENTRIES  = 96,
		NAME_LEN     = 16,
	};

	static const u32 c_handleMagic0 = 0x4e363453;	// 'N64S'
	static const u32 c_handleMagic1 = 0x4e44484e;	// 'NDHN'

	struct SoundHandle
	{
		u32  magic0;
		u32  magic1;
		u32  size;
		s32  entry;		// last known cache entry, validated against 'generation'
		u32  generation;
		char name[NAME_LEN];
	};

	struct CacheEntry
	{
		char name[NAME_LEN];
		u8*  data;
		u32  size;
		u64  lastUse;
		u32  generation;
	};

	static CacheEntry s_entries[MAX_ENTRIES];
	static u32 s_cacheSize = 0;
	static u32 s_generation = 1;

	// A few samples of silence in VOC format, returned when a sound cannot be loaded.
	static u8 s_silentVoc[26 + 7 + 1 + 64];

	static void buildSilentVoc()
	{
		static const u8 c_vocHeader[26] =
		{
			'C','r','e','a','t','i','v','e',' ','V','o','i','c','e',' ','F','i','l','e', 0x1A,
			0x1A, 0x00,		// header size
			0x0A, 0x01,		// version 1.10
			0x29, 0x11,		// version check value
		};
		memset(s_silentVoc, 0, sizeof(s_silentVoc));
		memcpy(s_silentVoc, c_vocHeader, sizeof(c_vocHeader));
		u8* block = s_silentVoc + sizeof(c_vocHeader);
		block[0] = 1;		// sound data block
		block[1] = 3;		// block size: rate + codec + 1 sample
		block[4] = 166;		// ~11kHz
		block[5] = 0;		// 8-bit unsigned PCM
		block[6] = 0x80;	// silence
		block[7] = 0;		// terminator
	}

	static bool isHandle(const u8* data)
	{
		if (!data || ((uintptr_t)data & 3)) { return false; }
		const SoundHandle* handle = (const SoundHandle*)data;
		return handle->magic0 == c_handleMagic0 && handle->magic1 == c_handleMagic1;
	}

	static void freeEntry(CacheEntry* entry)
	{
		free(entry->data);
		s_cacheSize -= entry->size;
		entry->data = nullptr;
		entry->size = 0;
		entry->name[0] = 0;
	}

	// Frees the least recently used entries until 'size' more bytes fit in the budget.
	// Sounds that are playing are touched on every mixed block, so they are evicted last.
	static void makeRoom(u32 size)
	{
		while (s_cacheSize + size > CACHE_BUDGET)
		{
			CacheEntry* oldest = nullptr;
			for (s32 i = 0; i < MAX_ENTRIES; i++)
			{
				if (s_entries[i].data && (!oldest || s_entries[i].lastUse < oldest->lastUse))
				{
					oldest = &s_entries[i];
				}
			}
			if (!oldest) { return; }
			freeEntry(oldest);
		}
	}

	bool purgeOne()
	{
		CacheEntry* oldest = nullptr;
		for (s32 i = 0; i < MAX_ENTRIES; i++)
		{
			if (s_entries[i].data && (!oldest || s_entries[i].lastUse < oldest->lastUse))
			{
				oldest = &s_entries[i];
			}
		}
		if (!oldest) { return false; }
		freeEntry(oldest);
		Debug_N64::s_audio.cacheBytes = s_cacheSize;
		return true;
	}

	static CacheEntry* findEntry(const char* name)
	{
		for (s32 i = 0; i < MAX_ENTRIES; i++)
		{
			if (s_entries[i].data && strcmp(s_entries[i].name, name) == 0)
			{
				return &s_entries[i];
			}
		}
		return nullptr;
	}

	static CacheEntry* getFreeEntry()
	{
		for (s32 i = 0; i < MAX_ENTRIES; i++)
		{
			if (!s_entries[i].data) { return &s_entries[i]; }
		}
		// All slots used: recycle the least recently used one.
		CacheEntry* oldest = &s_entries[0];
		for (s32 i = 1; i < MAX_ENTRIES; i++)
		{
			if (s_entries[i].lastUse < oldest->lastUse) { oldest = &s_entries[i]; }
		}
		freeEntry(oldest);
		return oldest;
	}

	static CacheEntry* loadEntry(const SoundHandle* handle)
	{
		FilePath path;
		FileStream file;
		if (!TFE_Paths::getFilePath(handle->name, &path) || !file.open(&path, Stream::MODE_READ))
		{
			TFE_System::logWrite(LOG_ERROR, "SoundCache", "Cannot open sound '%s'.", handle->name);
			Debug_N64::s_audio.soundFailures++;
			return nullptr;
		}

		const u32 size = (u32)file.getSize();
		makeRoom(size);
		u8* data = (u8*)malloc(size);
		if (!data)
		{
			file.close();
			TFE_System::logWrite(LOG_ERROR, "SoundCache", "Out of memory loading sound '%s' (%u bytes).", handle->name, size);
			Debug_N64::s_audio.soundFailures++;
			return nullptr;
		}
		file.readBuffer(data, size);
		file.close();

		CacheEntry* entry = getFreeEntry();
		strcpy(entry->name, handle->name);
		entry->data = data;
		entry->size = size;
		entry->generation = s_generation++;
		s_cacheSize += size;
		Debug_N64::s_audio.soundLoads++;
		Debug_N64::s_audio.cacheBytes = s_cacheSize;
		return entry;
	}

	u8* createHandle(const char* fileName, u32 fileSize)
	{
		if (!s_silentVoc[0]) { buildSilentVoc(); }
		if (strlen(fileName) >= NAME_LEN)
		{
			TFE_System::logWrite(LOG_ERROR, "SoundCache", "Sound name too long: '%s'.", fileName);
			return nullptr;
		}

		SoundHandle* handle = (SoundHandle*)game_alloc(sizeof(SoundHandle));
		if (!handle) { return nullptr; }
		handle->magic0 = c_handleMagic0;
		handle->magic1 = c_handleMagic1;
		handle->size = fileSize;
		handle->entry = -1;
		handle->generation = 0;
		strcpy(handle->name, fileName);
		return (u8*)handle;
	}

	u8* resolve(u8* data)
	{
		if (!isHandle(data)) { return data; }
		SoundHandle* handle = (SoundHandle*)data;
		Debug_N64::s_audio.soundResolves++;

		// Fast path: the entry this handle used last time is still valid.
		CacheEntry* entry = nullptr;
		if (handle->entry >= 0 && s_entries[handle->entry].data && s_entries[handle->entry].generation == handle->generation)
		{
			entry = &s_entries[handle->entry];
		}
		else
		{
			entry = findEntry(handle->name);
			if (!entry) { entry = loadEntry(handle); }
			if (!entry) { return s_silentVoc; }
			handle->entry = s32(entry - s_entries);
			handle->generation = entry->generation;
		}

		entry->lastUse = get_ticks();
		return entry->data;
	}
}
