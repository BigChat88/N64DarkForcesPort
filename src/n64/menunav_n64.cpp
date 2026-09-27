#include "menunav_n64.h"

namespace MenuNav_N64
{
	// The stick counts as a direction beyond c_stickPress and is released below
	// c_stickRelease, so a stick resting near the threshold does not repeat presses.
	static const s32 c_stickPress = 40;
	static const s32 c_stickRelease = 25;

	static u32 s_stickHeld = 0;
	static u32 s_held = 0;
	static u32 s_pressed = 0;

	static u32 stickDirection(u32 dir, s32 value, u32 positive, u32 negative)
	{
		if (dir & positive) { return value > c_stickRelease ? positive : 0; }
		if (dir & negative) { return value < -c_stickRelease ? negative : 0; }
		if (value > c_stickPress) { return positive; }
		if (value < -c_stickPress) { return negative; }
		return 0;
	}

	u32 held()
	{
		return s_held;
	}

	u32 pressed()
	{
		return s_pressed;
	}

	void update(bool dUp, bool dDown, bool dLeft, bool dRight, s8 stickX, s8 stickY)
	{
		s_stickHeld = stickDirection(s_stickHeld, stickY, NAV_UP, NAV_DOWN) |
		              stickDirection(s_stickHeld, stickX, NAV_RIGHT, NAV_LEFT);

		u32 held = s_stickHeld;
		if (dUp)    { held |= NAV_UP; }
		if (dDown)  { held |= NAV_DOWN; }
		if (dLeft)  { held |= NAV_LEFT; }
		if (dRight) { held |= NAV_RIGHT; }

		// Presses accumulate until the game consumes the input frame.
		s_pressed |= held & ~s_held;
		s_held = held;
	}

	void endFrame()
	{
		s_pressed = 0;
	}
}
