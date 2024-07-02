#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <spawn.h>
#include <stdarg.h>
#include <stdint.h>

uint64_t nxai_current_timestamp_ms();

uint64_t nxai_current_timestamp_us();

void nxai_initialise_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix );

void nxai_finalise_logging();

void nxai_vlog( const char *fmt, ... );

pid_t nxai_start_process( char *const argv[] );

#ifdef __cplusplus
}
#endif