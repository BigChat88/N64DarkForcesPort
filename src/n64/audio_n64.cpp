// Audio backend for the N64, built on libdragon's mixer.
//
// Mixer channel 0 streams the iMuse digital sound effects: with __AMIGA__
// defined, iMuse outputs interleaved stereo signed 8-bit samples at 11025 Hz,
// which are widened to 16-bit on demand and resampled by the mixer. The other
// channels belong to the SoundFont synthesizer playing the music (see
// music_n64.cpp). Mixing runs on the main thread from n64_update(), since
// iMuse is not thread safe.
#include "audio_n64.h"
#include "debug_n64.h"
#include "music_n64.h"
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/systemMidiDevice.h>
#include <TFE_Audio/MidiSynth/fm4Opl3Device.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <cstring>

#include <libdragon.h>

namespace TFE_Audio
{
	enum
	{
		OUTPUT_FREQ = 32000,
		IMUSE_FREQ  = 11025,
		SFX_CHANNEL = 0,
		MIXER_CHANNELS = Music_N64::FIRST_CHANNEL + Music_N64::VOICES,
		// iMuse asserts that it never mixes more than 256 stereo samples per call.
		MIX_CHUNK_SAMPLES = 256,
	};

	static AudioThreadCallback s_audioCallback = nullptr;
	static f32  s_volume = 1.0f;
	static bool s_paused = false;
	static bool s_initialized = false;
	static s8   s_mixBuffer[MIX_CHUNK_SAMPLES * 2 + 16];
	static waveform_t s_sfxWave;

	// Mixer read callback of the sound effect stream.
	static void readSfx(void* ctx, samplebuffer_t* sbuf, int wpos, int wlen, bool seeking)
	{
		s16* out = (s16*)samplebuffer_append(sbuf, wlen);
		while (wlen > 0)
		{
			const s32 count = wlen < MIX_CHUNK_SAMPLES ? wlen : MIX_CHUNK_SAMPLES;
			if (s_audioCallback && !s_paused)
			{
				s_audioCallback((f32*)s_mixBuffer, count, 1.0f);
				s32 peak = Debug_N64::s_audio.peak;
				for (s32 i = 0; i < count * 2; i++)
				{
					const s32 sample = s32(s_mixBuffer[i]);
					out[i] = s16(sample << 8);
					const s32 mag = sample < 0 ? -sample : sample;
					if (mag > peak) { peak = mag; }
				}
				Debug_N64::s_audio.peak = peak;
			}
			else
			{
				memset(out, 0, count * 2 * sizeof(s16));
			}
			out += count * 2;
			wlen -= count;
		}
	}

	bool init(bool useNullDevice/*=false*/, s32 outputId/*=-1*/)
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_AudioSystem::init");
		heap_stats_t heapBefore, heapAfter;
		sys_get_heap_stats(&heapBefore);
		audio_init(OUTPUT_FREQ, AUDIO_INIT_LATENCY_MS(120));
		mixer_init(MIXER_CHANNELS);

		memset(&s_sfxWave, 0, sizeof(s_sfxWave));
		s_sfxWave.name = "imuse-sfx";
		s_sfxWave.bits = 16;
		s_sfxWave.channels = 2;
		s_sfxWave.frequency = IMUSE_FREQ;
		s_sfxWave.len = WAVEFORM_UNKNOWN_LEN;
		s_sfxWave.read = readSfx;
		mixer_ch_play(SFX_CHANNEL, &s_sfxWave);

		s_initialized = true;
		setVolume(TFE_Settings::getSoundSettings()->soundFxVolume);

