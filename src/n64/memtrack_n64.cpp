#include "memtrack_n64.h"

// Enabled with "make N64_MEMTRACK=1": the table costs ~96KB of RAM.
#if N64_MEMTRACK
#include <cstdio>
#include <cstring>

namespace MemTrack_N64
{
	enum
	{
		TABLE_SIZE  = 8192,	// power of two
		MAX_OWNERS  = 256,
	};

	struct Allocation
	{
		uintptr_t ptr;		// 0 = empty, 1 = deleted
		uint32_t  size;
		uintptr_t caller;
	};

	static Allocation s_table[TABLE_SIZE];

	static uint32_t slotOf(uintptr_t ptr)
	{
		return (uint32_t)((ptr >> 3) * 2654435761u) & (TABLE_SIZE - 1);
	}

	void onAlloc(void* ptr, size_t size, uintptr_t caller)
	{
		if (!ptr) { return; }
		uint32_t slot = slotOf((uintptr_t)ptr);
		for (int i = 0; i < TABLE_SIZE; i++, slot = (slot + 1) & (TABLE_SIZE - 1))
		{
			if (s_table[slot].ptr <= 1)
			{
				s_table[slot].ptr = (uintptr_t)ptr;
				s_table[slot].size = (uint32_t)size;
				s_table[slot].caller = caller;
				return;
			}
		}
		// Table full: the allocation is simply not tracked.
	}

	void onFree(void* ptr)
	{
		if (!ptr) { return; }
		uint32_t slot = slotOf((uintptr_t)ptr);
		for (int i = 0; i < TABLE_SIZE; i++, slot = (slot + 1) & (TABLE_SIZE - 1))
		{
			if (s_table[slot].ptr == 0) { return; }
			if (s_table[slot].ptr == (uintptr_t)ptr)
			{
				s_table[slot].ptr = 1;
				return;
			}
		}
	}

	void setCaller(void* ptr, uintptr_t caller)
	{
		if (!ptr) { return; }
		uint32_t slot = slotOf((uintptr_t)ptr);
		for (int i = 0; i < TABLE_SIZE; i++, slot = (slot + 1) & (TABLE_SIZE - 1))
		{
			if (s_table[slot].ptr == 0) { return; }
			if (s_table[slot].ptr == (uintptr_t)ptr)
			{
				s_table[slot].caller = caller;
				return;
			}
		}
	}

	int report(char* out, int outSize, int maxLines)
	{
		static uintptr_t callers[MAX_OWNERS];
		static uint32_t bytes[MAX_OWNERS];
		int owners = 0;
		for (int i = 0; i < TABLE_SIZE; i++)
		{
			if (s_table[i].ptr <= 1) { continue; }
			int o = 0;
			for (; o < owners && callers[o] != s_table[i].caller; o++) {}
			if (o == owners)
			{
				if (owners == MAX_OWNERS) { continue; }
				callers[owners] = s_table[i].caller;
				bytes[owners] = 0;
				owners++;
			}
			bytes[o] += s_table[i].size;
		}

		int len = 0;
		for (int line = 0; line < maxLines && len < outSize; line++)
		{
			int best = -1;
			for (int o = 0; o < owners; o++)
			{
				if (bytes[o] && (best < 0 || bytes[o] > bytes[best])) { best = o; }
			}
			if (best < 0) { break; }
			len += snprintf(out + len, outSize - len, "%08lx %luK\n", (unsigned long)callers[best], (unsigned long)(bytes[best] / 1024));
			bytes[best] = 0;
		}
		return len;
	}
}

#else

namespace MemTrack_N64
{
	void onAlloc(void* ptr, size_t size, uintptr_t caller) {}
	void onFree(void* ptr) {}
	void setCaller(void* ptr, uintptr_t caller) {}
	int report(char* out, int outSize, int maxLines) { return 0; }
}

#endif
