#pragma once
// Music for the N64: iMuse's MIDI output is played by libdragon's SF64
// SoundFont synthesizer on the mixer. The SoundFont is the user's own .sf2
// (see soundfont/ in the repository root), converted to rom:/MUSIC.SF64.

namespace Music_N64
{
	enum
	{
		// Mixer channels 0-1 are the (stereo) iMuse digital sound stream.
		FIRST_CHANNEL = 2,
		VOICES        = 16,	// the synthesizer steals voices when all are busy
	};

	// Loads the SoundFont and starts driving iMuse from the mixer. Must be
	// called after the mixer is initialized. Without a SoundFont in the ROM,
	// music is simply disabled.
	void init();

	// Frees the SoundFont and synthesizer (~275KB) while the music is paused, e.g. while
	// the PDA is open, and loads them again. The MIDI channel state (programs, controllers,
	// pitch bend) is kept and sent again on resume so the instruments stay the same.
	void suspend();
	void resume();
}
