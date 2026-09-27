#include <TFE_Audio/audioSystem.h>
//#include <TFE_Audio/audioDevice.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_System/system.h>
#include <TFE_Settings/settings.h>
#include <TFE_Jedi/IMuse/imuse.h> // IM_AUDIO_OVERSAMPLE

// AHI
#include <devices/ahi.h>
#include <proto/exec.h>
#include <proto/ahi.h>
#include <utility/hooks.h>
#include <SDI_hook.h>
#include <clib/debug_protos.h>

// Paula
#include <devices/audio.h>
#include <hardware/custom.h>
#include <hardware/intbits.h>
#include <hardware/dmabits.h>
#include <hardware/cia.h>
#include <graphics/gfxbase.h>
#include <proto/graphics.h>
#include <SDI_interrupt.h>

static_assert(IM_AUDIO_OVERSAMPLE == 0, "IM_AUDIO_OVERSAMPLE is not supported.");

//#define TEST_TONE
#define USE_MIDI_RENDER

static struct CIA *ciaa = (struct CIA *)0xbfe001;
static struct Custom *custom = (struct Custom *)0xdff000;

struct SoundSource
{
	int dummy;
};

namespace TFE_Audio
{
	enum
	{
		AUDIO_FREQ = 11025,
		AUDIO_CHANNEL_COUNT = 2,
		AUDIO_CALLBACK_BUFFER_SIZE = 256,
		MIDI_CALLBACK_BUFFER_SIZE = 128, // was 80 in DoomSound.library
		BUFFERED_SILENT_FRAME_COUNT = 16,
		AUDIO_BUFFER_SIZE = (AUDIO_CALLBACK_BUFFER_SIZE * AUDIO_CHANNEL_COUNT),
		MIDI_BUFFER_SIZE = (MIDI_CALLBACK_BUFFER_SIZE * AUDIO_CHANNEL_COUNT)
	};

	// AHI
	struct Library *AHIBase = NULL;
	static struct MsgPort *AHImp = NULL;
	static struct AHIRequest *AHIio = NULL;
	static struct AHIAudioCtrl *actrl = NULL;
	static struct Hook SoundHook;

	// Paula
	static struct Interrupt *oldIntAud0 = NULL;
	static struct Interrupt *oldIntAud2 = NULL;
	static struct MsgPort *audioMP = NULL;
	static struct IOAudio *audioIO = NULL;
	static BYTE audioDev = -1;
	static s8 *chipBuffer = NULL;
	static s8 *s_chipBufferAudio;
	static s8 *s_chipBufferMidi;
	static UBYTE oldFilter = CIAF_LED;
	static UWORD intFlags = 0;
	static UWORD dmaFlags = 0;
	static struct Interrupt AudioInt;

	// Client volume controls, ranging from [0, 1]
	static f32 s_soundFxVolume = 1.0f;
	static Fixed s_ahiVolume = 0x10000;

	static bool s_paused = false;
	static bool s_nullDevice = false;
	static volatile s32 s_silentAudioFrames = 0;
	static s8 s_sampleBuffer[AUDIO_BUFFER_SIZE * 2];	// 256 stereo + oversampling. double buffered
	static int s_currentSound;
#ifdef USE_MIDI_RENDER
	static s8 s_sampleBufferMidi[MIDI_BUFFER_SIZE * 2];
	static int s_currentSoundMidi;
#endif

	static AudioThreadCallback s_audioThreadCallback = nullptr;

	HOOKPROTO(SoundFunc, ULONG, struct AHIAudioCtrl *actrl, struct AHISoundMessage *smsg)
	{
#ifdef USE_MIDI_RENDER
		if (smsg->ahism_Channel == 0) { // SFX
#endif
			s8* buffer = &s_sampleBuffer[AUDIO_BUFFER_SIZE * s_currentSound];
			s_currentSound ^= 1;
			AHI_SetSound(0, s_currentSound, 0, 0, actrl, 0);
			//kprintf("%s %ld\n", __FUNCTION__, s_currentSound);
			s_audioThreadCallback((f32*)buffer, AUDIO_CALLBACK_BUFFER_SIZE, 0.0f);

#ifdef USE_MIDI_RENDER
		} else { // MIDI
			s8* buffer = &s_sampleBufferMidi[MIDI_BUFFER_SIZE * s_currentSoundMidi];
			s_currentSoundMidi ^= 1;
			AHI_SetSound(1, 2 | s_currentSoundMidi, 0, 0, actrl, 0);
			memset(buffer, 0, MIDI_BUFFER_SIZE);
			TFE_MidiPlayer::synthesizeMidi((f32*)buffer, MIDI_CALLBACK_BUFFER_SIZE, true);
		}
#endif
		return 0;
	}

