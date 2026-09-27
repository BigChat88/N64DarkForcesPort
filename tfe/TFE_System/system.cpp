#include <TFE_System/system.h>
#include <TFE_System/profiler.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Settings/settings.h>
#if defined(__N64__)
#include <TFE_Jedi/Math/fixedPoint.h>
#include <n64sys.h>
#include <string.h>

#define FLOAT_TIMER

static u64 SDL_GetPerformanceCounter()
{
	return get_ticks();
}

static f64 SDL_GetPerformanceCounterFloat()
{
	return (f64)get_ticks();
}

static u64 SDL_GetPerformanceFrequency()
{
	return TICKS_PER_SECOND;
}

#elif defined(__AMIGA__)
#include <TFE_Jedi/Math/fixedPoint.h>
#define GRAPHICS_DISPLAYINFO_H
#include <proto/dos.h>
#include <proto/timer.h>
#include <proto/lowlevel.h>
//#define SDL_GetPerformanceCounter() (0)
//#define SDL_GetPerformanceFrequency() (1)

#define FLOAT_TIMER

static f64 SDL_GetPerformanceCounterFloat()
{
	struct Device *TimerBase = DOSBase->dl_TimeReq->tr_node.io_Device;
	struct EClockVal ev;
	ReadEClock(&ev);
	return ((f64)ev.ev_hi * 4294967296) + ((f64)ev.ev_lo);
}

static u64 SDL_GetPerformanceCounter()
{
	struct Device *TimerBase = DOSBase->dl_TimeReq->tr_node.io_Device;
	struct EClockVal ev;
	ReadEClock(&ev);
	return ((u64)ev.ev_hi << 32) | ((u64)ev.ev_lo);
}

static u64 SDL_GetPerformanceFrequency()
{
	struct Device *TimerBase = DOSBase->dl_TimeReq->tr_node.io_Device;
	struct EClockVal ev;
	return ReadEClock(&ev);
}

#else
#include <SDL.h>
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h> 
#include <algorithm>

#ifdef _WIN32
// Following includes for Windows LinkCallback
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "Shellapi.h"
#include <synchapi.h>
#undef min
#undef max
#elif defined __linux__ || defined __AMIGA__
#include <sys/types.h>
#include <unistd.h>
#endif

namespace TFE_System
{
	f64 c_gameTimeScale = 1.02;	// Adjust game time to match DosBox.

#ifdef __AMIGA__
	static f64 s_time;
	static f64 s_startTime;
	static f64 s_lastSyncCheck;
	static f64 s_syncCheckDelay;
#else
	static u64 s_time;
	static u64 s_startTime;
	static u64 s_lastSyncCheck;
	static u64 s_syncCheckDelay;
#endif
	static f64 s_freq;
	static f64 s_refreshRate;
	
	static f64 s_dt = 1.0 / 60.0;		// This is just to handle the first frame, so any reasonable value will work.
	static f64 s_dtRaw = 1.0 / 60.0;
#ifdef __AMIGA__0
	static const f64 c_maxDt = 1.0 / 10.0;	// more slack on Amiga
#else
	static const f64 c_maxDt = 0.05;	// 20 fps
#endif

	static bool s_synced = false;
	static bool s_resetStartTime = false;
	static bool s_quitMessagePosted = false;
	static bool s_systemUiRequestPosted = false;

	static s32 s_missedFrameCount = 0;

	static char s_versionString[64];

	void init(f32 refreshRate, bool synced, const char* versionString)
	{
		TFE_System::logWrite(LOG_MSG, "Startup", "TFE_System::init");
		s_time = SDL_GetPerformanceCounter();
		s_lastSyncCheck = s_time;
		s_startTime = s_time;

		const u64 timerFreq = SDL_GetPerformanceFrequency();
		s_syncCheckDelay = timerFreq * 10;	// 10 seconds.
		s_freq = 1.0 / (f64)timerFreq;

		s_refreshRate = f64(refreshRate);
		s_synced = synced;

		TFE_COUNTER(s_missedFrameCount, "Miss-Predicted Vysnc Intervals");
		strcpy(s_versionString, versionString);
	}

	void shutdown()
	{
	}

	void setVsync(bool sync)
	{
		s_synced = sync;
		TFE_Settings::getGraphicsSettings()->vsync = sync;
		TFE_RenderBackend::enableVsync(sync);
		s_lastSyncCheck = SDL_GetPerformanceCounter();
	}

	bool getVSync()
	{
		return s_synced;
	}

	const char* getVersionString()
	{
		return s_versionString;
	}

	void resetStartTime()
	{
		s_resetStartTime = true;
	}

	f64 updateThreadLocal(u64* localTime)
	{
#ifdef __AMIGA__
		f64* localTimeFloat = (f64*)localTime;
		const f64 curTime = SDL_GetPerformanceCounterFloat();
		//const f64 uDt = (*localTimeFloat) > 0.0 ? curTime - (*localTimeFloat) : 0.0;
		const f64 uDt = (*localTime) != 0u ? curTime - (*localTimeFloat) : 0.0;

		const f64 dt = uDt * s_freq;
		*localTimeFloat = curTime;
#else
		const u64 curTime = SDL_GetPerformanceCounter();
		const u64 uDt = (*localTime) > 0u ? curTime - (*localTime) : 0u;

		const f64 dt = f64(uDt) * s_freq;
		*localTime = curTime;
#endif

		return dt;
	}

