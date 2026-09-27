#pragma once
// Development counters shown in an on-screen overlay (the ISViewer log is not
// visible in every emulator, and never on real hardware).
#include <TFE_System/types.h>

namespace Debug_N64
{
	struct AudioStats
	{
		bool callbackSet;	// iMuse registered its mixer
		u32  buffersFilled;	// libdragon audio buffers written
		s32  peak;			// highest |sample| written since the last overlay update
		u32  soundLoads;	// sounds read from ROM into the cache
		u32  soundFailures;	// sounds that could not be loaded
		u32  soundResolves;	// data requests from iMuse
		u32  cacheBytes;	// bytes currently cached
		s32  activeSounds;	// wave sounds being mixed by iMuse
		s32  maxVolume;		// highest volume among them
		s32  mixerBytes;	// heap used by audio_init + mixer_init + channel limits
		s32  musicBytes;	// heap used by the SoundFont bank and synthesizer
	};
	extern AudioStats s_audio;

	// Builds the overlay text; empty when the overlay is disabled.
	const char* getOverlayText();
	void toggleOverlay();
}
