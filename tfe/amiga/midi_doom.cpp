#include <TFE_Audio/MidiSynth/fm4Opl3Device.h>
#include <TFE_Audio/midi.h>
#include <TFE_Jedi/Math/core_math.h>
#include <TFE_Jedi/IMuse/imList.h>
//#include <cstring>
//#include <algorithm>
//#include <assert.h>
#include "doommidi.h"
//#include <clib/debug_protos.h>

using namespace TFE_Jedi;

namespace TFE_Audio
{
	Fm4Opl3Device::~Fm4Opl3Device()
	{
		exit();
	}

	u32 Fm4Opl3Device::getOutputCount()
	{
		return 1;
	}

	void Fm4Opl3Device::getOutputName(s32 index, char* buffer, u32 maxLength)
	{
		*buffer = 0;
	}

	bool Fm4Opl3Device::selectOutput(s32 index)
	{
		if (!m_streamActive)
		{
			m_streamActive = MIDI_LoadInstruments() && MIDI_InitVolumeTable();
		}
		return m_streamActive;
	}

	s32 Fm4Opl3Device::getActiveOutput(void)
	{
		// Only output 0 exists.
		return 0;
	}

	void Fm4Opl3Device::exit()
	{
		MIDI_FreeVolumeTable();
		MIDI_UnloadInstruments();
		m_streamActive = false;
	}

	const char* Fm4Opl3Device::getName()
	{
		return "DoomSound";
	}

	bool Fm4Opl3Device::render(f32* buffer, u32 sampleCount)
	{
		if (!m_streamActive) { return false; }

		MIDI_FillBuffer(buffer, sampleCount);
		return true;
	}

	bool Fm4Opl3Device::canRender()
	{
		return m_streamActive;
	}

	void Fm4Opl3Device::setVolume(f32 volume)
	{
		//m_volume = volume;
		//m_volumeScaled = m_volume * c_outputScale;
	}

	// Raw midi commands.
	void Fm4Opl3Device::message(u8 type, u8 arg1, u8 arg2)
	{
		if (!m_streamActive) { return; }
		const u8 msgType = type & 0xf0;
		const u8 channel = type & 0x0f;
		//kprintf("msgType %lu channel %2lu arg1 %3lu arg2 %3lu\n", msgType, channel, arg1, arg2);
		switch (msgType)
		{
		case MID_NOTE_OFF:			//kprintf("MID_NOTE_OFF       channel %2lu arg1 %3lu\n", channel, arg1);
			MIDI_NoteOff(channel, arg1);
			break;
		case MID_NOTE_ON:			//kprintf("MID_NOTE_ON        channel %2lu arg1 %3lu arg2 %3lu\n", channel, arg1, arg2);
			if (arg2 != 0) // TODO integrate this
				MIDI_NoteOn(channel, arg1, arg2);
			else
				MIDI_NoteOff(channel, arg1);
			break;
		case MID_CONTROL_CHANGE:	//kprintf("MID_CONTROL_CHANGE channel %2lu arg1 %3lu arg2 %3lu\n", channel, arg1, arg2);
			MIDI_ControlChange(channel, arg1, arg2);
			break;
		case MID_PROGRAM_CHANGE:	//kprintf("MID_PROGRAM_CHANGE channel %2lu arg1 %3lu\n", channel, arg1);
			MIDI_ProgramChange(channel, arg1);
			break;
		case MID_PITCH_BEND:		//kprintf("MID_PITCH_BEND     channel %2lu arg1 %3lu arg2 %3lu\n", channel, arg1, arg2);
			// probably unused
			//MIDI_PitchBend(channel, ((s32(arg2) << 7) | s32(arg1)) >> 6);
			break;
		}
	}

	void Fm4Opl3Device::message(const u8* msg, u32 len)
	{
		message(msg[0], msg[1], len >= 2 ? msg[2] : 0);
	}

	void Fm4Opl3Device::noteAllOff()
	{
		if (!m_streamActive) { return; }

		MIDI_AllNotesOff();
	}
};