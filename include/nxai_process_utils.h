#pragma once
#include <spawn.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void initialise_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix );

void nxai_vlog( const char *fmt, ... );

pid_t nxai_start_process( char *const argv[], const char *output_filepath, int *output_fd );

#ifdef __cplusplus
}
#endif