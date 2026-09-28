#pragma once
// Frame time profiler (make N64_PROFILE=1).
//
// Each zone adds up the CPU time spent between N64_PROFILE_BEGIN() and
// N64_PROFILE_END(); about once a second the totals are turned into average
// milliseconds per displayed frame, shown in the top right corner of the screen.
// Zones of the same kind must not nest (the sector renderer recurses through
// adjoins, but each of its zones ends before the recursion).
#include <TFE_System/types.h>

#ifndef N64_PROFILE
#define N64_PROFILE 0
#endif

namespace Profile_N64
{
	enum Zone
	{
		PZ_WORLD,		// drawWorld(): the whole 3D view
		PZ_WALLS,		// solid walls and signs
		PZ_FLATS,		// floors, ceilings and skies
		PZ_TRANS,		// transparent (mid texture) walls
		PZ_SPRITES,		// sprites and frames
		PZ_MODELS,		// 3D objects
		PZ_TASKS,		// game tasks: logic, AI, INF, and drawing the frame
		PZ_AUDIO,		// sound effects and music synthesis
		PZ_SWAP,		// RDP blit, overlays, waiting for a free display buffer
		PZ_COUNT
	};

	void begin(Zone zone);
	void end(Zone zone);
	// A frame was shown: closes the frame and updates the averages about once a second.
	void frameShown();
	const char* getText();
}

#if N64_PROFILE
#define N64_PROFILE_BEGIN(zone) Profile_N64::begin(Profile_N64::zone)
#define N64_PROFILE_END(zone)   Profile_N64::end(Profile_N64::zone)
#else
#define N64_PROFILE_BEGIN(zone)
#define N64_PROFILE_END(zone)
#endif
