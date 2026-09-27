#pragma once
// Console style menu navigation: the D-pad and the analog stick both give the four
// directions. Menus that support it are driven with them instead of the cursor.
#include <TFE_System/types.h>

namespace MenuNav_N64
{
	enum Direction
	{
		NAV_UP    = 1 << 0,
		NAV_DOWN  = 1 << 1,
		NAV_LEFT  = 1 << 2,
		NAV_RIGHT = 1 << 3,
	};

	// Directions held right now.
	u32 held();
	// Directions pressed since the last input frame (see TFE_Input::endFrame()).
	u32 pressed();

	// Called by the main loop.
	void update(bool dUp, bool dDown, bool dLeft, bool dRight, s8 stickX, s8 stickY);
	void endFrame();
}
