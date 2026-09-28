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

	// Last MIDI state per channel, replayed after resume(). -1 = never set.
	enum { MIDI_CHANNELS = 16, MIDI_CONTROLLERS = 120 };
	static s16 s_program[MIDI_CHANNELS];
	static s16 s_controller[MIDI_CHANNELS][MIDI_CONTROLLERS];
	static s32 s_pitchBend[MIDI_CHANNELS];
	static bool s_suspended = false;

	static void resetChannelState()
	{
		for (s32 ch = 0; ch < MIDI_CHANNELS; ch++)
		{
			s_program[ch] = -1;
			s_pitchBend[ch] = -1;
			for (s32 c = 0; c < MIDI_CONTROLLERS; c++) { s_controller[ch][c] = -1; }
		}
	}

	void recordMessage(u8 type, u8 arg1, u8 arg2)
	{
		const s32 ch = type & 0x0f;
		switch (type & 0xf0)
		{
			case 0xb0: if (arg1 < MIDI_CONTROLLERS) { s_controller[ch][arg1] = arg2; } break;
			case 0xc0: s_program[ch] = arg1; break;
			case 0xe0: s_pitchBend[ch] = arg1 | (arg2 << 7); break;
			default: break;
		}
	}

	// Advances the synthesizer (envelopes, release tails) once per mixing round.
	static void softEvent(void* ctx, int roundSamples)
	{
		s_softSample += roundSamples;
		if (s_target) { s_target->ops->process(s_target, s_softSample); }
	}

	// Runs one iMuse MIDI tick at an exact sample time and schedules the next one.
	static int tickEvent(void* ctx)
	{
		const s64 now = s_scheduledSample;
		if (s_softSample < now)
		{
			if (s_target) { s_target->ops->process(s_target, now); }
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

		resetChannelState();
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

	void suspend()
	{
		if (!s_bank || s_suspended) { return; }
		// message() stops forwarding (but keeps recording) once there is no target.
		s_target = nullptr;
		sf64_synth_close(s_synth);
		sf64_close(s_bank);
		s_synth = nullptr;
		s_bank = nullptr;
		s_suspended = true;
	}

	void resume()
	{
		if (!s_suspended) { return; }
		s_suspended = false;

		s_bank = sf64_load(c_soundFontPath);
		s_synth = sf64_synth_create(s_bank);
		sf64_synth_set_channels(s_synth, FIRST_CHANNEL, VOICES, MIXER_PRIORITY_MUSIC);
		for (s32 ch = FIRST_CHANNEL; ch < FIRST_CHANNEL + VOICES; ch++)
		{
			mixer_ch_set_limits(ch, 0, 192000, 0);
		}
		midi_target_t* t = sf64_synth_midi_target(s_synth);
		if (t->ops->reset) { t->ops->reset(t, s_now); }

		// Restore the instruments and controllers iMuse set up before the suspension.
		for (s32 ch = 0; ch < MIDI_CHANNELS; ch++)
		{
			if (s_program[ch] >= 0) { t->ops->program_change(t, ch, s_program[ch], s_now); }
			for (s32 c = 0; c < MIDI_CONTROLLERS; c++)
			{
				if (s_controller[ch][c] >= 0) { t->ops->control_change(t, ch, c, s_controller[ch][c], s_now); }
			}
			if (s_pitchBend[ch] >= 0) { t->ops->pitch_bend(t, ch, s_pitchBend[ch], s_now); }
		}
		s_target = t;
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
			Music_N64::recordMessage(type, arg1, arg2);
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
