// TFE only uses a thread for the MIDI player. Music is not implemented on the
// N64 yet, so the thread is never started; this will move to kthread (or to
// libdragon's MIDI player) once music is added.
#include <TFE_System/Threads/thread.h>

class ThreadN64 : public Thread
{
public:
	ThreadN64(const char* name, ThreadFunc func, void* userData) : Thread(name, func, userData) {}
	virtual ~ThreadN64() {}

	virtual bool run() { return true; }
	virtual void pause() {}
	virtual void resume() {}
	virtual void waitOnExit() {}
};

Thread* Thread::create(const char* name, ThreadFunc func, void* userData)
{
	return new ThreadN64(name, func, userData);
}