	void update()
	{
		// This assumes that SDL_GetPerformanceCounter() is monotonic.
		// However if errors do occur, the dt clamp later should limit the side effects.
#ifdef __AMIGA__
		const f64 curTime = SDL_GetPerformanceCounterFloat();
		const f64 uDt =  (curTime > s_time) ? (curTime - s_time) : 1.0;
#else
		const u64 curTime = SDL_GetPerformanceCounter();
		const u64 uDt = (curTime > s_time) ? (curTime - s_time) : 1;	// Make sure time is monotonic.
#endif
		s_time = curTime;
		if (s_resetStartTime)
		{
			s_startTime = s_time;
			s_resetStartTime = false;
		}

		// Delta time since the previous frame.
		f64 dt = f64(uDt) * s_freq;

#ifndef __AMIGA__
		// If vsync is enabled, then round up to the nearest vsync interval as our delta time.
		// Ideally, if we are hitting the correct framerate, this should always be 
		// 1 vsync interval.
		if (s_synced && s_refreshRate > 0.0f && s_refreshRate < 120.0f)	// If the refresh rate is too high, rounding becomes unreliable.
		{
			const f64 intervals = std::max(1.0, floor(dt * s_refreshRate + 0.1));
			f64 newDt = intervals / s_refreshRate;

			f64 roundDiff = fabs(newDt - dt);
			if (roundDiff > 0.25 / s_refreshRate)
			{
				// Something went wrong, so use the original delta time and check if the refresh rate has changed.
				// Also verify that vsync is still set.
				// Note that checking vsync state and refresh are costly, so don't do it constantly.
				if (curTime - s_lastSyncCheck > s_syncCheckDelay)
				{
					f64 newRate = (f64)TFE_RenderBackend::getDisplayRefreshRate();
					s_refreshRate = newRate ? newRate : s_refreshRate;
					s_synced = TFE_RenderBackend::getVsyncEnabled();
					s_lastSyncCheck = curTime;
				}
				s_missedFrameCount++;
			}
			else
			{
				dt = newDt;
			}
		}
#endif

		s_dtRaw = dt;
#ifndef __AMIGA__0
		// Next make sure that if the current fps is too low, that the game just slows down.
		// This avoids the "spiral of death" when using fixed time steps and avoids issues
		// during loading spikes.
		// This caps the low end framerate before slowdown to 20 fps.
		s_dt = std::min(dt, c_maxDt);
#endif
	}

	// Timing
	// Return the delta time.
	f64 getDeltaTime()
	{
		return s_dt;
	}

	f64 getDeltaTimeRaw()
	{
		return s_dtRaw;
	}

	// Get time since "start time"
	f64 getTime()
	{
#ifdef __AMIGA__
		const f64 uDt = s_time - s_startTime;
#else
		const u64 uDt = s_time - s_startTime;
#endif
		return f64(uDt) * s_freq;
	}
	
	u64 getCurrentTimeInTicks()
	{
		return SDL_GetPerformanceCounter() - s_startTime;
	}

	f64 convertFromTicksToSeconds(u64 ticks)
	{
		return f64(ticks) * s_freq;
	}

	f64 microsecondsToSeconds(f64 mu)
	{
		return mu / 1000000.0;
	}

	void getDateTimeString(char* output)
	{
		time_t _tm = time(NULL);
		struct tm* curtime = localtime(&_tm);
		strcpy(output, asctime(curtime));
	}

	bool osShellExecute(const char* pathToExe, const char* exeDir, const char* param, bool waitForCompletion)
	{
#ifdef _WIN32
		// Prepare shellExecutInfo
		SHELLEXECUTEINFO ShExecInfo = { 0 };
		ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
		ShExecInfo.fMask = waitForCompletion ? SEE_MASK_NOCLOSEPROCESS : 0;
		ShExecInfo.hwnd = NULL;
		ShExecInfo.lpVerb = NULL;
		ShExecInfo.lpFile = pathToExe;
		ShExecInfo.lpParameters = param;
		ShExecInfo.lpDirectory = exeDir;
		ShExecInfo.nShow = SW_SHOW;
		ShExecInfo.hInstApp = NULL;

		// Execute the file with the parameters
		if (!ShellExecuteEx(&ShExecInfo))
		{
			return false;
		}

		if (waitForCompletion)
		{
			WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
			CloseHandle(ShExecInfo.hProcess);
		}
		return true;
#endif
		return false;
	}

#ifdef _WIN32
	void sleep(u32 sleepDeltaMS)
	{
		Sleep(sleepDeltaMS);
	}
#elif defined __linux__
	void sleep(u32 sleepDeltaMS)
	{
		struct timespec ts = {0, 0};
		while (sleepDeltaMS >= 1000) {
			ts.tv_sec += 1;
			sleepDeltaMS -= 1000;
		}
		ts.tv_nsec = sleepDeltaMS * 1000000;
		nanosleep(&ts, NULL);
	}
#elif defined __N64__
	void sleep(u32 sleepDeltaMS)
	{
		wait_ms(sleepDeltaMS);
	}
#elif defined __AMIGA__
	void sleep(u32 sleepDeltaMS)
	{
		usleep(sleepDeltaMS*1000);
	}
#endif

	void postQuitMessage()
	{
		s_quitMessagePosted = true;
	}
		
	void postSystemUiRequest()
	{
		s_systemUiRequestPosted = true;
	}



	bool quitMessagePosted()
	{
		return s_quitMessagePosted;
	}

	bool systemUiRequestPosted()
	{
		bool systemUiPostReq = s_systemUiRequestPosted;
		s_systemUiRequestPosted = false;
		return systemUiPostReq;
	}
}
