// Frame time profiler, see profile_n64.h.
#include "profile_n64.h"
#include <cstdio>

#include <libdragon.h>

namespace Profile_N64
{
	static u32 s_start[PZ_COUNT];
	static u32 s_total[PZ_COUNT];
	static u32 s_frameStart = 0;
	static u32 s_periodTicks = 0;
	static u32 s_periodFrames = 0;
	static char s_text[256] = "";

	void begin(Zone zone)
	{
		s_start[zone] = TICKS_READ();
	}

	void end(Zone zone)
	{
		s_total[zone] += TICKS_READ() - s_start[zone];
	}

	// Tenths of a millisecond per frame.
	static u32 tenthsPerFrame(u32 ticks)
	{
		return (u32)((u64)ticks * 10000 / TICKS_PER_SECOND / s_periodFrames);
	}

	void frameShown()
	{
		const u32 now = TICKS_READ();
		if (s_frameStart)
		{
			s_periodTicks += now - s_frameStart;
			s_periodFrames++;
		}
		s_frameStart = now;
		if (s_periodTicks < TICKS_PER_SECOND) { return; }

		u32 ms[PZ_COUNT];
		for (s32 i = 0; i < PZ_COUNT; i++)
		{
			ms[i] = tenthsPerFrame(s_total[i]);
			s_total[i] = 0;
		}
		const u32 frame = tenthsPerFrame(s_periodTicks);
		const u32 fps = (u32)((u64)s_periodFrames * 10 * TICKS_PER_SECOND / s_periodTicks);
		const u32 rendered = ms[PZ_WALLS] + ms[PZ_FLATS] + ms[PZ_TRANS] + ms[PZ_SPRITES] + ms[PZ_MODELS];
		const u32 worldOther = ms[PZ_WORLD] > rendered ? ms[PZ_WORLD] - rendered : 0;
		const u32 logic = ms[PZ_TASKS] > ms[PZ_WORLD] ? ms[PZ_TASKS] - ms[PZ_WORLD] : 0;
		const u32 measured = ms[PZ_TASKS] + ms[PZ_AUDIO] + ms[PZ_SWAP];
		const u32 other = frame > measured ? frame - measured : 0;

		#define MS(v) (unsigned long)((v) / 10), (unsigned long)((v) % 10)
		snprintf(s_text, sizeof(s_text),
			"fps  %2lu.%lu\nframe%3lu.%lu\n"
			"world%3lu.%lu\n wall%3lu.%lu\n flat%3lu.%lu\n tran%3lu.%lu\n sprt%3lu.%lu\n 3do %3lu.%lu\n othr%3lu.%lu\n"
			"logic%3lu.%lu\naudio%3lu.%lu\nswap %3lu.%lu\nother%3lu.%lu",
			MS(fps), MS(frame),
			MS(ms[PZ_WORLD]), MS(ms[PZ_WALLS]), MS(ms[PZ_FLATS]), MS(ms[PZ_TRANS]), MS(ms[PZ_SPRITES]), MS(ms[PZ_MODELS]), MS(worldOther),
			MS(logic), MS(ms[PZ_AUDIO]), MS(ms[PZ_SWAP]), MS(other));
		#undef MS

		s_periodTicks = 0;
		s_periodFrames = 0;
	}

	const char* getText()
	{
		return s_text;
	}
}