	static inline void DeInterleave(s8 *dstLeft, s8 *dstRight, s8 *src, size_t count)
	{
		do
		{
			// this matches the AHI channel order
			*dstRight++ = *src++;
			*dstLeft++ = *src++;
		} while (--count);
	}

	static void TestTone(s8* dst, s32 count, s32 modifier)
	{
		float multiplier = modifier+1;
		for (s32 i = 0; i < count; i++)
		{
			*dst++ = sinf(((i) / 512.0f * multiplier*8.0f) * M_PI*2) * 127.0f;
			*dst++ = sinf(((i) / 512.0f * multiplier*4.0f) * M_PI*2) * 127.0f;
		}
	}

	INTERRUPTPROTO(AudioFunc, ULONG, struct Custom *c, APTR data)
	{
		UWORD intreqr = c->intreqr;
		s8 *bufferLeft, *bufferRight;

		if (intreqr & INTF_AUD0) { // SFX
			// switch to the finished buffer
			s_currentSound ^= 1;
			bufferLeft = s_chipBufferAudio + s_currentSound * AUDIO_BUFFER_SIZE;
			bufferRight = bufferLeft + AUDIO_CALLBACK_BUFFER_SIZE;
			c->aud[0].ac_ptr = (UWORD *)bufferLeft;
			c->aud[1].ac_ptr = (UWORD *)bufferRight;
			//c->intreq = INTF_AUD0;
			// fill the new buffer
			int bufferSound = s_currentSound/* ^ 1*/;
			s8* buffer = s_sampleBuffer;
			bufferLeft = s_chipBufferAudio + bufferSound * AUDIO_BUFFER_SIZE;
			bufferRight = bufferLeft + AUDIO_CALLBACK_BUFFER_SIZE;
			s_audioThreadCallback((f32*)buffer, AUDIO_CALLBACK_BUFFER_SIZE, 0.0f);
			DeInterleave(bufferLeft, bufferRight, buffer, AUDIO_CALLBACK_BUFFER_SIZE);
			c->intreq = INTF_AUD0;
		}
		if (intreqr & INTF_AUD2) { // MIDI
			// switch to the finished buffer
			s_currentSoundMidi ^= 1;
			bufferLeft = s_chipBufferMidi + s_currentSoundMidi * MIDI_BUFFER_SIZE;
			bufferRight = bufferLeft + MIDI_CALLBACK_BUFFER_SIZE;
			c->aud[2].ac_ptr = (UWORD *)bufferLeft;
			c->aud[3].ac_ptr = (UWORD *)bufferRight;
			// fill the new buffer
			int bufferSound = s_currentSoundMidi/* ^ 1*/;
			s8* buffer = s_sampleBufferMidi;
			bufferLeft = s_chipBufferMidi + bufferSound * MIDI_BUFFER_SIZE;
			bufferRight = bufferLeft + MIDI_CALLBACK_BUFFER_SIZE;
			memset(buffer, 0, MIDI_BUFFER_SIZE);
			TFE_MidiPlayer::synthesizeMidi((f32*)buffer, MIDI_CALLBACK_BUFFER_SIZE, true);
			DeInterleave(bufferLeft, bufferRight, buffer, MIDI_CALLBACK_BUFFER_SIZE);
			c->intreq = INTF_AUD2;
		}

		return 0;
	}

	static void AHI_UpdateSoundState(bool playing)
	{
		if (playing)
		{
			AHI_ControlAudio(actrl, AHIC_Play, TRUE, TAG_END);
		}
		else
		{
			AHI_ControlAudio(actrl, AHIC_Play, FALSE, TAG_END);
		}
	}

