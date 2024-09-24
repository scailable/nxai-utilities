#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <spawn.h>
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

void nxai_initialise_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix, bool log_to_console );

void nxai_finalise_logging();

void nxai_vlog( const char *fmt, ... );

pid_t nxai_start_process( char *const argv[], bool connect_console );

#ifdef __cplusplus
}
#endif