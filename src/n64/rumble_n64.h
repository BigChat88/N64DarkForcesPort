#pragma once
// Rumble Pak feedback for the N64: the motor of the Rumble Pak in controller 1
// runs for a short time when the player takes damage. It can be turned off from
// the Options panel of the escape menu (TFE system setting "rumble").
#include <TFE_System/types.h>

namespace Rumble_N64
{
	// Runs the motor for at least the given time (pulses overlap, they do not add up).
	void pulse(u32 ms);

	// Starts or stops the motor. Called once per frame; outside of gameplay (menus,
	// PDA, escape menu) the motor is stopped and pending pulses are dropped.
	void update(bool inGame);
}
