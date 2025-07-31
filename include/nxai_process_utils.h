#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined( _MSC_VER )
// Windows specific imports
#define NOMINMAX//< Needed to prevent windows.h define macros min() and max().
#include <windows.h>
#include <handleapi.h>
#include <ioapiset.h>
#include <processthreadsapi.h>
#include <synchapi.h>
typedef HANDLE nxai_pipe_t;
typedef DWORD nxai_process_t;
typedef HANDLE nxai_thread_t;
typedef HANDLE nxai_mutex_t;
typedef unsigned long nxai_thread_return_t;
#define NXAI_THREAD_RETURN 0
typedef DWORD nxai_signal_t;
typedef BOOL nxai_handler_return_t;
#else
// Linux specific imports
#include <spawn.h>
#include <pthread.h>
typedef int nxai_pipe_t;
typedef pid_t nxai_process_t;
typedef pthread_t nxai_thread_t;
typedef void *nxai_thread_return_t;
#define NXAI_THREAD_RETURN NULL
typedef int32_t nxai_signal_t;
typedef void nxai_handler_return_t;
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef NXAI_DEBUG
#define debug_vlog( fmt, args... ) nxai_vlog( fmt, ##args )
#else
#define debug_vlog( fmt, args... ) /* Don't do anything in release builds */
#endif

int nxai_strcasecmp( const char *str1, const char *str2 );

void nxai_chmod( const char *filepath, int mode );

void nxai_thread_join( nxai_thread_t *thread );

#ifdef _MSC_VER
typedef unsigned long ( *function_ptr )( void * );
#else
typedef void *( *function_ptr )( void * );
#endif
bool nxai_thread_create( nxai_thread_t *thread, function_ptr function, void *input_arguments );
char *nxai_path_join( char *path, ... );

/**
 * Ensures proper cleanup of child processes when the parent process terminates.
 *
 * This function implements cross-platform functionality to handle child process
 * termination across Windows and Linux platforms. On Windows, it uses Job Objects
 * to monitor the parent process, while on Linux it utilizes the prctl system call.
 *
 * @see nxai_ensure_child_cleanup() for the implementation details
 * @note This function must be called from the child process
 * @warning Failure to call this function may result in orphaned processes
 *          if the parent terminates unexpectedly
 */
void nxai_ensure_child_cleanup();

void nxai_chdir( const char *path );

void nxai_sleep_ms( int milliseconds );

uint64_t nxai_current_timestamp_ms();

uint64_t nxai_current_timestamp_us();

void nxai_initialize_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix, bool log_to_console, bool log_to_file, int log_verbosity_level );

void nxai_finalise_logging();

void nxai_vlog_verbose( const char *fmt, ... );

void nxai_error_log( const char *fmt, ... );

void nxai_vlog( const char *fmt, ... );

nxai_process_t nxai_start_process( char *const argv[], bool connect_console, nxai_pipe_t *stderr_pipe );

int nxai_process_wait( nxai_process_t process_id, int timeout_seconds );

char *nxai_read_pipe_to_string( nxai_pipe_t pipe );

int nxai_kill_process( nxai_process_t process );

int nxai_shutdown_process( nxai_process_t process );

bool nxai_check_process_status( nxai_process_t process, int *status );

void nxai_lock_mutex( nxai_mutex_t *mutex );

void nxai_unlock_mutex( nxai_mutex_t *mutex );

nxai_mutex_t nxai_initialize_mutex();

void nxai_process_set_sigs( nxai_handler_return_t ( *handler )( nxai_signal_t ) );

bool nxai_process_started( nxai_process_t process );

#ifdef __cplusplus
}
#endif