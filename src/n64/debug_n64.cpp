#include "debug_n64.h"
#include "memtrack_n64.h"
#include <cstdio>

#include <libdragon.h>

namespace TFE_Memory
{
	s32 n64_regionSummary(char* out, s32 outSize);
}

namespace Debug_N64
{
	AudioStats s_audio = {};

	static bool s_overlayEnabled = N64_DEBUG_OVERLAY != 0;
	static char s_text[640];
	static char s_memText[320];

	void toggleOverlay()
	{
		s_overlayEnabled = !s_overlayEnabled;
	}

	const char* getOverlayText()
	{
		if (!s_overlayEnabled) { return ""; }

		// Region sizes and the largest heap owners (N64_MEMTRACK builds) change slowly and
		// walking the allocation table is expensive, so refresh them about once a second.
		static s32 s_memFrames = 0;
		if (s_memFrames-- <= 0)
		{
			s_memFrames = 60;
			s32 len = TFE_Memory::n64_regionSummary(s_memText, sizeof(s_memText));
			if (len < (s32)sizeof(s_memText) - 1)
			{
				s_memText[len++] = '\n';
				s_memText[len] = 0;
				MemTrack_N64::report(s_memText + len, sizeof(s_memText) - len, 4);
			}
		}

		heap_stats_t heap;
		sys_get_heap_stats(&heap);
		snprintf(s_text, sizeof(s_text),
			"mix:%s buf:%lu peak:%ld act:%ld vol:%ld\nsnd load:%lu fail:%lu req:%lu cache:%luK\nheap free:%dK mixer:%ldK music:%ldK\n%s",
			s_audio.callbackSet ? "on" : "OFF", (unsigned long)s_audio.buffersFilled, (long)s_audio.peak,
			(long)s_audio.activeSounds, (long)s_audio.maxVolume,
			(unsigned long)s_audio.soundLoads, (unsigned long)s_audio.soundFailures, (unsigned long)s_audio.soundResolves,
			(unsigned long)(s_audio.cacheBytes / 1024), heap.free / 1024,
			(long)(s_audio.mixerBytes / 1024), (long)(s_audio.musicBytes / 1024), s_memText);

		// Audio buffers are only written every few video frames, so hold the peak for ~0.5s.
		static s32 s_peakFrames = 0;
		if (++s_peakFrames >= 30)
		{
			s_peakFrames = 0;
			s_audio.peak = 0;
		}
		return s_text;
	}
}
