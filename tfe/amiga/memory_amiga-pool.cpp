#include <cstring>

#include <TFE_Memory/memoryRegion.h>
#include <TFE_System/system.h>
#include <TFE_System/memoryPool.h>
#include <TFE_System/math.h>
#include <TFE_Jedi/Math/core_math.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <algorithm>

#include <proto/exec.h>

using namespace TFE_Jedi;

struct MemoryRegion
{
	char name[32];
	void *poolHeader;
};

namespace TFE_Memory
{
	// TODO tune these values!
	static const u32 c_puddleSize = 32u * 1024u;
	static const u32 c_treshSize = 10u * 1024u;

	ULONG GetMemSize(APTR memory)
	{
		if (memory == NULL)
			return 0;

		ULONG *mem = (ULONG *)memory;
		ULONG memSize = *--mem;
		return memSize;
	}

	APTR AllocVecPooled(APTR poolHeader, ULONG memSize)
	{
		ULONG *memory;

		if (poolHeader == NULL)
			return NULL;

		memSize += sizeof(ULONG);
		memory = (ULONG *)AllocPooled(poolHeader, memSize);
		if (memory != NULL)
			*memory++ = memSize;
		//STAT_ALLOC(memSize);

		return memory;
	}

	void FreeVecPooled(APTR poolHeader, APTR memory)
	{
		if (memory == NULL || poolHeader == NULL)
			return;

		ULONG *mem = (ULONG *)memory;
		ULONG memSize = *--mem;
		//STAT_FREE(memSize);
		FreePooled(poolHeader, mem, memSize);
	}

	bool createPool(MemoryRegion* region)
	{
		if (!region->poolHeader)
			region->poolHeader = CreatePool(MEMF_FAST, c_puddleSize, c_treshSize);

		//STAT_RESET();

		return region->poolHeader != nullptr;
	}

	void destroyPool(MemoryRegion* region)
	{
		if (!region->poolHeader)
			return;

		DeletePool(region->poolHeader);
		region->poolHeader = NULL;

		//STAT_PRINT();
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

		strcpy(region->name, name);
		if (!createPool(region))
		{
			free(region);
			TFE_System::logWrite(LOG_ERROR, "MemoryRegion", "Failed to memory block of size %u in region '%s'.", blockSize, name);
			return nullptr;
		}

		return region;
	}

	void region_clear(MemoryRegion* region)
	{
		assert(region);
		destroyPool(region);
		createPool(region);
	}

	void region_destroy(MemoryRegion* region)
	{
		assert(region);
		destroyPool(region);
		free(region);
	}

	void* region_alloc(MemoryRegion* region, size_t size)
	{
		assert(region);
		if (size == 0) { return nullptr; }

		TFE_System::logWrite(LOG_MSG, "MemoryRegion", "%s(%p,%d)\n", __FUNCTION__, region, size);
		void* mem = AllocVecPooled(region->poolHeader, size);
		TFE_System::logWrite(LOG_MSG, "MemoryRegion", "%s mem %p\n", __FUNCTION__, mem);
		if (mem)
			return mem;

		// We are all out of memory...
		TFE_System::logWrite(LOG_ERROR, "MemoryRegion", "Failed to allocate %u bytes in region '%s'.", size, region->name);
		return nullptr;
	}

	void* region_realloc(MemoryRegion* region, void* ptr, size_t size)
	{
		assert(region);
		if (!ptr) { return region_alloc(region, size); }
		if (size == 0) { return nullptr; }

		u32 prevSize = GetMemSize(ptr);
		// If the current block is already large enough, skip looping over the memory blocks.
		if (prevSize >= size)
		{
			return ptr;
		}

		// Allocate a new block of memory.
		void* newMem = AllocVecPooled(region->poolHeader, size);
		if (!newMem) { return nullptr; }
		// Copy over the contents from the previous block.
		memcpy(newMem, ptr, std::min((u32)size, prevSize));
		// Free the previous block
		region_free(region, ptr);
		// Then return the new block.
		return newMem;
	}

	void region_free(MemoryRegion* region, void* ptr)
	{
		if (!ptr || !region) { return; }

		FreeVecPooled(region->poolHeader, ptr);
	}

	size_t region_getMemoryUsed(MemoryRegion* region)
	{
		return 0;
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

/*
	size_t alloc_align(size_t baseSize)
	{
		return (baseSize + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
	}

	s32 getBinFromSize(u32 size)
	{
		if (size < 32) { return 0; }
		else if (size <= 64) { return 1; }
		else if (size <= 128) { return 2; }
		else if (size <= 256) { return 3; }
		else if (size <= 512) { return 4; }
		return 5;
	}
*/

	void region_test()
	{
	}
}