	static void Paula_UpdateSoundState(bool playing)
	{
		if (playing)
		{
			custom->intena = INTF_SETCLR|/*INTF_INTEN|*/intFlags;
			custom->dmacon = DMAF_SETCLR|dmaFlags;
		}
		else
		{
			custom->dmacon = dmaFlags;
			custom->intena = intFlags;
		}
	}

	static void UpdateSoundState()
	{
		bool playing = s_audioThreadCallback && !s_paused;
		if (actrl)
		{
			AHI_UpdateSoundState(playing);
		}
		else if (!audioDev)
		{
			Paula_UpdateSoundState(playing);
		}
	}

	static bool MidiCanRender()
	{
		MidiDevice* device = TFE_MidiPlayer::getMidiDevice();
		//kprintf("%s:%ld %lx\n", __FUNCTION__, __LINE__, device);
		return (device && device->canRender());
	}

	void Paula_Shutdown()
	{
		/*
		if (!oldFilter) {
			ciaa->ciapra &= ~CIAF_LED;
			oldFilter = CIAF_LED;
		}
		*/

		if (oldIntAud0) {
			SetIntVector(INTB_AUD0, oldIntAud0);
			oldIntAud0 = NULL;
		}

		if (oldIntAud2) {
			SetIntVector(INTB_AUD0, oldIntAud2);
			oldIntAud2 = NULL;
		}

		if (chipBuffer) {
			FreeVec(chipBuffer);
			chipBuffer = NULL;
			s_chipBufferAudio = NULL;
			s_chipBufferMidi = NULL;
		}

		if (!audioDev) {
			audioIO->ioa_Request.io_Command = CMD_RESET;
			audioIO->ioa_Request.io_Flags = 0;
			DoIO((struct IORequest *)audioIO);
			CloseDevice((struct IORequest *)audioIO);
			audioDev = -1;
		}

		if (audioIO) {
			DeleteIORequest((struct IORequest *)audioIO);
			audioIO = NULL;
		}

		if (audioMP) {
			DeleteMsgPort(audioMP);
			audioMP = NULL;
		}
	}

