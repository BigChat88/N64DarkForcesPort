#include <cstring>
#include <TFE_Audio/midi.h>
//#include <TFE_Audio/midiPlayer.h>

#include <TFE_Audio/systemMidiDevice.h>
#include <TFE_System/system.h>

#include <midi/camd.h>
#include <midi/mididefs.h>
#include <proto/camd.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <SDI_compiler.h>

struct Library *CamdBase = NULL;

static struct MidiNode *g_midiNode = NULL;
static struct MidiLink *g_midiLink = NULL;

namespace TFE_Audio
{
	SystemMidiDevice::SystemMidiDevice()
	{
		m_outputId = -1;
		if ((CamdBase = OpenLibrary((STRPTR)"camd.library", 37)))
		{
			getOutputCount();
		}
	}

	SystemMidiDevice::~SystemMidiDevice()
	{
		exit();
	}

	void SystemMidiDevice::exit()
	{
		if (g_midiNode)
		{
			FlushMidi(g_midiNode);
			if (g_midiLink)
			{
				RemoveMidiLink(g_midiLink);
				g_midiLink = NULL;
			}
			DeleteMidi(g_midiNode);
			g_midiNode = NULL;
		}
		if (CamdBase)
		{
			CloseLibrary(CamdBase);
			CamdBase = NULL;
		}
		m_outputId = -1;
	}

	const char* SystemMidiDevice::getName()
	{
		return "CAMD";
	}

	void SystemMidiDevice::message(u8 type, u8 arg1, u8 arg2)
	{
		MidiMsg mm;
		mm.mm_Status = type;
		mm.mm_Data1  = arg1;
		mm.mm_Data2  = arg2;
		PutMidiMsg(g_midiLink, &mm);
#if 0
		const u8 msgType = type & 0xf0;
		const u8 channel = type & 0x0f;

		switch (msgType)
		{
		case MID_NOTE_OFF:
			break;
		case MID_NOTE_ON:
			break;
		case MID_CONTROL_CHANGE:
			break;
		case MID_PROGRAM_CHANGE:
			TFE_System::logWrite(LOG_MSG, __FILE__, "MID_PROGRAM_CHANGE %2u %u\n", channel, arg1);
			break;
		case MID_PITCH_BEND:
			TFE_System::logWrite(LOG_MSG, __FILE__, "MID_PITCH_BEND %2u %u\n", channel, (s32(arg2) << 7) | s32(arg1));
			break;
		}
#endif
	}

	void SystemMidiDevice::message(const u8* msg, u32 len)
	{
		/*
		MidiMsg mm;
		mm.mm_Status = msg[0];
		mm.mm_Data1  = msg[1];
		mm.mm_Data2  = msg[2];
		PutMidiMsg(g_midiLink, &mm);
		*/
		message(msg[0], msg[1], len >= 2 ? msg[2] : 0);
	}

	void SystemMidiDevice::noteAllOff()
	{
		// not supported
	}

	void SystemMidiDevice::setVolume(f32 volume)
	{
		// No-op.
	}

	u32 SystemMidiDevice::getOutputCount()
	{
		if (m_outputs.empty())
		{
			APTR lock = LockCAMD(CD_Linkages);
			struct MidiCluster *cluster = NULL;
			while ((cluster = NextCluster(cluster)))
			{
				// skip the input clusters
				if (IsListEmpty(&cluster->mcl_Receivers))
					continue;

				//TFE_System::logWrite(LOG_MSG, "Midi Device", "found cluster: %s", cluster->mcl_Node.ln_Name);
				m_outputs.push_back(cluster->mcl_Node.ln_Name);
			}
			UnlockCAMD(lock);
		}

		return (u32)m_outputs.size();
	}

	void SystemMidiDevice::getOutputName(s32 index, char* buffer, u32 maxLength)
	{
		*buffer = 0;
	}

	bool SystemMidiDevice::selectOutput(s32 index) 
	{
		if (!CamdBase) { return false; }
		if (index < 0 || index >= (s32)getOutputCount())
		{
			index = 0;
		}
		if (index != m_outputId && index < (s32)getOutputCount())
		{
			g_midiNode = CreateMidi(
				MIDI_MsgQueue, 0,
				MIDI_SysExSize, 0,
				MIDI_Name, (IPTR)"TFE Midi Out",
				TAG_END);
			if (g_midiNode)
			{
				TFE_System::logWrite(LOG_MSG, "Midi Device", "selected cluster: %s", m_outputs[index].c_str());
				g_midiLink = AddMidiLink(g_midiNode, MLTYPE_Sender,
					MLINK_Location, (IPTR)m_outputs[index].c_str(),
					TAG_END);
				if (g_midiLink)
				{
					return true;
				}
			}
		}

		//exit();
		return false;
	}

	s32 SystemMidiDevice::getActiveOutput(void)
	{
		return m_outputId > 0 ? m_outputId : 0;
	}
}


