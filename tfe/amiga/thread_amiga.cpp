#include <TFE_System/system.h>
#include <TFE_System/Threads/thread.h>
//#include <pthread.h>
//#include "threadLinux.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <clib/debug_protos.h>

#include <SDI_compiler.h>

class ThreadAmiga : public Thread
{
public:
	ThreadAmiga(const char* name, ThreadFunc func, void* userData);
	virtual ~ThreadAmiga();

	virtual bool run();
	virtual void pause();
	virtual void resume();
	virtual void waitOnExit(void);
	
	ThreadFunc getFunc() { return m_func; }
	void* getUserData() { return m_userData; }
	struct Task *getParent() { return m_parent; }

protected:
	//void threadFunc(void);

	struct Task *m_task;
	struct Task *m_parent;
};

ThreadAmiga::ThreadAmiga(const char* name, ThreadFunc func, void* userData) : Thread(name, func, userData)
{
}

ThreadAmiga::~ThreadAmiga()
{
}

//void ThreadAmiga::threadFunc(void)
void threadFunc(void)
{
	struct Process *me = (struct Process *)FindTask(NULL);
	ThreadAmiga* thread = (ThreadAmiga *)me->pr_ExitData;
	me->pr_ExitData = 0;

	thread->getFunc()(thread->getUserData());

	Forbid();
	Signal(thread->getParent(), SIGBREAKF_CTRL_E);
}

bool ThreadAmiga::run(void)
{
	m_parent = FindTask(NULL);
	SetSignal(0, SIGBREAKF_CTRL_E);
	m_task = (struct Task *)CreateNewProcTags(
		NP_Name, (IPTR)m_name,
		NP_Priority, 21,
		NP_Entry, (IPTR)threadFunc,
		NP_ExitData, (IPTR)this,
		TAG_END);
	return (m_task != nullptr);
}

void ThreadAmiga::pause(void)
{
}

void ThreadAmiga::resume(void)
{
}

void ThreadAmiga::waitOnExit(void)
{
	if (m_task)
	{
		Wait(SIGBREAKF_CTRL_E);
	}
}

//factory
Thread* Thread::create(const char* name, ThreadFunc func, void* userData)
{
	return new ThreadAmiga(name, func, userData);
}