	static bool PaulaInit(s32 outputId)
	{
		// Paula: audioDevice=0
		if (outputId >= 0 && outputId != 0)
		{
			return false;
		}

		bool canRender = MidiCanRender();

		if ((audioMP = CreateMsgPort())) {
			if ((audioIO = (struct IOAudio *)CreateIORequest(audioMP, sizeof(struct IOAudio)))) {
				UBYTE whichannel[] = {0};
				whichannel[0] = canRender ? 15 : 3;
				audioIO->ioa_Request.io_Message.mn_Node.ln_Pri = 127; // no stealing
				audioIO->ioa_Request.io_Command = ADCMD_ALLOCATE;
				audioIO->ioa_Request.io_Flags = ADIOF_NOWAIT;
				audioIO->ioa_AllocKey = 0;
				audioIO->ioa_Data = whichannel;
				audioIO->ioa_Length = sizeof(whichannel);

				if (!(audioDev = OpenDevice((STRPTR)AUDIONAME, 0, (struct IORequest *)audioIO, 0))) {
					UWORD period;
					if (GfxBase->DisplayFlags & PAL) {
						period = 3546895 / AUDIO_FREQ;
					} else {
						period = 3579545 / AUDIO_FREQ;
					}

					ULONG byteSize = AUDIO_BUFFER_SIZE * 2;
					if (canRender) {
						byteSize += MIDI_BUFFER_SIZE * 2;
					}

					chipBuffer = (s8 *)AllocVec(byteSize, MEMF_CHIP|MEMF_CLEAR|MEMF_PUBLIC);
					if (chipBuffer) {

						dmaFlags = DMAF_AUD0|DMAF_AUD1;
						intFlags = INTF_AUD0;
						if (canRender)
						{
							dmaFlags |= DMAF_AUD2|DMAF_AUD3;
							intFlags |= INTF_AUD2;
						}

						AudioInt.is_Node.ln_Type = NT_INTERRUPT;
						AudioInt.is_Node.ln_Pri = 100;
						AudioInt.is_Node.ln_Name = (char *)"TFE Audio Interrupt";
						//AudioInt.is_Data = NULL;
						AudioInt.is_Code = (void (*)())AudioFunc;

						// disable the DMA and interrupts
						custom->dmacon = dmaFlags;
						custom->intena = intFlags;
						custom->intreq = intFlags;

						// game audio
						oldIntAud0 = SetIntVector(INTB_AUD0, &AudioInt);
						s_chipBufferAudio = chipBuffer;
						s_currentSound = 0;
						UWORD vol = s_ahiVolume >> 10;

						// left channel
						custom->aud[0].ac_len = AUDIO_CALLBACK_BUFFER_SIZE / sizeof(UWORD);
						custom->aud[0].ac_per = period;
						custom->aud[0].ac_vol = vol;
						custom->aud[0].ac_ptr = (UWORD *)s_chipBufferAudio;

						// right channel
						custom->aud[1].ac_len = AUDIO_CALLBACK_BUFFER_SIZE / sizeof(UWORD);
						custom->aud[1].ac_per = period;
						custom->aud[1].ac_vol = vol;
						custom->aud[1].ac_ptr = (UWORD *)(s_chipBufferAudio + AUDIO_CALLBACK_BUFFER_SIZE);

						if (canRender) {
							// MIDI
							oldIntAud2 = SetIntVector(INTB_AUD2, &AudioInt);
							s_chipBufferMidi = chipBuffer + 2 * AUDIO_BUFFER_SIZE;

							// left channel
							custom->aud[2].ac_len = MIDI_CALLBACK_BUFFER_SIZE / sizeof(UWORD);
							custom->aud[2].ac_per = period;
							custom->aud[2].ac_vol = 64; // TODO
							custom->aud[2].ac_ptr = (UWORD *)s_chipBufferMidi;

							// right channel
							custom->aud[3].ac_len = MIDI_CALLBACK_BUFFER_SIZE / sizeof(UWORD);
							custom->aud[3].ac_per = period;
							custom->aud[3].ac_vol = 64; // TODO
							custom->aud[3].ac_ptr = (UWORD *)(s_chipBufferMidi + MIDI_CALLBACK_BUFFER_SIZE);
						}

						// turn off the low-pass filter
						/*
						oldFilter = ciaa->ciapra & CIAF_LED;
						ciaa->ciapra |= CIAF_LED;
						*/

						TFE_System::logWrite(LOG_MSG, "Audio", "Audio initialized using Paula DMA %04hx Int %04hx.", dmaFlags, intFlags);

						return true;
					}
				}
			}
		}

		Paula_Shutdown();

		return false;
	}

	void AHI_Shutdown()
	{
		if (actrl)
		{
			AHI_ControlAudio(actrl, AHIC_Play, FALSE, TAG_END);
			struct AHIEffMasterVolume vol = {
				AHIET_MASTERVOLUME | AHIET_CANCEL,
				0x10000
			};
			AHI_SetEffect(&vol, actrl);
			AHI_FreeAudio(actrl);
			actrl = NULL;
		}

		if (AHIBase)
		{
			CloseDevice((struct IORequest *)AHIio);
			AHIBase = NULL;
		}

		if (AHIio)
		{
			DeleteIORequest((struct IORequest *)AHIio);
			AHIio = NULL;
		}

		if (AHImp)
		{
			DeleteMsgPort(AHImp);
			AHImp = NULL;
		}
	}

