#include "nxai_process_utils.h"

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

#if defined(_MSC_VER)
    // Windows specific imports
    #include <direct.h>
    #include <handleapi.h>
    #include <io.h>
    #include <ioapiset.h>
    #include <processthreadsapi.h>
    #include <strsafe.h>
    #include <synchapi.h>
    #include <tchar.h>
    #include <windows.h>
#else
    // Linux specific imports
    #include <spawn.h>
    #include <sys/prctl.h>
    #include <sys/stat.h>
    #include <sys/time.h>
    #include <sys/wait.h>
    #include <unistd.h>
extern char** environ;
#endif

#ifdef NXAI_DEBUG
    #include "memory_leak_detector.h"
#endif

void nxai_ensure_child_cleanup()
{
#if !defined(_MSC_VER)
    // Linux implementation
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
}

bool nxai_process_started(nxai_process_t process)
{
#if defined(_MSC_VER)
    // Windows implementation
    if (process.process_id == 1)
    {
        return false;
    }
#else
    // Linux implementation
    if (process == 1)
    {
        return false;
    }
#endif
    return true;
}

#if defined(_MSC_VER)
static char* convert_input_arguments(char* const argv[])
{
    // Determine length of string
    int index = 0;
    size_t string_length = 0;
    while (argv[index] != NULL)
    {
        string_length +=
            strlen(argv[index]) + 3; // Make space for trailing space and quotation marks
        index++;
        if (index == 1024)
        {
            nxai_vlog(
                "Too many input arguments! Array needs to be terminated with a NULL pointer.\n");
            return NULL;
        }
    }
    if (string_length == 0)
    {
        nxai_vlog("Error! Argument string length is 0.\n");
        return NULL;
    }
    // Alloc string
    char* argument_string = (char*) malloc(string_length * sizeof(char));
    // Generate string
    index = 0;
    size_t current_index = 0;
    while (argv[index] != NULL)
    {
        // Add preceding quote
        argument_string[current_index++] = '"';
        size_t arg_length = strlen(argv[index]);
        memcpy(&(argument_string[current_index]), argv[index], arg_length);
        current_index += arg_length;
        // Add trailing quote
        argument_string[current_index++] = '"';
        // Add space
        argument_string[current_index++] = ' ';
        index++;
    }
    argument_string[current_index - 1] = 0;
    return argument_string;
}
#endif

nxai_process_t nxai_start_process(
    char* const argv[],
    bool connect_console,
    nxai_pipe_t* stderr_pipe)
{
#if defined(_MSC_VER)
    nxai_process_t new_process = {.process_id = 1, .job_handle = NULL};

    // Windows implementation
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);

    if (!connect_console)
    {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = INVALID_HANDLE_VALUE;
    }

    // Connect stderr to the provided pipe
    if (stderr_pipe != NULL && false)
    {
        // Initialize stderr_pipe and handles
        HANDLE hWritePipe;
        *stderr_pipe = nxai_create_empty_pipe();
        nxai_create_pipe_handles(&(*stderr_pipe)->handle, &hWritePipe);

        // The writing end of the pipe goes to the subprocess
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdError = hWritePipe;

        // Prevent the pipe handle from being inherited by other processes
        SetHandleInformation((*stderr_pipe)->handle, HANDLE_FLAG_INHERIT, 0);
    }

    char* argument_string = convert_input_arguments(argv);

    // Create job object for process management
    SECURITY_ATTRIBUTES saAttr = {0};
    saAttr.nLength = sizeof(saAttr);
    saAttr.bInheritHandle = FALSE; // Prevent handle inheritance

    HANDLE job_handle = CreateJobObjectA(&saAttr, NULL);
    if (job_handle == NULL)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog(
            "Error: Could not create job object for process: %.*s\n",
            error_length,
            error_string);
        free(argument_string);
        return new_process;
    }

    // Configure job object limits
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info = {0};
    job_info.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    job_info.BasicLimitInformation.PriorityClass = NORMAL_PRIORITY_CLASS;

    if (!SetInformationJobObject(
            job_handle,
            JobObjectExtendedLimitInformation,
            &job_info,
            sizeof(job_info)))
    {
        CloseHandle(job_handle);
        free(argument_string);
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog(
            "Error: Could not set information for job object: %.*s\n",
            error_length,
            error_string);
        return new_process;
    }

    // Create process suspended
    DWORD creation_flags = CREATE_SUSPENDED;
    if (!CreateProcessA(
            NULL, // lpApplicationName
            argument_string, // lpCommandLine
            NULL, // lpProcessAttributes
            NULL, // lpThreadAttributes
            TRUE, // bInheritHandles
            creation_flags, // dwCreationFlags
            NULL, // lpEnvironment
            NULL, // lpCurrentDirectory
            &si, // lpStartupInfo
            &pi // lpProcessInformation
            ))
    {
        CloseHandle(job_handle);
        free(argument_string);
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog("Error: Could not start process: %.*s\n", error_length, error_string);
        return new_process;
    }

    // Assign suspended process to job object
    if (!AssignProcessToJobObject(job_handle, pi.hProcess))
    {
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(job_handle);
        free(argument_string);
        return new_process;
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    free(argument_string);

    new_process.process_id = pi.dwProcessId;
    new_process.job_handle = job_handle;
    return new_process;
#else
    // Linux implementation
    pid_t child_pid;
    int cerr_pipe[2];

    // Initialize file actions and attributes objects
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attrp;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawnattr_init(&attrp);

    if (connect_console == false)
    {
        // Redirect stdout to /dev/null
        posix_spawn_file_actions_adddup2(
            &file_actions,
            open("/dev/null", O_WRONLY),
            STDOUT_FILENO);
    }

    // Connect stderr
    pipe(cerr_pipe);
    fcntl(cerr_pipe[0], F_SETFL, O_NONBLOCK);
    posix_spawn_file_actions_addclose(&file_actions, cerr_pipe[0]);
    posix_spawn_file_actions_adddup2(&file_actions, cerr_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&file_actions, cerr_pipe[1]);

    // Spawn a new process
    if (posix_spawn(&child_pid, argv[0], &file_actions, &attrp, argv, environ) != 0)
    {
        // Could not start
        return 1;
    }

    // Cleanup
    posix_spawn_file_actions_destroy(&file_actions);
    posix_spawnattr_destroy(&attrp);
    close(cerr_pipe[1]);
    *stderr_pipe = cerr_pipe[0];

    return child_pid;
#endif
}

