# SoundFont for the music

Dark Forces' music is General MIDI, played on the N64 by libdragon's SF64
synthesizer. `SC55.sf2` is the SoundFont the port uses: a Roland SC-55 style
General MIDI set, the sound module the original game's music was written for.

Every build converts it to `rom:/MUSIC.SF64` with `audioconv64`: the Makefile
(`make`) and `tools/pack_rom.py` (`build.cmd`) both use this exact file.
