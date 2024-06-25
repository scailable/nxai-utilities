#pragma once
#include <spawn.h>

pid_t nxai_start_process( char *const argv[], const char* output_filepath, int* output_fd );
