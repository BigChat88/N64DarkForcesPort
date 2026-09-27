#ifdef __cplusplus
extern "C" {
#endif

int  MIDI_LoadInstruments(void);
void MIDI_UnloadInstruments(void);
int  MIDI_InitVolumeTable(void);
void MIDI_FreeVolumeTable(void);
void MIDI_AllNotesOff(void);
void MIDI_NoteOff(int channel, int note);
void MIDI_NoteOn(int channel, int note, int velocity);
void MIDI_PitchBend(int channel, int pitch);
void MIDI_ControlChange(int channel, int controller, int value);
void MIDI_ProgramChange(int channel, int value);
void MIDI_FillBuffer(void *buffer, int samples);

#ifdef __cplusplus
}
#endif