static void sigchld_handler(int signum)
{
    // Empty handler, just to register the signal
    (void) signum;
}

int nxai_process_wait(nxai_process_t process, int timeout_seconds)
{
#if defined(_MSC_VER)
    // Windows implementation
    HANDLE hProcess =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, process.process_id);

    if (hProcess == NULL)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog("Warning: Could not get process handle: %.*s\n", error_length, error_string);
        return -1;
    }

    DWORD waitResult = WaitForSingleObject(hProcess, timeout_seconds * 1000);

    switch (waitResult)
    {
        case WAIT_OBJECT_0:
        {
            DWORD exitCode;
            GetExitCodeProcess(hProcess, &exitCode);
            CloseHandle(hProcess);
            CloseHandle(process.job_handle);
            return exitCode;
        }
        case WAIT_TIMEOUT:
        {
            CloseHandle(hProcess);
            return -2;
        }
        default:
            char error_string[1024];
            DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
            nxai_vlog(
                "Warning: Could not wait for process kill: %.*s\n",
                error_length,
                error_string);
            CloseHandle(hProcess);
            return -1;
    }
#else
    // Linux implementation
    struct sigaction sa;
    sigset_t mask;

    // Register SIGCHLD handler
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1)
    {
        perror("sigaction");
        return -1;
    }

    // Block SIGCHLD
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
    {
        perror("sigprocmask");
        return -1;
    }

    // Set up timeout
    struct timespec ts = {
        .tv_sec = timeout_seconds,
        .tv_nsec = 0,
    };

    while (1)
    {
        // Reset timeout for each iteration
        int sig = sigtimedwait(&mask, NULL, &ts);

        if (sig == SIGCHLD)
        {
            int status;
            waitpid(process, &status, WNOHANG);
            return WEXITSTATUS(status);
        }
        else if (sig == -1)
        {
            if (errno == ETIMEDOUT)
            {
                return -2; // Timeout expired
            }
            else if (errno != EINTR)
            {
                return -1; // Other errors
            }
            // Handle EINTR by breaking
            break;
        }
    }
#endif
}

int nxai_kill_process(nxai_process_t process)
{
#if defined(_MSC_VER)
    // Windows implementation

    DWORD exitCode = 9;
    HANDLE hProcess = OpenProcess(
        PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        process.process_id);
    if (hProcess == NULL)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog(
            "Warning: Could not get process handle to terminate: %.*s\n",
            error_length,
            error_string);
        return -1;
    }
    // Terminate the process
    nxai_vlog("Sending termination signal...\n");
    if (!TerminateProcess(hProcess, 9))
    {
        CloseHandle(hProcess);
        return -1;
    }
    return exitCode;
#else
    // Linux implementation
    return kill(process, SIGKILL);
#endif
}

bool nxai_check_process_status(nxai_process_t process, int* status)
{
#if defined(_MSC_VER)
    // Windows implementation
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process.process_id);

    if (hProcess == NULL)
    {
        return false;
    }

    DWORD exitCode;
    if (GetExitCodeProcess(hProcess, &exitCode))
    {
        *status = exitCode;
    }
    else
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog("Error: Could not get exit code of process: %.*s\n", error_length, error_string);
        *status = 1;
    }
    CloseHandle(hProcess);
    return exitCode == STILL_ACTIVE;
#else
    // Linux implementation
    int result = waitpid(process, status, WNOHANG);
    if (result == 0)
    {
        return true;
    }
    else
    {
        *status = WEXITSTATUS(*status);
        return false;
    }
#endif
}

void nxai_process_set_sigs(void (*handler)(int))
{
#if !defined(_MSC_VER)
    signal(SIGQUIT, handler);
    // We expect write failures to occur but we want to handle them where
    // the error occurs rather than in a SIGPIPE handler.
    signal(SIGPIPE, SIG_IGN);
#endif
    // Linux implementation
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGABRT, handler);

    // Set death signal when parent is terminated
    nxai_ensure_child_cleanup();
}
