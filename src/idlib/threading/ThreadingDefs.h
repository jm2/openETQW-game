// Copyright (C) 2007 Id Software, Inc.
//

#ifndef __THREADINGDEFS_H__
#define __THREADINGDEFS_H__

#if defined( _WIN32 )
	struct threadHandle_t {
		HANDLE			handle;
		unsigned int	id;
	};

	typedef unsigned int (__stdcall *threadProc_t)( void* );

	#define signalHandle_t	HANDLE

	#define lockHandle_t	CRITICAL_SECTION
#else
	#include <pthread.h>
	typedef void* (*threadProc_t)( void* );
	// pthreads have no suspended create: sdSysThread::Create records the creation
	// parameters here (per thread) and sdSysThread::Start creates the OS thread
	struct threadHandle_t {
		pthread_t		handle;
		threadProc_t	proc;
		void*			parms;
		int				priority;		// threadPriority_e
		unsigned int	stackSize;		// 0 = default
	};
	struct signalHandle_t {
		pthread_mutex_t	mutex;
		pthread_cond_t	cond;
		bool			signaled;
		bool			waiting;
	};
	#define lockHandle_t	pthread_mutex_t
#endif

#endif /* !__THREADINGDEFS_H__ */
