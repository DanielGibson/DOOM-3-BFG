/*
===========================================================================

Doom 3 BFG Edition GPL Source Code
Copyright (C) 1993-2012 id Software LLC, a ZeniMax Media company.
Copyright (C) 2012 Robert Beckebans
Copyright (C) 2013 Daniel Gibson

This file is part of the Doom 3 BFG Edition GPL Source Code ("Doom 3 BFG Edition Source Code").

Doom 3 BFG Edition Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 BFG Edition Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 BFG Edition Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 BFG Edition Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 BFG Edition Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
#if 1
#pragma hdrstop
#include "../../precompiled.h"
#include <SDL.h>
//#include <SDL_mutex.h>


// SDL_SetThreadPriority() is for the "current thread" - how useless...
// so create a wrapper xthread_t function that gets the priority, the orig function
// and the orig params as an argument, sets the priority and calls the original
// function (with its parameters) afterwards

struct fnwrapper
{
	fnwrapper( xthread_t& f, void* p, SDL_ThreadPriority prio ) :
		function( f ), parms( p ), prio( prio ) {}
		
	xthread_t function;
	void* parms;
	SDL_ThreadPriority prio;
};

int wrapperfn( void* parms )
{
	fnwrapper* fw = ( fnwrapper* )parms;
	SDL_SetThreadPriority( fw->prio );
	unsigned int ret = fw->function( fw->parms );
	delete fw;
	return ret;
}

/*
========================
Sys_Createthread
========================
*/
uintptr_t Sys_CreateThread( xthread_t function, void* parms, xthreadPriority priority, const char* name,
							core_t core, int stackSize, bool suspended )
{

	assert( suspended == false ); // not used with true so far, TODO: remove that option
	
	SDL_ThreadPriority sdl_prio;
	switch( priority )
	{
		case THREAD_LOWEST:
		case THREAD_BELOW_NORMAL:
			sdl_prio = SDL_THREAD_PRIORITY_LOW;
			break;
		case THREAD_NORMAL:
			sdl_prio = SDL_THREAD_PRIORITY_NORMAL;
			break;
		case THREAD_ABOVE_NORMAL:
		case THREAD_HIGHEST:
			sdl_prio = SDL_THREAD_PRIORITY_HIGH;
			break;
	}
	
	fnwrapper* w = new fnwrapper( function, parms, sdl_prio );
	
	SDL_Thread* t = SDL_CreateThread( wrapperfn, name, w );
	assert( t ); // FIXME: proper checking
	
	return uintptr_t( t );
}


/*
========================
Sys_GetCurrentThreadID
========================
*/
uintptr_t Sys_GetCurrentThreadID()
{
	// like on windows, this is != the thread handle
	// also, it isn't used
	return SDL_ThreadID();
}

/*
========================
Sys_DestroyThread
========================
*/
void Sys_DestroyThread( uintptr_t threadHandle )
{
	SDL_Thread* t = ( SDL_Thread* )threadHandle;
	SDL_WaitThread( t, NULL );
}

/*
========================
Sys_Yield
========================
*/
void Sys_Yield()
{
	// FIXME: this is POSIX-specific, SDL has no such function - if necessary use a platform-specific implementation here!
	pthread_yield();
}

/*
================================================================================================

	Signal

================================================================================================
*/

/*
========================
Sys_SignalCreate
========================
*/
void Sys_SignalCreate( signalHandle_t& handle, bool manualReset )
{
	// handle = CreateEvent( NULL, manualReset, FALSE, NULL );

	handle.manualReset = manualReset;
	// if this is true, the signal is only set to nonsignaled when Clear() is called,
	// else it's "auto-reset" and the state is set to !signaled after a single waiting
	// thread has been released

	// the inital state is always "not signaled"
	handle.signaled = false;
	handle.waiting = 0;
	handle.cond = SDL_CreateCond();
	handle.mutex = SDL_CreateMutex();
}

/*
========================
Sys_SignalDestroy
========================
*/
void Sys_SignalDestroy( signalHandle_t& handle )
{
	// CloseHandle( handle );
	handle.signaled = false;
	handle.waiting = 0;
	SDL_DestroyMutex(handle.mutex);
	SDL_DestroyCond(handle.cond);
}

