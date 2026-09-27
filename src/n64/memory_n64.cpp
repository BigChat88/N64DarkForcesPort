// Memory regions backed by malloc(), one intrusive list of blocks per region.
// Same approach as tfe/amiga/memory_amiga.cpp, without the exec.library lists.

// Keep assertf() active here even in NDEBUG builds: running out of memory must
// produce a readable report instead of a null pointer crash somewhere else.
#undef NDEBUG
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <assert.h>

#include <TFE_Memory/memoryRegion.h>
#include <TFE_System/system.h>
#include <libdragon.h>
#include "memtrack_n64.h"

struct MemoryBlock
{
	MemoryBlock* prev;
	MemoryBlock* next;
	u32 size;
	u32 pad;	// keep the payload 8-byte aligned
};

struct MemoryRegion
{
	char name[32];
	MemoryBlock* head;
	size_t used;
};

namespace TFE_Memory
{
	// Live regions, only used to report memory usage when running out of memory.
	enum { MAX_REGIONS = 16 };
	static MemoryRegion* s_regions[MAX_REGIONS];

	static void trackRegion(MemoryRegion* region, bool add)
	{
		for (s32 i = 0; i < MAX_REGIONS; i++)
		{
			if (add && !s_regions[i]) { s_regions[i] = region; return; }
			if (!add && s_regions[i] == region) { s_regions[i] = nullptr; return; }
		}
	}

	static MemoryBlock* getBlock(void* ptr)
	{
		return (MemoryBlock*)ptr - 1;
	}

	static void* getPayload(MemoryBlock* block)
	{
		return block + 1;
	}

	static MemoryBlock* allocBlock(MemoryRegion* region, size_t size)
	{
		MemoryBlock* block = (MemoryBlock*)malloc(sizeof(MemoryBlock) + size);
		if (!block) { return nullptr; }

		block->size = (u32)size;
		block->prev = nullptr;
		block->next = region->head;
		if (region->head) { region->head->prev = block; }
		region->head = block;
		region->used += size;
		return block;
	}

	static void freeBlock(MemoryRegion* region, MemoryBlock* block)
	{
		if (block->prev) { block->prev->next = block->next; }
		else { region->head = block->next; }
		if (block->next) { block->next->prev = block->prev; }
		region->used -= block->size;
		free(block);
	}

	MemoryRegion* region_create(const char* name, size_t blockSize, size_t maxSize)
	{
		assert(name);
		if (!name || !blockSize) { return nullptr; }

		MemoryRegion* region = (MemoryRegion*)malloc(sizeof(MemoryRegion));
		if (!region)
		{
			TFE_System::logWrite(LOG_ERROR, "MemoryRegion", "Failed to allocate region '%s'.", name);
			return nullptr;
		}

		strncpy(region->name, name, sizeof(region->name) - 1);
		region->name[sizeof(region->name) - 1] = 0;
		region->head = nullptr;
		region->used = 0;
		trackRegion(region, true);
		return region;
	}

	void region_clear(MemoryRegion* region)
	{
		assert(region);
		while (region->head)
		{
			freeBlock(region, region->head);
		}
	}

	void region_destroy(MemoryRegion* region)
	{
		assert(region);
		region_clear(region);
		trackRegion(region, false);
		free(region);
	}

	// Stops with a report of the heap and every region (see malloc_n64.cpp too).
	void n64_reportOutOfMemory(const char* where, size_t size)
	{
		heap_stats_t stats;
		sys_get_heap_stats(&stats);
		char report[1024];
		s32 len = snprintf(report, sizeof(report), "heap: used %d / %d, free %d\n", stats.used, stats.total, stats.free);
		for (s32 i = 0; i < MAX_REGIONS && len < (s32)sizeof(report); i++)
		{
			if (!s_regions[i]) { continue; }
			len += snprintf(report + len, sizeof(report) - len, "%s: %u KB\n", s_regions[i]->name, (u32)(s_regions[i]->used / 1024));
		}
		if (len < (s32)sizeof(report))
		{
			len += MemTrack_N64::report(report + len, sizeof(report) - len, 10);
		}
		assertf(false, "Out of memory: %u bytes (%s)\n%s", (u32)size, where, report);
	}

	void* region_alloc(MemoryRegion* region, size_t size)
	{
		assert(region);
		if (size == 0) { return nullptr; }

		MemoryBlock* block = allocBlock(region, size);
		if (block)
		{
			MemTrack_N64::setCaller(block, (uintptr_t)__builtin_return_address(0));
			return getPayload(block);
		}

		// Out of memory is fatal on the N64: stop with a readable report instead
		// of letting the caller dereference a null pointer later.
		n64_reportOutOfMemory(region->name, size);
		return nullptr;
	}

	void* region_realloc(MemoryRegion* region, void* ptr, size_t size)
	{
		assert(region);
		if (!ptr) { return region_alloc(region, size); }
		if (size == 0) { return nullptr; }

		MemoryBlock* block = getBlock(ptr);
		const u32 prevSize = block->size;
		if (prevSize >= size) { return ptr; }

		void* newMem = region_alloc(region, size);
		if (!newMem) { return nullptr; }
		memcpy(newMem, ptr, std::min((u32)size, prevSize));
		freeBlock(region, block);
		return newMem;
	}

	void region_free(MemoryRegion* region, void* ptr)
	{
		if (!ptr || !region) { return; }
		freeBlock(region, getBlock(ptr));
	}

	size_t region_getMemoryUsed(MemoryRegion* region)
	{
		return region ? region->used : 0;
	}

	void region_getBlockInfo(MemoryRegion* region, size_t* blockCount, size_t* blockSize)
	{
		*blockCount = 0;
		*blockSize = 1;
	}

	size_t region_getMemoryCapacity(MemoryRegion* region)
	{
		return 0;
	}

	RelativePointer region_getRelativePointer(MemoryRegion* region, void* ptr)
	{
		return NULL_RELATIVE_POINTER;
	}

	void* region_getRealPointer(MemoryRegion* region, RelativePointer ptr)
	{
		return nullptr;
	}

	bool region_serializeToDisk(MemoryRegion* region, FileStream* file)
	{
		return false;
	}

	MemoryRegion* region_restoreFromDisk(MemoryRegion* region, FileStream* file)
	{
		return nullptr;
	}

	void region_test()
	{
	}
}
