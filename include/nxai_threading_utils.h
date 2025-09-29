#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "nxai_pipe_utils.h"
#if defined( _MSC_VER )
// Windows specific imports
#define NOMINMAX//< Needed to prevent windows.h define macros min() and max().
#include <windows.h>
#include <handleapi.h>
#include <ioapiset.h>
#include <processthreadsapi.h>
#include <synchapi.h>
typedef HANDLE nxai_thread_t;
typedef HANDLE nxai_mutex_t;
typedef unsigned long nxai_thread_return_t;
#define NXAI_THREAD_RETURN 0
#else
// Linux specific imports
#include <spawn.h>
#include <pthread.h>
typedef pthread_t nxai_thread_t;
typedef pthread_mutex_t nxai_mutex_t;
typedef void *nxai_thread_return_t;
#define NXAI_THREAD_RETURN NULL
#endif

void nxai_thread_join( nxai_thread_t thread );

#ifdef _MSC_VER
typedef unsigned long ( *function_ptr )( void * );
#else
typedef void *( *function_ptr )( void * );
#endif
char *_nxai_path_join( int arg_count, ... );
bool nxai_thread_create( nxai_thread_t *thread, function_ptr function, void *input_arguments );

void nxai_lock_mutex( nxai_mutex_t *mutex );

void nxai_unlock_mutex( nxai_mutex_t *mutex );

nxai_mutex_t nxai_initialize_mutex();

void nxai_atomic_increment( int *number );

void nxai_atomic_decrement( int *number );

#ifdef __cplusplus
}
#endif