		sys_get_heap_stats(&heapAfter);
		Debug_N64::s_audio.mixerBytes = heapAfter.used - heapBefore.used;
		Music_N64::init();
		heap_stats_t heapMusic;
		sys_get_heap_stats(&heapMusic);
		Debug_N64::s_audio.musicBytes = heapMusic.used - heapAfter.used;
		return true;
	}

	void shutdown()
	{
		if (!s_initialized) { return; }
		mixer_close();
		audio_close();
		s_initialized = false;
	}

	void n64_update()
	{
		if (!s_initialized) { return; }
		while (audio_can_write())
		{
			s16* out = audio_write_begin();
			mixer_poll(out, audio_get_buffer_length());
			audio_write_end();
			Debug_N64::s_audio.buffersFilled++;
		}
	}

	void stopAllSounds() {}
	void selectDevice(s32 id) {}
	void setUpsampleFilter(AudioUpsampleFilter filter) {}
	AudioUpsampleFilter getUpsampleFilter() { return AUF_DEFAULT; }

	void setVolume(f32 volume)
	{
		s_volume = volume;
		if (s_initialized) { mixer_ch_set_vol(SFX_CHANNEL, volume, volume); }
	}
	f32 getVolume() { return s_volume; }
	void pause() { s_paused = true; }
	void resume() { s_paused = false; }

	// Mixing only ever runs on the main thread.
	void lock() {}
	void unlock() {}

	void bufferedAudioClear() {}
	void setAudioThreadCallback(AudioThreadCallback callback)
	{
		s_audioCallback = callback;
		Debug_N64::s_audio.callbackSet = (callback != nullptr);
	}

	const OutputDeviceInfo* getOutputDeviceList(s32& count, s32& curOutput)
	{
		count = 0;
		curOutput = 0;
		return nullptr;
	}

	// Sound sources are only used by the desktop builds; Dark Forces plays
	// everything through iMuse.
	bool playOneShot(SoundType type, f32 volume, const SoundBuffer* buffer, bool looping, SoundFinishedCallback finishedCallback, void* cbUserData, s32 cbArg) { return false; }
	SoundSource* createSoundSource(SoundType type, f32 volume, const SoundBuffer* buffer, SoundFinishedCallback callback, void* userData) { return nullptr; }
	s32 getSourceSlot(SoundSource* source) { return -1; }
	SoundSource* getSourceFromSlot(s32 slot) { return nullptr; }
	void playSource(SoundSource* source, bool looping) {}
	void stopSource(SoundSource* source) {}
	void freeSource(SoundSource* source) {}
	void setSourceVolume(SoundSource* source, f32 volume) {}
	void setSourceBuffer(SoundSource* source, const SoundBuffer* buffer) {}
	bool isSourcePlaying(SoundSource* source) { return false; }
	f32 getSourceVolume(SoundSource* source) { return 0.0f; }

	// MIDI devices referenced by TFE_MidiPlayer::allocateMidiDevice().
	SystemMidiDevice::SystemMidiDevice() : m_midiout(nullptr), m_outputId(-1) {}
	SystemMidiDevice::~SystemMidiDevice() {}
	void SystemMidiDevice::exit() {}
	const char* SystemMidiDevice::getName() { return "None"; }
	void SystemMidiDevice::message(u8 type, u8 arg1, u8 arg2) {}
	void SystemMidiDevice::message(const u8* msg, u32 len) {}
	void SystemMidiDevice::noteAllOff() {}
	void SystemMidiDevice::setVolume(f32 volume) {}
	u32 SystemMidiDevice::getOutputCount() { return 0; }
	void SystemMidiDevice::getOutputName(s32 index, char* buffer, u32 maxLength) { if (maxLength) { buffer[0] = 0; } }
	bool SystemMidiDevice::selectOutput(s32 index) { return false; }
	s32 SystemMidiDevice::getActiveOutput(void) { return -1; }

	Fm4Opl3Device::~Fm4Opl3Device() {}
	void Fm4Opl3Device::exit() {}
	const char* Fm4Opl3Device::getName() { return "OPL3 (disabled)"; }
	bool Fm4Opl3Device::render(f32* buffer, u32 sampleCount) { return false; }
	bool Fm4Opl3Device::canRender() { return false; }
	void Fm4Opl3Device::message(u8 type, u8 arg1, u8 arg2) {}
	void Fm4Opl3Device::message(const u8* msg, u32 len) {}
	void Fm4Opl3Device::noteAllOff() {}
	void Fm4Opl3Device::setVolume(f32 volume) {}
	u32 Fm4Opl3Device::getOutputCount() { return 0; }
	void Fm4Opl3Device::getOutputName(s32 index, char* buffer, u32 maxLength) { if (maxLength) { buffer[0] = 0; } }
	bool Fm4Opl3Device::selectOutput(s32 index) { return false; }
	s32 Fm4Opl3Device::getActiveOutput(void) { return -1; }
}
