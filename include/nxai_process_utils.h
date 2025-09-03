#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#if defined( _MSC_VER )
// Windows specific imports
#define NOMINMAX//< Needed to prevent windows.h define macros min() and max().
#include <windows.h>
#include <handleapi.h>
#include <ioapiset.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include "nxai_shm_utils.h"
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
typedef pthread_mutex_t nxai_mutex_t;
typedef void *nxai_thread_return_t;
#define NXAI_THREAD_RETURN NULL
typedef int32_t nxai_signal_t;
typedef void nxai_handler_return_t;
#endif

#ifdef NXAI_DEBUG
#define debug_vlog( fmt, args... ) nxai_vlog( fmt, ##args )
#else
#define debug_vlog( fmt, args... ) /* Don't do anything in release builds */
#endif

// Helper macro to count arguments
#define PP_NARG( ... ) PP_NARG_( __VA_ARGS__, PP_RSEQ_N() )
#define PP_NARG_( ... ) PP_128TH_ARG( __VA_ARGS__ )
#define PP_128TH_ARG( _1, _2, _3, _4, _5, _6, _7, _8, _9, _10,                    \
                      _11, _12, _13, _14, _15, _16, _17, _18, _19, _20,           \
                      _21, _22, _23, _24, _25, _26, _27, _28, _29, _30,           \
                      _31, _32, _33, _34, _35, _36, _37, _38, _39, _40,           \
                      _41, _42, _43, _44, _45, _46, _47, _48, _49, _50,           \
                      _51, _52, _53, _54, _55, _56, _57, _58, _59, _60,           \
                      _61, _62, _63, _64, _65, _66, _67, _68, _69, _70,           \
                      _71, _72, _73, _74, _75, _76, _77, _78, _79, _80,           \
                      _81, _82, _83, _84, _85, _86, _87, _88, _89, _90,           \
                      _91, _92, _93, _94, _95, _96, _97, _98, _99, _100,          \
                      _101, _102, _103, _104, _105, _106, _107, _108, _109, _110, \
                      _111, _112, _113, _114, _115, _116, _117, _118, _119, _120, \
                      _121, _122, _123, _124, _125, _126, _127, N, ... ) N
#define PP_RSEQ_N() 127, 126, 125, 124, 123, 122, 121, 120,           \
                    119, 118, 117, 116, 115, 114, 113, 112, 111, 110, \
                    109, 108, 107, 106, 105, 104, 103, 102, 101, 100, \
                    99, 98, 97, 96, 95, 94, 93, 92, 91, 90,           \
                    89, 88, 87, 86, 85, 84, 83, 82, 81, 80,           \
                    79, 78, 77, 76, 75, 74, 73, 72, 71, 70,           \
                    69, 68, 67, 66, 65, 64, 63, 62, 61, 60,           \
                    59, 58, 57, 56, 55, 54, 53, 52, 51, 50,           \
                    49, 48, 47, 46, 45, 44, 43, 42, 41, 40,           \
                    39, 38, 37, 36, 35, 34, 33, 32, 31, 30,           \
                    29, 28, 27, 26, 25, 24, 23, 22, 21, 20,           \
                    19, 18, 17, 16, 15, 14, 13, 12, 11, 10,           \
                    9, 8, 7, 6, 5, 4, 3, 2, 1, 0

// Wrapper macro to handle the actual function call
#define nxai_path_join( ... ) _nxai_path_join( PP_NARG( __VA_ARGS__ ), __VA_ARGS__ )

#ifdef _MSC_VER
DWORD get_windows_error( DWORD errorCode, char *buffer, DWORD bufferSize );
#endif

int nxai_strcasecmp( const char *str1, const char *str2 );

void nxai_chmod( const char *filepath, int mode );

void nxai_thread_join( nxai_thread_t thread );

#ifdef _MSC_VER
typedef unsigned long ( *function_ptr )( void * );
#else
typedef void *( *function_ptr )( void * );
#endif
char *_nxai_path_join( int arg_count, ... );
bool nxai_thread_create( nxai_thread_t *thread, function_ptr function, void *input_arguments );

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

bool nxai_check_process_status( nxai_process_t process, int *status );

void nxai_lock_mutex( nxai_mutex_t *mutex );

void nxai_unlock_mutex( nxai_mutex_t *mutex );

nxai_mutex_t nxai_initialize_mutex();

void nxai_process_set_sigs( nxai_handler_return_t ( *handler )( nxai_signal_t ) );

bool nxai_process_started( nxai_process_t process );

#ifdef __cplusplus
}
#endif