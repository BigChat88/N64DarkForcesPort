#include "music_n64.h"
#include <TFE_Audio/midiDevice.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_System/system.h>
#include <cstdio>

#include <libdragon.h>

namespace Music_N64
{
	static const char* c_soundFontPath = "rom:/MUSIC.SF64";

	static sf64_bank_t*   s_bank = nullptr;
	static sf64_synth_t*  s_synth = nullptr;
	static midi_target_t* s_target = nullptr;

	// Mixer sample clock. MIDI events are stamped with s_now so the synthesizer
	// places them at the exact sample of the iMuse tick that produced them.
	static s64 s_now = 0;
	static s64 s_scheduledSample = 0;
	static s64 s_softSample = 0;
	static f64 s_sampleRemainder = 0.0;

	// Advances the synthesizer (envelopes, release tails) once per mixing round.
	static void softEvent(void* ctx, int roundSamples)
	{
		s_softSample += roundSamples;
		s_target->ops->process(s_target, s_softSample);
	}

	// Runs one iMuse MIDI tick at an exact sample time and schedules the next one.
	static int tickEvent(void* ctx)
	{
		const s64 now = s_scheduledSample;
		if (s_softSample < now)
		{
			s_target->ops->process(s_target, now);
			s_softSample = now;
		}

		s_now = now;
		const f64 seconds = TFE_MidiPlayer::n64_tick();

		const f64 samples = seconds * audio_get_frequency() + s_sampleRemainder;
		s32 delay = (s32)samples;
		if (delay < 1) { delay = 1; }
		s_sampleRemainder = samples - delay;

		s_scheduledSample += delay;
		return delay;
	}

	void init()
	{
		FILE* test = fopen(c_soundFontPath, "rb");
		if (!test)
		{
			TFE_System::logWrite(LOG_WARNING, "Music", "No SoundFont at %s, music disabled.", c_soundFontPath);
			return;
		}
		fclose(test);

		s_bank = sf64_load(c_soundFontPath);
		s_synth = sf64_synth_create(s_bank);
		sf64_synth_set_channels(s_synth, FIRST_CHANNEL, VOICES, MIXER_PRIORITY_MUSIC);
		// High notes play samples well above the output rate, and pitch bends or
		// vibrato push them higher still (Talay reaches ~99kHz); exceeding the limit
		// is a fatal assert in the mixer.
		for (s32 ch = FIRST_CHANNEL; ch < FIRST_CHANNEL + VOICES; ch++)
		{
			mixer_ch_set_limits(ch, 0, 192000, 0);
		}
		s_target = sf64_synth_midi_target(s_synth);
		if (s_target->ops->reset) { s_target->ops->reset(s_target, 0); }

		mixer_add_soft_event(softEvent, nullptr);
		mixer_add_event(0, tickEvent, nullptr);
	}

	bool isAvailable()
	{
		return s_target != nullptr;
	}

	s64 now()
	{
		return s_now;
	}

	midi_target_t* target()
	{
		return s_target;
	}
}

namespace TFE_Audio
{
	// iMuse -> SF64 synthesizer. The music volume setting is applied by
	// TFE_MidiPlayer by rescaling the channel volume (CC7) messages, since the
	// synthesizer has no global volume (hasGlobalVolumeCtrl() == false).
	class N64MidiDevice : public MidiDevice
	{
	public:
		MidiDeviceType getType() override { return MIDI_TYPE_SF2; }

		void exit() override {}
		bool hasGlobalVolumeCtrl() override { return false; }
		const char* getName() override { return "SF64 SoundFont"; }

		u32  getOutputCount() override { return 1; }
		void getOutputName(s32 index, char* buffer, u32 maxLength) override { snprintf(buffer, maxLength, "%s", getName()); }
		bool selectOutput(s32 index) override { return true; }
		s32  getActiveOutput(void) override { return 0; }

		// Audio is produced by the mixer, not rendered by the device.
		bool render(f32* buffer, u32 sampleCount) override { return false; }
		bool canRender() override { return false; }

		void message(u8 type, u8 arg1, u8 arg2) override
		{
			midi_target_t* t = Music_N64::target();
			if (!t) { return; }

			const s64 now = Music_N64::now();
			const s32 ch = type & 0x0f;
			switch (type & 0xf0)
			{
				case 0x80: t->ops->note_off(t, ch, arg1, arg2, now); break;
				case 0x90:
					if (arg2) { t->ops->note_on(t, ch, arg1, arg2, now); }
					else { t->ops->note_off(t, ch, arg1, 0, now); }
					break;
				case 0xa0: if (t->ops->poly_pressure) { t->ops->poly_pressure(t, ch, arg1, arg2, now); } break;
				case 0xb0: t->ops->control_change(t, ch, arg1, arg2, now); break;
				case 0xc0: t->ops->program_change(t, ch, arg1, now); break;
				case 0xd0: if (t->ops->channel_pressure) { t->ops->channel_pressure(t, ch, arg1, now); } break;
				case 0xe0: t->ops->pitch_bend(t, ch, arg1 | (arg2 << 7), now); break;
				default: break;	// System messages are not used by Dark Forces.
			}
		}

		void message(const u8* msg, u32 len) override
		{
			if (len >= 1 && msg[0] < 0xf0)
			{
				message(msg[0], len > 1 ? msg[1] : 0, len > 2 ? msg[2] : 0);
			}
		}

		void noteAllOff() override
		{
			for (s32 ch = 0; ch < 16; ch++)
			{
				message(0xb0 | ch, 123, 0);	// All Notes Off
			}
		}

		void setVolume(f32 volume) override {}
	};

	MidiDevice* createN64MidiDevice()
	{
		return new N64MidiDevice();
	}
}
