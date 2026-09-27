#pragma once

namespace TFE_Audio
{
	// Fills every free libdragon audio buffer. Called once per frame from the
	// main loop: iMuse is not thread safe, so mixing never happens in an interrupt.
	void n64_update();
}