/*
========================
Sys_SignalRaise
========================
*/
void Sys_SignalRaise( signalHandle_t& handle )
{
	// SetEvent( handle );
	// thanks for establishing P() and V(), the worst function names ever, Dijkstra.
	SDL_mutexP( handle.mutex );

	if( handle.manualReset )
	{
		// signaled until reset
		handle.signaled = true;
		SDL_CondBroadcast( handle.cond );
	}
	else
	{
		// automode: signaled until first thread is released
		if( handle.waiting > 0 )
		{
			// there are waiting threads => release one
			SDL_CondSignal( handle.cond );
		}
		else
		{
			// no waiting threads, save signal
			handle.signaled = true;
			// while the MSDN documentation is a bit unspecific about what happens
			// when SetEvent() is called n times without a wait inbetween
			// (will only one wait be successful afterwards or n waits?)
			// it seems like the signaled state is a flag, not a counter.
			// http://stackoverflow.com/a/13703585 claims the same.
		}
	}

	SDL_mutexV( handle.mutex );
}

/*
========================
Sys_SignalClear
========================
*/
void Sys_SignalClear( signalHandle_t& handle )
{
	// events are created as auto-reset so this should never be needed
	//ResetEvent( handle );
	SDL_mutexP( handle.mutex );

	// TODO: probably signaled could be atomically changed?
	handle.signaled = false;

	SDL_mutexV( handle.mutex );
}

/*
========================
Sys_SignalWait
========================
*/
bool Sys_SignalWait( signalHandle_t& handle, int timeout )
{
	//DWORD result = WaitForSingleObject( handle, timeout == idSysSignal::WAIT_INFINITE ? INFINITE : timeout );
	//assert( result == WAIT_OBJECT_0 || ( timeout != idSysSignal::WAIT_INFINITE && result == WAIT_TIMEOUT ) );
	//return ( result == WAIT_OBJECT_0 );

	int status;
	SDL_mutexP( handle.mutex );

	if( handle.signaled ) // there is a signal that hasn't been used yet
	{
		if( ! handle.manualReset ) // for auto-mode only one thread may be released - this one.
			handle.signaled = false;

		status = 0; // success!
	}
	else     // we'll have to wait for a signal
	{
		++handle.waiting;
		if( timeout == idSysSignal::WAIT_INFINITE )
		{
			status = SDL_CondWait( handle.cond, handle.mutex );
		}
		else
		{
			status = SDL_CondWaitTimeout( handle.cond, handle.mutex, timeout );
		}
		--handle.waiting;
	}

	SDL_mutexV( handle.mutex );

	assert( status == 0 || ( timeout != idSysSignal::WAIT_INFINITE && status == SDL_MUTEX_TIMEDOUT ) );

	return ( status == 0 ); // FIXME: 0 == success, SDL_MUTEX_TIMEDOUT == timeout, < 0 == error (=> SDL_GetError)

}

/*
================================================================================================

	Mutex

================================================================================================
*/

#if 0 // for some reason, using semaphores doesn't work.
/*
========================
Sys_MutexCreate
========================
*/
void Sys_MutexCreate( mutexHandle_t& handle )
{
	// mutexes are semaphores with a max val of 1
	// use SDL_sem instead of SDL_mutex to get SDL_SemTryWait()
	handle = SDL_CreateSemaphore( 1 );
	assert( handle ); // FIXME
}

/*
========================
Sys_MutexDestroy
========================
*/
void Sys_MutexDestroy( mutexHandle_t& handle )
{
	SDL_DestroySemaphore( handle );
}

/*
========================
Sys_MutexLock
========================
*/
bool Sys_MutexLock( mutexHandle_t& handle, bool blocking )
{
	int status;
	status = SDL_SemTryWait( handle );
	if(status != 0) {
		if(!blocking)
			return false;
		SDL_SemWait(handle);
	}
	return true;

	/*if( blocking )
	{
		status = SDL_SemWait( handle );
	}
	else
	{
		status = SDL_SemTryWait( handle );
	}
	// FIXME: if status < 0 print SDL_GetError()
	return status == 0;
	*/
}