	bool AHI_Init(s32 outputId)
	{
		char namebuf[64];

		// AHI: audioDevice=1
		if (outputId >= 0 && outputId != 1)
		{
			return false;
		}

		AHImp = CreateMsgPort();
		if (AHImp)
		{
			AHIio = (struct AHIRequest *)CreateIORequest(AHImp, sizeof(struct AHIRequest));
			if (AHIio)
			{
				if (!OpenDevice(AHINAME, AHI_NO_UNIT, (struct IORequest *)AHIio, 0))
				{
					AHIBase = (struct Library *)AHIio->ahir_Std.io_Device;

					AHI_GetAudioAttrs(AHI_DEFAULT_ID, NULL,
						AHIDB_BufferLen, sizeof(namebuf),
						AHIDB_Driver, (IPTR)namebuf,
						TAG_END);
					//kprintf("%s:%ld AHIDB_Driver %s\n", __FUNCTION__, __LINE__, namebuf);

					// don't use Paula modes when audioDevice is set to auto (-1)
					if (outputId < 0 && !strcmp(namebuf, "paula"))
					{
						AHI_Shutdown();
						return false;
					}

					SoundHook.h_Entry = (ULONG (*)())&SoundFunc;
					SoundHook.h_SubEntry = NULL;
					SoundHook.h_Data = NULL;

#ifdef USE_MIDI_RENDER
					ULONG channels = MidiCanRender() ? 2 : 1;
					//kprintf("%s:%ld %lx\n", __FUNCTION__, __LINE__, channels);
#endif

					actrl = AHI_AllocAudio(AHIA_AudioID, AHI_DEFAULT_ID,
						AHIA_MixFreq, AUDIO_FREQ,
#ifdef USE_MIDI_RENDER
						AHIA_Channels, channels,
						AHIA_Sounds, channels*2,
#else
						AHIA_Channels, 1,
						AHIA_Sounds, 2,
#endif
						AHIA_SoundFunc, (IPTR)&SoundHook,
						TAG_END);

					if (actrl)
					{
						ULONG r = channels*2;
						struct AHIEffMasterVolume vol = {
							AHIET_MASTERVOLUME,
							r * 0x10000
						};
						AHI_SetEffect(&vol, actrl);

						//memset(s_sampleBuffer, 0, sizeof(s_sampleBuffer));
						struct AHISampleInfo sample[4];
						sample[0].ahisi_Type = AHIST_S8S;
						sample[0].ahisi_Length = AUDIO_CALLBACK_BUFFER_SIZE;
						sample[1] = sample[0];
						sample[0].ahisi_Address = &s_sampleBuffer[0];
						sample[1].ahisi_Address = &s_sampleBuffer[AUDIO_BUFFER_SIZE];

#ifdef USE_MIDI_RENDER
						if (channels >= 2)
						{
							sample[2].ahisi_Type = AHIST_S8S;
							sample[2].ahisi_Length = MIDI_CALLBACK_BUFFER_SIZE;
							sample[3] = sample[2];
							sample[2].ahisi_Address = &s_sampleBufferMidi[0];
							sample[3].ahisi_Address = &s_sampleBufferMidi[MIDI_BUFFER_SIZE];
							if (!AHI_LoadSound(2, AHIST_DYNAMICSAMPLE, &sample[2], actrl) &&
								!AHI_LoadSound(3, AHIST_DYNAMICSAMPLE, &sample[3], actrl))
							{
								s_currentSoundMidi = 0;

								Fixed s_ahiVolumeMidi = 0x10000; // TODO
								AHI_Play(actrl,
									AHIP_BeginChannel, 1,
									AHIP_Freq, AUDIO_FREQ,
									AHIP_Vol, s_ahiVolumeMidi,
									AHIP_Pan, 0x8000,
									AHIP_Sound, 2,
									AHIP_EndChannel, 0,
									TAG_END);
							}
						}
#endif

						if (!AHI_LoadSound(0, AHIST_DYNAMICSAMPLE, &sample[0], actrl) &&
							!AHI_LoadSound(1, AHIST_DYNAMICSAMPLE, &sample[1], actrl))
						{
							s_currentSound = 0;

							AHI_Play(actrl,
								AHIP_BeginChannel, 0,
								AHIP_Freq, AUDIO_FREQ,
								AHIP_Vol, s_ahiVolume,
								AHIP_Pan, 0x8000,
								AHIP_Sound, 0,
								AHIP_EndChannel, 0,
								TAG_END);

							AHI_GetAudioAttrs(AHI_INVALID_ID, actrl,
								AHIDB_BufferLen, sizeof(namebuf),
								AHIDB_Name, (IPTR)namebuf,
								TAG_END);

							ULONG mixfreq;
							AHI_ControlAudio(actrl,
								AHIC_MixFreq_Query, (IPTR)&mixfreq,
								TAG_END);

							TFE_System::logWrite(LOG_MSG, "Audio", "Audio initialized using AHI mode '%s' at %d Hz.", namebuf, mixfreq);

							return true;
						}
					}
				}
			}
		}

		AHI_Shutdown();

		return false;
	}

