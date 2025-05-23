#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#if defined( __WIN32__ )
// Windows specific imports
#include <handleapi.h>
#include <ioapiset.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <windows.h>
typedef HANDLE nxai_pipe_t;
typedef HANDLE nxai_process_t;
#else
typedef int nxai_pipe_t;
typedef pid_t nxai_process_t;
// Linux specific imports
#include <spawn.h>
#endif

#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef NXAI_DEBUG
#define debug_vlog( fmt, args... ) nxai_vlog( fmt, ##args )
#else
#define debug_vlog( fmt, args... ) /* Don't do anything in release builds */
#endif

uint64_t nxai_current_timestamp_ms();

uint64_t nxai_current_timestamp_us();

void nxai_initialize_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix, bool log_to_console, bool log_to_file, int log_verbosity_level );

void nxai_finalise_logging();

void nxai_vlog_verbose( const char *fmt, ... );

void nxai_vlog( const char *fmt, ... );

nxai_process_t nxai_start_process( char *const argv[], bool connect_console, nxai_pipe_t *stderr_pipe );

int waitpid_timeout( pid_t process_id, int timeout_seconds );

#ifdef __cplusplus
}
#endif