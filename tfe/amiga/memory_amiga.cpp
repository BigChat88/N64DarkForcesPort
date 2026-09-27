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
#include <clib/alib_protos.h>

using namespace TFE_Jedi;

struct MemoryBlock
{
	struct MinNode node;
	u32 size;
};

struct MemoryRegion
{
	char name[32];
	struct MinList memlist;
};

namespace TFE_Memory
{
	struct MemoryBlock *GetNewBlock(MemoryRegion* region, u32 size)
	{
		struct MemoryBlock *node;

		node = (struct MemoryBlock *)malloc(sizeof(struct MemoryBlock) + size);
		if (node)
		{
			node->size = size;
			AddTail((struct List *)&region->memlist, (struct Node *)node);
		}

		return node;
	}

	void FreeBlock(MemoryBlock *node)
	{
		Remove((struct Node *)node);
		free(node);
	}
	
	struct MemoryBlock *GetPtrBlock(void *ptr)
	{
		struct MemoryBlock *node = ((MemoryBlock *)ptr - 1);
		return node;
	}
	
	void *GetBlockPtr(struct MemoryBlock *node)
	{
		void* mem = (node + 1);
		return mem;
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
		NewList((struct List *)&region->memlist);

		return region;
	}

	void region_clear(MemoryRegion* region)
	{
		assert(region);
		struct MemoryBlock *node, *node2;
		for (node = (struct MemoryBlock *)region->memlist.mlh_Head; (node2 = (struct MemoryBlock *)node->node.mln_Succ); node = node2)
		{
			FreeBlock(node);
		}
	}

	void region_destroy(MemoryRegion* region)
	{
		assert(region);
		region_clear(region);
		free(region);
	}

	void* region_alloc(MemoryRegion* region, size_t size)
	{
		assert(region);
		if (size == 0) { return nullptr; }

		//TFE_System::logWrite(LOG_MSG, "MemoryRegion", "%s(%p,%d)\n", __FUNCTION__, region, size);
		struct MemoryBlock *node = GetNewBlock(region, size);
		if (node)
		{
			void* mem = GetBlockPtr(node);
			//TFE_System::logWrite(LOG_MSG, "MemoryRegion", "%s mem %p\n", __FUNCTION__, mem);
			return mem;
		}

		// We are all out of memory...
		TFE_System::logWrite(LOG_ERROR, "MemoryRegion", "Failed to allocate %u bytes in region '%s'.", size, region->name);
		return nullptr;
	}

	void* region_realloc(MemoryRegion* region, void* ptr, size_t size)
	{
		assert(region);
		if (!ptr) { return region_alloc(region, size); }
		if (size == 0) { return nullptr; }

		struct MemoryBlock *node, *node2;
		node = GetPtrBlock(ptr);

		u32 prevSize = node->size;
		// If the current block is already large enough, skip looping over the memory blocks.
		if (prevSize >= size)
		{
			return ptr;
		}

		// Allocate a new block of memory.
		node2 = GetNewBlock(region, size);
		if (!node2) { return nullptr; }
		void* newMem = GetBlockPtr(node2);
		//if (!newMem) { return nullptr; }
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

		struct MemoryBlock *node = GetPtrBlock(ptr);
		FreeBlock(node);
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

	void region_test()
	{
	}
}