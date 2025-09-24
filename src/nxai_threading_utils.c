#include "nxai_threading_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "nxai_utils.h"

#if defined( _MSC_VER )
// Windows specific imports
#include <handleapi.h>
#include <ioapiset.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <windows.h>
#include <io.h>
#include <tchar.h>
#include <strsafe.h>
#include <direct.h>
#else
// Linux specific imports
#include <spawn.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/prctl.h>
#endif

#ifdef NXAI_DEBUG
#include "memory_leak_detector.h"
#endif

void nxai_thread_join( nxai_thread_t thread ) {
#if defined( _MSC_VER )
    // Windows implementation
    WaitForSingleObject( thread, INFINITE );
#else
    // Linux implementation
    pthread_join( thread, NULL );
#endif
}

bool nxai_thread_create( nxai_thread_t *thread, function_ptr function, void *input_arguments ) {
#if defined( _MSC_VER )
    // Windows implementation
    DWORD dwThreadIdArray;

    // Create the thread to begin execution on its own.
    *thread = CreateThread(
            NULL,              // default security attributes
            0,                 // use default stack size
            function,          // thread function name
            input_arguments,   // argument to thread function
            0,                 // use default creation flags
            &dwThreadIdArray );// returns the thread identifier

    // Check the return value for success.
    if ( *thread == NULL ) {
        nxai_vlog( "Could not create thread!\n" );
        return false;
    }
    return true;
#else
    // Linux implementation
    int ret = pthread_create( thread, NULL, (void *) function, input_arguments );
    return ret == 0;
#endif
}

// Lock mutex function
void nxai_lock_mutex( nxai_mutex_t *mutex ) {
#if defined( _MSC_VER )
    // Windows implementation
    WaitForSingleObject( *mutex, INFINITE );
#else
    // Linux implementation
    pthread_mutex_lock( mutex );
#endif
}

// Unlock mutex function
void nxai_unlock_mutex( nxai_mutex_t *mutex ) {
#if defined( _MSC_VER )
    // Windows implementation
    ReleaseMutex( *mutex );
#else
    // Linux implementation
    pthread_mutex_unlock( mutex );
#endif
}

// Initialize mutex function
nxai_mutex_t nxai_initialize_mutex() {
#if defined( _MSC_VER )
    // Windows implementation using CreateMutex
    return CreateMutexA( NULL, FALSE, NULL );
#else
    // Linux implementation using pthread_mutex_t
    static pthread_mutex_t new_mutex = PTHREAD_MUTEX_INITIALIZER;
    return new_mutex;
#endif
}