	bool init(bool useNullDevice/*=false*/, s32 outputId/*=-1*/)
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_AudioSystem::init");

		TFE_Settings_Sound* soundSettings = TFE_Settings::getSoundSettings();
		setVolume(soundSettings->soundFxVolume);

		if (AHI_Init(outputId))
		{
			s_nullDevice = false;
			return true;
		}

		if (PaulaInit(outputId))
		{
			s_nullDevice = false;
			return true;
		}

		TFE_System::logWrite(LOG_ERROR, "Audio", "Cannot start audio device.");
		s_nullDevice = true;
		return false;
	}

	void shutdown()
	{
		TFE_System::logWrite(LOG_MSG, "Audio", "Shutdown");
		if (s_nullDevice) { return; }

		stopAllSounds();

		AHI_Shutdown();
		Paula_Shutdown();
	}

	void stopAllSounds() {}
	void selectDevice(s32 id) {}
	void setUpsampleFilter(AudioUpsampleFilter filter) {}
	AudioUpsampleFilter getUpsampleFilter() { return AUF_DEFAULT; }

	void setVolume(f32 volume)
	{
		s_soundFxVolume = volume;
		s_ahiVolume = volume * 65536.0f;
		//TFE_System::logWrite(LOG_MSG, "Audio", "Volume %f %08x", volume, s_ahiVolume);
		if (actrl)
		{
			AHI_SetVol(0, s_ahiVolume, 0x8000, actrl, AHISF_IMM);
		}
		else if (!audioDev)
		{
			UWORD vol = s_ahiVolume >> 10;
			custom->aud[0].ac_vol = vol;
			custom->aud[1].ac_vol = vol;
		}
	}

	f32 getVolume()
	{
		return s_soundFxVolume;
	}

	void pause()
	{
		s_paused = true;
		UpdateSoundState();
	}

	void resume()
	{
		s_paused = false;
		UpdateSoundState();
	}

	// Really the buffered audio will continue to process so time advances properly.
	// But for 'BUFFERED_SILENT_FRAME_COUNT' audio will be silent.
	// This allows the buffered data to be consumed without audio hitches.
	void bufferedAudioClear()
	{
		s_silentAudioFrames = BUFFERED_SILENT_FRAME_COUNT;
	}

	void setAudioThreadCallback(AudioThreadCallback callback)
	{
		if (s_nullDevice) { return; }

		s_audioThreadCallback = callback;
		UpdateSoundState();
	}

	const OutputDeviceInfo* getOutputDeviceList(s32& count, s32& curOutput)
	{
		count = 0;
		curOutput = 0;
		return nullptr;
	}

	void lock()
	{
		//Forbid();
		//kprintf("lock   %lx\n", FindTask(NULL));
	}

	void unlock()
	{
		//kprintf("unlock %lx\n", FindTask(NULL));
		//Permit();
	}

	// One shot, play and forget. Only do this if the client needs no control until stopAllSounds() is called.
	// Note that looping one shots are valid.
	bool playOneShot(SoundType type, f32 volume, const SoundBuffer* buffer, bool looping, SoundFinishedCallback finishedCallback, void* cbUserData, s32 cbArg) { return false; }

	// Sound source that the client holds onto.
	SoundSource* createSoundSource(SoundType type, f32 volume, const SoundBuffer* buffer, SoundFinishedCallback callback, void* userData) { return nullptr; }
	s32 getSourceSlot(SoundSource* source) { return -1; }
	SoundSource* getSourceFromSlot(s32 slot) { return nullptr; }
	void playSource(SoundSource* source, bool looping) {}
	void stopSource(SoundSource* source) { return; }
	void freeSource(SoundSource* source) { return; }
	void setSourceVolume(SoundSource* source, f32 volume) { return; }
	void setSourceBuffer(SoundSource* source, const SoundBuffer* buffer) { return; }
	bool isSourcePlaying(SoundSource* source) { return false; }
	f32 getSourceVolume(SoundSource* source) { return 0.0f; }
}
