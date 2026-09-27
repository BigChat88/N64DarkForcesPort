#pragma once
// Boot-time "powered by libdragon" dragon logo animation, see intro_n64.cpp.

namespace Intro_N64
{
	// Plays the animation (~5 seconds, silent). Sets up and tears down its own
	// display and rdpq, so it must run before the render backend is initialized.
	void play();
}
