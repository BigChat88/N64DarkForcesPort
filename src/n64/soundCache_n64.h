#pragma once
// Digital sound (VOC) cache for the N64.
//
// Dark Forces preloads its sound effects into RAM, which does not fit next to a
// level in 8MB. On the N64, loading a VOC only creates a small handle that
// remembers the file; the sound data is read from the ROM the first time iMuse
// needs it and kept in a fixed-budget LRU cache.
#include <TFE_System/types.h>

namespace SoundCache_N64
{
	// Creates a handle for 'fileName' (already resolved to a name the TFE
	// search paths can find). The handle is allocated with game_alloc() so the
	// callers can keep freeing it with game_free().
	u8* createHandle(const char* fileName, u32 fileSize);

	// Returns the VOC data for a handle (loading it if needed), or 'data'
	// unchanged if it is not a handle.
	u8* resolve(u8* data);

	// Frees the least recently used cached sound. Returns false if the cache is
	// empty. Called when an allocation fails anywhere (see malloc wrappers).
	bool purgeOne();
}
