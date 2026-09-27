// Allocation wrappers (linked with --wrap=malloc/calloc/realloc/free).
// The sound cache only uses memory nobody else needs: when an allocation fails,
// cached sounds are released one by one (least recently used first) and the
// allocation is retried. Live allocations are also recorded by MemTrack_N64 so
// an out-of-memory report can show who owns the heap.
#include "soundCache_n64.h"
#include "memtrack_n64.h"
#include <cstddef>
#include <cstdint>

namespace TFE_Memory
{
	void n64_reportOutOfMemory(const char* where, size_t size);
}

extern "C"
{
	void* __real_malloc(size_t size);
	void* __real_calloc(size_t count, size_t size);
	void* __real_realloc(void* ptr, size_t size);
	void  __real_free(void* ptr);

	void* __wrap_malloc(size_t size)
	{
		void* ptr;
		while (!(ptr = __real_malloc(size)) && size && SoundCache_N64::purgeOne()) {}
		if (!ptr && size) { TFE_Memory::n64_reportOutOfMemory("malloc", size); }
		MemTrack_N64::onAlloc(ptr, size, (uintptr_t)__builtin_return_address(0));
		return ptr;
	}

	void* __wrap_calloc(size_t count, size_t size)
	{
		void* ptr;
		while (!(ptr = __real_calloc(count, size)) && count && size && SoundCache_N64::purgeOne()) {}
		MemTrack_N64::onAlloc(ptr, count * size, (uintptr_t)__builtin_return_address(0));
		return ptr;
	}

	void* __wrap_realloc(void* ptr, size_t size)
	{
		void* newPtr;
		while (!(newPtr = __real_realloc(ptr, size)) && size && SoundCache_N64::purgeOne()) {}
		if (newPtr || !size)
		{
			MemTrack_N64::onFree(ptr);
			MemTrack_N64::onAlloc(newPtr, size, (uintptr_t)__builtin_return_address(0));
		}
		return newPtr;
	}

	void __wrap_free(void* ptr)
	{
		MemTrack_N64::onFree(ptr);
		__real_free(ptr);
	}
}
