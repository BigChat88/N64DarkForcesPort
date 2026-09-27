# SoundFont for the music

Dark Forces' music is General MIDI, played on the N64 by libdragon's SF64
synthesizer. Put a SoundFont 2 file (`.sf2`) in this folder; the build converts
the first one it finds to `rom:/MUSIC.SF64`. SoundFonts are not part of the
repository (`*.sf2` is ignored by git).

A Roland SC-55 style General MIDI SoundFont sounds closest to the original
game. Without a SoundFont the game runs without music.