/*
========================
Sys_MutexUnlock
========================
*/
void Sys_MutexUnlock( mutexHandle_t& handle )
{
	SDL_SemPost( handle );
}
#else

/*
========================
Sys_MutexCreate
========================
*/
void Sys_MutexCreate( mutexHandle_t& handle )
{
	// mutexes are semaphores with a max val of 1
	// use SDL_sem instead of SDL_mutex to get SDL_SemTryWait()
	handle = SDL_CreateMutex();
	assert( handle ); // FIXME
}

/*
========================
Sys_MutexDestroy
========================
*/
void Sys_MutexDestroy( mutexHandle_t& handle )
{
	SDL_DestroyMutex( handle );
}

/*
========================
Sys_MutexLock
========================
*/
bool Sys_MutexLock( mutexHandle_t& handle, bool blocking )
{
	//int status;
	if( SDL_TryLockMutex(handle) != 0 )
	{
		if( ! blocking )
			return false;

		SDL_LockMutex(handle);
	}
	return true;
}

/*
========================
Sys_MutexUnlock
========================
*/
void Sys_MutexUnlock( mutexHandle_t& handle )
{
	SDL_UnlockMutex( handle );
}

#endif

/*
================================================================================================

	Interlocked Integer

================================================================================================
*/

/*
========================
Sys_InterlockedIncrement
========================
*/
interlockedInt_t Sys_InterlockedIncrement( interlockedInt_t& value )
{
	// return InterlockedIncrementAcquire( & value );
	return __sync_add_and_fetch( &value, 1 );
}

/*
========================
Sys_InterlockedDecrement
========================
*/
interlockedInt_t Sys_InterlockedDecrement( interlockedInt_t& value )
{
	// return InterlockedDecrementRelease( & value );
	return __sync_sub_and_fetch( &value, 1 );
}

/*
========================
Sys_InterlockedAdd
========================
*/
interlockedInt_t Sys_InterlockedAdd( interlockedInt_t& value, interlockedInt_t i )
{
	//return InterlockedExchangeAdd( & value, i ) + i;
	return __sync_add_and_fetch( &value, i );
}

/*
========================
Sys_InterlockedSub
========================
*/
interlockedInt_t Sys_InterlockedSub( interlockedInt_t& value, interlockedInt_t i )
{
	//return InterlockedExchangeAdd( & value, - i ) - i;
	return __sync_sub_and_fetch( &value, i );
}

/*
========================
Sys_InterlockedExchange
========================
*/
interlockedInt_t Sys_InterlockedExchange( interlockedInt_t& value, interlockedInt_t exchange )
{
	//return InterlockedExchange( & value, exchange );
	
	// source: http://gcc.gnu.org/onlinedocs/gcc-4.1.1/gcc/Atomic-Builtins.html
	// These builtins perform an atomic compare and swap. That is, if the current value of *ptr is oldval, then write newval into *ptr.
	return __sync_val_compare_and_swap( &value, value, exchange );
}

/*
========================
Sys_InterlockedCompareExchange
========================
*/
interlockedInt_t Sys_InterlockedCompareExchange( interlockedInt_t& value, interlockedInt_t comparand, interlockedInt_t exchange )
{
	//return InterlockedCompareExchange( & value, exchange, comparand );
	return __sync_val_compare_and_swap( &value, comparand, exchange );
}

/*
================================================================================================

	Interlocked Pointer

================================================================================================
*/

/*
========================
Sys_InterlockedExchangePointer
========================
*/
void* Sys_InterlockedExchangePointer( void*& ptr, void* exchange )
{
	//return InterlockedExchangePointer( & ptr, exchange );
	return __sync_val_compare_and_swap( &ptr, ptr, exchange );
}

/*
========================
Sys_InterlockedCompareExchangePointer
========================
*/
void* Sys_InterlockedCompareExchangePointer( void*& ptr, void* comparand, void* exchange )
{
	//return InterlockedCompareExchangePointer( & ptr, exchange, comparand );
	return __sync_val_compare_and_swap( &ptr, comparand, exchange );
}

#endif // 0
