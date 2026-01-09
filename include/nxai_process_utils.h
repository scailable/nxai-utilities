#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "nxai_threading_utils.h"

#if !defined(EXPORT_MACRO)
    #define EXPORT_MACRO
#endif

#if defined(_MSC_VER)
    // Windows specific imports
    #define NOMINMAX //< Needed to prevent windows.h define macros min() and max().
    #include <handleapi.h>
    #include <ioapiset.h>
    #include <processthreadsapi.h>
    #include <synchapi.h>
    #include <windows.h>

    #include "nxai_pipe_utils.h"
typedef struct nxai_process_t
{
    DWORD process_id;
    HANDLE job_handle;
} nxai_process_t;
typedef unsigned long nxai_thread_return_t;
    #define NXAI_THREAD_RETURN 0
#else
    // Linux specific imports
    #include <pthread.h>
    #include <spawn.h>
typedef pid_t nxai_process_t;
#endif

#ifdef NXAI_DEBUG
    #define debug_vlog(fmt, args...) nxai_vlog(fmt, ##args)
#else
    #define debug_vlog(fmt, ...) /* Don't do anything in release builds */
#endif

EXPORT_MACRO nxai_process_t
    nxai_start_process(char* const argv[], bool connect_console, nxai_pipe_t* stderr_pipe);

EXPORT_MACRO int nxai_process_wait(nxai_process_t process_id, int timeout_seconds);

EXPORT_MACRO int nxai_kill_process(nxai_process_t process);

EXPORT_MACRO bool nxai_check_process_status(nxai_process_t process, int* status);

EXPORT_MACRO void nxai_process_set_sigs(void (*handler)(int));

EXPORT_MACRO bool nxai_process_started(nxai_process_t process);

#ifdef __cplusplus
}
#endif
