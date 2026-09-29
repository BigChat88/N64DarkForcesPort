#pragma once
// Releases the part of a level the player can no longer go back to.
//
// ARC does not fit in 8MB up to its end: at the final fight the heap has enough free
// bytes in total but no hole large enough for Mohc's speech (M16MOC01.VOC, 83KB).
// Once the player rides the one-way elevator to Mohc, every object of the Arc Hammer
// interior is deleted and the sprites nobody uses any more are freed, which returns
// large contiguous blocks (PHASE1.WAX alone is 140KB) to the heap.

namespace LevelPurge_N64
{
	// Call when a level is loaded or a game restored.
	void reset();
	// Call once per frame while the level runs.
	void update();
}
