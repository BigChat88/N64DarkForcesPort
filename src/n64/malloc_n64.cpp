// Allocation wrappers (linked with --wrap=malloc/calloc/realloc/memalign/free).
// The sound cache only uses memory nobody else needs: when an allocation fails,
// cached sounds are released one by one (least recently used first) and the
// allocation is retried. Live allocations are also recorded by MemTrack_N64 so
// an out-of-memory report can show who owns the heap.
// A small block is also held back from startup and released only when an allocation
// finally fails: the libdragon inspector needs some heap to draw the assertion page,
// and with none left it faulted and only showed the CPU registers.
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
}

namespace MemReserve_N64
{
	enum { RESERVE_SIZE = 32 * 1024 };
	static void* s_reserve = nullptr;

	void acquire()
	{
		if (!s_reserve) { s_reserve = __real_malloc(RESERVE_SIZE); }
	}

	void release()
	{
		if (s_reserve)
		{
			__real_free(s_reserve);
			s_reserve = nullptr;
		}
	}
}

extern "C"
{

	void* __wrap_malloc(size_t size)
	{
		void* ptr;
		while (!(ptr = __real_malloc(size)) && size && SoundCache_N64::purgeOne()) {}
		if (!ptr && size)
		{
			MemReserve_N64::release();
			TFE_Memory::n64_reportOutOfMemory("malloc", size);
		}
		MemTrack_N64::onAlloc(ptr, size, (uintptr_t)__builtin_return_address(0));
		return ptr;
	}

	// malloc_uncached() (mixer voice buffers, surfaces...) allocates with memalign(), so
	// it also has to release cached sounds before failing: a music voice starting after
	// the PDA was loaded in ARC ran out of memory with the sound cache still full.
	void* __real_memalign(size_t align, size_t size);
	void* __wrap_memalign(size_t align, size_t size)
	{
		void* ptr;
		while (!(ptr = __real_memalign(align, size)) && size && SoundCache_N64::purgeOne()) {}
		// The callers (mixer, surfaces) assert on failure: leave room for the report.
		if (!ptr && size) { MemReserve_N64::release(); }
		MemTrack_N64::onAlloc(ptr, size, (uintptr_t)__builtin_return_address(0));
		return ptr;
	}

	void* __wrap_calloc(size_t count, size_t size)
	{
		void* ptr;
		while (!(ptr = __real_calloc(count, size)) && count && size && SoundCache_N64::purgeOne()) {}
		if (!ptr && count && size) { MemReserve_N64::release(); }
		MemTrack_N64::onAlloc(ptr, count * size, (uintptr_t)__builtin_return_address(0));
		return ptr;
	}

	void* __wrap_realloc(void* ptr, size_t size)
	{
		void* newPtr;
		while (!(newPtr = __real_realloc(ptr, size)) && size && SoundCache_N64::purgeOne()) {}
		if (!newPtr && size) { MemReserve_N64::release(); }
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
