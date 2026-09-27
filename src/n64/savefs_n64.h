#pragma once
// "save:/" filesystem: a handful of small files kept in RAM and written back to
// the cartridge SRAM whenever a file opened for writing is closed. Used for the
// agent progress (DARKPILO.CFG) and TFE settings.

namespace SaveFS_N64
{
	// Mounts "save:/" and loads the current contents from SRAM.
	void init();
}
