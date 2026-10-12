#include "rumble_n64.h"
#include <TFE_Settings/settings.h>

#include <libdragon.h>

namespace Rumble_N64
{
	static u32  s_stopTime = 0;		// get_ticks_ms() when the motor stops
	static bool s_pending = false;	// a pulse has not finished yet
	static bool s_active = false;	// last state sent to the Rumble Pak

	void pulse(u32 ms)
	{
		const u32 stop = get_ticks_ms() + ms;
		// Compared as a signed difference, so it also works when the counter wraps.
		if (!s_pending || s32(stop - s_stopTime) > 0) { s_stopTime = stop; }
		s_pending = true;
	}

	void update(bool inGame)
	{
		if (s_pending && (!inGame || s32(get_ticks_ms() - s_stopTime) >= 0)) { s_pending = false; }

		// The Rumble Pak can be swapped at any time; the joypad module tracks it.
		const bool supported = joypad_get_rumble_supported(JOYPAD_PORT_1);
		if (!supported) { s_active = false; }
		const bool active = s_pending && supported && TFE_Settings::getSystemSettings()->rumble;
		if (active != s_active)
		{
			joypad_set_rumble_active(JOYPAD_PORT_1, active);
			s_active = active;
		}
	}
}
