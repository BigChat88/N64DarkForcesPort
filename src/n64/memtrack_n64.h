#pragma once
// Development tool: records which code owns the live heap memory, so the
// out-of-memory report can list the biggest owners (as code addresses, to be
// resolved with mips64-elf-addr2line against build/darkforces64.elf).
#include <cstddef>
#include <cstdint>

namespace MemTrack_N64
{
	void onAlloc(void* ptr, size_t size, uintptr_t caller);
	void onFree(void* ptr);
	// Re-attributes a tracked allocation (e.g. region blocks to the TFE caller).
	void setCaller(void* ptr, uintptr_t caller);

	// Appends "caller bytes" lines for the biggest owners to 'out'.
	int report(char* out, int outSize, int maxLines);
}
