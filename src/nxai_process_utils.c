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

#if defined( _MSC_VER )
// Windows specific imports
#include <handleapi.h>
#include <ioapiset.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <windows.h>
#include <io.h>
#include <tchar.h>
#include <strsafe.h>
#include <direct.h>
#else
// Linux specific imports
#include <spawn.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/prctl.h>
#endif

#ifdef NXAI_DEBUG
#include "memory_leak_detector.h"
#endif

extern char **environ;

char *_start_log_filepath = NULL;
char *_rotating_log_filepath = NULL;
char *_log_log_filepath = NULL;
char *_log_prefix = NULL;
char *_old_logfile_path = NULL;

static uint64_t last_timestamp = 0;
size_t logfile_max_size_mb = 10;
static bool start_logfile_full = false;
static size_t logfile_last_size = 0;
static bool _log_to_console = false;
static bool _log_to_file = true;
static int _log_verbosity_level = 1;
FILE *start_logfile;
FILE *rotating_logfile;
nxai_mutex_t rotating_logfile_lock;

static void nxai_vvlog( const char *fmt, va_list *args );

#if defined( _MSC_VER )
DWORD get_windows_error( DWORD errorCode, char *buffer, DWORD bufferSize ) {
    return FormatMessageA(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            errorCode,
            MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
            buffer,
            bufferSize,
            NULL );
}
#endif

int nxai_strcasecmp( const char *str1, const char *str2 ) {
#if defined( _MSC_VER )
    // Windows implementation
    return _stricmp( str1, str2 );
#else
    // Linux implementation
    return strcasecmp( str1, str2 );
#endif
}

void nxai_chmod( const char *filepath, int mode ) {
#if defined( _MSC_VER )
    // Windows implementation
    _chmod( filepath, mode );
#else
    // Linux implementation
    chmod( filepath, mode );
#endif
}

void nxai_thread_join( nxai_thread_t thread ) {
#if defined( _MSC_VER )
    // Windows implementation
    WaitForSingleObject( thread, INFINITE );
#else
    // Linux implementation
    pthread_join( thread, NULL );
#endif
}

/**
 * Combines multiple path components into a single path string.
 * Uses forward slashes on Unix-like systems and backslashes on Windows.
 * The first path component is passed explicitly, remaining components via ...
 *
 * N.B. Last argument to thus function must always be NULL
 * @param path First component of the path
 * @param ... Variable number of additional path components
 * @return A newly allocated string containing the joined path
 */
char *_nxai_path_join( int arg_count, ... ) {
// Add separator
#if defined( _MSC_VER )
    // Windows implementation
    char separator = '\\';
#else
    // Linux implementation
    char separator = '/';
#endif

    va_list args;
    char *result = NULL;
    size_t total_length = 0;

    // First pass: calculate total length
    va_start( args, arg_count );
    const char *arg = va_arg( args, const char * );

    for ( size_t arg_index = 0; arg_index < arg_count; arg_index++ ) {
        size_t arg_length = strlen( arg );
        total_length += arg_length;

        if ( arg[arg_length - 1] != separator ) {
            total_length += 1;// Add space for separator
        }
        arg = va_arg( args, const char * );
    }

    va_end( args );

    // Allocate memory for result
    result = malloc( total_length + 1 );
    if ( !result ) {
        return NULL;
    }
    char *current_pos = result;

    // Second pass: construct remaining path
    va_start( args, arg_count );
    arg = va_arg( args, const char * );

    bool separator_added = false;
    for ( size_t arg_index = 0; arg_index < arg_count; arg_index++ ) {
        size_t arg_length = strlen( arg );

        // Copy argument
        memcpy( current_pos, arg, arg_length );
        current_pos += arg_length;

        if ( arg[arg_length - 1] != separator ) {
            *current_pos = separator;
            current_pos++;
            separator_added = true;
        } else {
            separator_added = false;
        }

        arg = va_arg( args, const char * );
    }
    if ( separator_added == true ) {
        // Ensure no trailing separator
        current_pos--;
    }

    va_end( args );
    *current_pos = '\0';// Null terminate

    return result;
}

bool nxai_thread_create( nxai_thread_t *thread, function_ptr function, void *input_arguments ) {
#if defined( _MSC_VER )
    // Windows implementation
    DWORD dwThreadIdArray;

    // Create the thread to begin execution on its own.
    *thread = CreateThread(
            NULL,              // default security attributes
            0,                 // use default stack size
            function,          // thread function name
            input_arguments,   // argument to thread function
            0,                 // use default creation flags
            &dwThreadIdArray );// returns the thread identifier

    // Check the return value for success.
    if ( *thread == NULL ) {
        nxai_vlog( "Could not create thread!\n" );
        return false;
    }
    return true;
#else
    // Linux implementation
    int ret = pthread_create( thread, NULL, (void *) function, input_arguments );
    return ret == 0;
#endif
}

void nxai_ensure_child_cleanup() {
#if !defined( _MSC_VER )
    // Linux implementation
    prctl( PR_SET_PDEATHSIG, SIGTERM );
#endif
}

void nxai_chdir( const char *path ) {
#if defined( _MSC_VER )
    // Windows implementation
    int result = _chdir( path );
#else
    // Linux implementation
    chdir( path );
#endif
}

void nxai_sleep_ms( int milliseconds ) {
#if defined( _MSC_VER )
    // Windows implementation
    Sleep( milliseconds );
#else
    // Linux implementation
    usleep( milliseconds * 1000 );
#endif
}

uint64_t nxai_current_timestamp_ms() {
    struct timespec ts;
    timespec_get( &ts, TIME_UTC );
    uint64_t time_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return time_ms;
}

uint64_t nxai_current_timestamp_us() {
    struct timespec ts;
    timespec_get( &ts, TIME_UTC );
    uint64_t time_us = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    return time_us;
}

#if defined( _WIN32 ) || defined( _WIN64 )
#define NXAI_FILE_PERMS _S_IWRITE | _S_IREAD
#define NXAI_SET_FILE_PERMS _chmod
#else
#define NXAI_FILE_PERMS 0666
#define NXAI_SET_FILE_PERMS chmod
#endif

void nxai_initialize_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix, bool log_to_console, bool log_to_file, int log_verbosity_level ) {
    _start_log_filepath = strdup( start_log_filepath );
    _rotating_log_filepath = strdup( rotating_log_filepath );
    _log_prefix = strdup( log_prefix );
    _log_to_console = log_to_console;
    _log_verbosity_level = log_verbosity_level;
    _log_to_file = log_to_file;
    if ( _log_to_file == true ) {
        // Create and clear log files
        start_logfile = fopen( _start_log_filepath, "w" );
        if ( start_logfile == NULL ) {
            printf( "Failed to initialise logfile: %s\n", _start_log_filepath );
        }
        NXAI_SET_FILE_PERMS( _start_log_filepath, 0666 );
        rotating_logfile = fopen( _rotating_log_filepath, "w" );
        if ( rotating_logfile == NULL ) {
            printf( "Failed to initialise logfile: %s\n", _rotating_log_filepath );
        }
        rotating_logfile_lock = nxai_initialize_mutex();
        NXAI_SET_FILE_PERMS( _rotating_log_filepath, 0666 );
        // Create old log file path
        size_t old_filepath_length = strlen( _rotating_log_filepath ) + 5 + 1;
        _old_logfile_path = (char *) malloc( old_filepath_length );
        strcpy( _old_logfile_path, _rotating_log_filepath );
        strcat( _old_logfile_path, ".old" );
    }
}

void nxai_finalise_logging() {
    free( _start_log_filepath );
    free( _rotating_log_filepath );
    free( _log_prefix );
    free( _old_logfile_path );
    if ( start_logfile_full == false ) {
        fclose( start_logfile );
    }
    fclose( rotating_logfile );
}

void nxai_vlog_verbose( const char *fmt, ... ) {
    if ( _log_verbosity_level > 1 ) {
        va_list args;
        va_start( args, fmt );
        nxai_vvlog( fmt, &args );
        va_end( args );
    }
}

void nxai_error_log( const char *fmt, ... ) {
    va_list args;
    va_list args_copy;

    // Initialize both va_lists at once
    va_start( args, fmt );
    va_copy( args_copy, args );

    // First, process arguments for stderr printing
    vfprintf( stderr, fmt, args );

    // Process the same arguments for vvlog
    nxai_vvlog( fmt, &args_copy );

    // Clean up both va_lists
    va_end( args );
    va_end( args_copy );
}

void nxai_vlog( const char *fmt, ... ) {
    va_list args;
    va_start( args, fmt );
    nxai_vvlog( fmt, &args );
    va_end( args );
}

bool nxai_get_file_size( const char *filepath, size_t *file_size ) {
#if defined( _MSC_VER )
    // Windows specific implementation
    WIN32_FIND_DATA fileData;
    HANDLE hFile = FindFirstFile( filepath, &fileData );

    if ( hFile != INVALID_HANDLE_VALUE ) {
        *file_size = (size_t) fileData.nFileSizeLow;
        FindClose( hFile );
        return true;
    } else {
        printf( "An unexpected error occurred accessing log file: %s %s\n", filepath, strerror( errno ) );
        return false;
    }

#else
    // Linux specific implementation
    struct stat file_stat;
    if ( stat( _start_log_filepath, &file_stat ) < 0 ) {
        switch ( errno ) {
            case EACCES:// Permission denied
                printf( "Permission denied trying to open logfile: %s.\n", filepath );
            default:
                printf( "An unexpected error occurred accessing log file: %s %s\n", filepath, strerror( errno ) );
        }
        return false;
    }
    *file_size = file_stat.st_size;
    return true;
#endif
}

static void nxai_vvlog( const char *fmt, va_list *args ) {

    if ( _log_verbosity_level == 0 || ( _log_to_file == false && _log_to_console == false ) ) {
        // Logging is turned off. Return immediately
        return;
    }

    // Copy argument list
    va_list copied_args;
    va_copy( copied_args, *args );

    // Get the current timestamp
    uint64_t timestamp = nxai_current_timestamp_us();

    uint64_t duration = 0;
    if ( last_timestamp == 0 ) {
        last_timestamp = timestamp;
    }

    duration = timestamp - last_timestamp;
    last_timestamp = timestamp;

    const char *log_prefix = _log_prefix;
    if ( log_prefix == NULL ) {
        log_prefix = "";
    }

    if ( _log_to_console == true ) {
        // Print to console
        printf( "%s%llu %09llu: ", log_prefix, (uint64_t) timestamp / 1000, duration );
        vprintf( fmt, *args );
    }

    if ( _start_log_filepath == NULL || _rotating_log_filepath == NULL || _log_to_file == false ) {
        return;
    }

    FILE *flogfile = NULL;

    // Determine which file to log to
    if ( start_logfile_full == false ) {
        // Check if start logfile is full
        if ( (size_t) logfile_last_size < logfile_max_size_mb * 1000000 ) {
            // Write to start_log
            flogfile = start_logfile;
        } else {
            start_logfile_full = true;
            logfile_last_size = 0;
            fclose( start_logfile );
        }
    }

    if ( flogfile == NULL ) {
        // Start logfile was full, open rotating logfile
        nxai_lock_mutex( &rotating_logfile_lock );
        if ( (size_t) logfile_last_size > logfile_max_size_mb * 1000000 ) {
            // Rotating logfile is full, rename to ".old"
            if ( rotating_logfile != NULL ) {
                fclose( rotating_logfile );
                rotating_logfile = NULL;
                int result = rename( _rotating_log_filepath, _old_logfile_path );
                if ( result != 0 ) {
                    perror( "Error renaming file" );
                    nxai_unlock_mutex( &rotating_logfile_lock );
                    return;
                }
            }
            // Create new log file
            rotating_logfile = fopen( _rotating_log_filepath, "w" );
            if ( rotating_logfile == NULL ) {
                perror( "Error creating log file" );
                nxai_unlock_mutex( &rotating_logfile_lock );
                return;
            }
            logfile_last_size = 0;
        }
        nxai_unlock_mutex( &rotating_logfile_lock );
        // Write to rotating log
        flogfile = rotating_logfile;
    }

    // Write to logfile
    int bytes_written = fprintf( flogfile, "%s%llu %09llu: ", log_prefix, (uint64_t) timestamp / 1000, duration );
    if ( bytes_written < 0 ) {
        printf( "Failed to write to log file!\n" );
        return;
    }
    logfile_last_size += bytes_written;
    bytes_written = vfprintf( flogfile, fmt, copied_args );
    if ( bytes_written < 0 ) {
        printf( "Failed to write to log file!\n" );
        return;
    }
    logfile_last_size += bytes_written;
    if ( _log_verbosity_level > 1 ) {
        fflush( flogfile );// Flush writing file to make sure latest prints are logged
        fflush( stdout );
    }
}

bool nxai_process_started( nxai_process_t process ) {
#if defined( _MSC_VER )
    // Windows implementation
    if ( process.process_id == 1 ) {
        return false;
    }
#else
    // Linux implementation
    if ( process == 1 ) {
        return false;
    }
#endif
    return true;
}

#if defined( _MSC_VER )
static char *convert_input_arguments( char *const argv[] ) {
    // Determine length of string
    int index = 0;
    size_t string_length = 0;
    while ( argv[index] != NULL ) {
        string_length += strlen( argv[index] ) + 3;// Make space for trailing space and quotation marks
        index++;
        if ( index == 1024 ) {
            nxai_vlog( "Too many input arguments! Array needs to be terminated with a NULL pointer.\n" );
            return NULL;
        }
    }
    if ( string_length == 0 ) {
        nxai_vlog( "Error! Argument string length is 0.\n" );
        return NULL;
    }
    // Alloc string
    char *argument_string = malloc( string_length * sizeof( char ) );
    // Generate string
    index = 0;
    size_t current_index = 0;
    while ( argv[index] != NULL ) {
        // Add preceding quote
        argument_string[current_index++] = '"';
        size_t arg_length = strlen( argv[index] );
        memcpy( &( argument_string[current_index] ), argv[index], arg_length );
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

nxai_process_t nxai_start_process( char *const argv[], bool connect_console, nxai_pipe_t *stderr_pipe ) {
#if defined( _MSC_VER )
    // Windows implementation
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory( &si, sizeof( si ) );
    ZeroMemory( &pi, sizeof( pi ) );

    si.cb = sizeof( si );

    if ( !connect_console ) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = INVALID_HANDLE_VALUE;
    }

    // Connect stderr to the provided pipe
    if ( stderr_pipe != NULL ) {
        // Initialize stderr_pipe and handles
        HANDLE hWritePipe;
        *stderr_pipe = nxai_create_empty_pipe();
        nxai_create_pipe_handles( &( *stderr_pipe )->handle, &hWritePipe );

        // The writing end of the pipe goes to the subprocess
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdError = hWritePipe;

        // Prevent the pipe handle from being inherited by other processes
        SetHandleInformation( ( *stderr_pipe )->handle, HANDLE_FLAG_INHERIT, 0 );
    }

    char *argument_string = convert_input_arguments( argv );

    // Create job object for process management
    SECURITY_ATTRIBUTES saAttr = { 0 };
    saAttr.nLength = sizeof( saAttr );
    saAttr.bInheritHandle = FALSE;// Prevent handle inheritance

    HANDLE job_handle = CreateJobObjectA( &saAttr, NULL );
    if ( job_handle == NULL ) {
        char error_string[1024];
        DWORD error_length = get_windows_error( GetLastError(), error_string, 1024 );
        nxai_vlog( "Error: Could not create job object for process: %.*s\n", error_length, error_string );
        free( argument_string );
        return (nxai_process_t) { 1, NULL };
    }

    // Configure job object limits
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info = { 0 };
    job_info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_BREAKAWAY_OK;
    job_info.BasicLimitInformation.PriorityClass = NORMAL_PRIORITY_CLASS;

    if ( !SetInformationJobObject( job_handle, JobObjectExtendedLimitInformation,
                                   &job_info, sizeof( job_info ) ) ) {
        CloseHandle( job_handle );
        free( argument_string );
        char error_string[1024];
        DWORD error_length = get_windows_error( GetLastError(), error_string, 1024 );
        nxai_vlog( "Error: Could not set information for job object: %.*s\n", error_length, error_string );
        return (nxai_process_t) { 1, NULL };
    }

    // Create process suspended
    DWORD creation_flags = CREATE_SUSPENDED;
    if ( !CreateProcessA(
                 NULL,           // lpApplicationName
                 argument_string,// lpCommandLine
                 NULL,           // lpProcessAttributes
                 NULL,           // lpThreadAttributes
                 TRUE,           // bInheritHandles
                 creation_flags, // dwCreationFlags
                 NULL,           // lpEnvironment
                 NULL,           // lpCurrentDirectory
                 &si,            // lpStartupInfo
                 &pi             // lpProcessInformation
                 ) ) {
        CloseHandle( job_handle );
        free( argument_string );
        char error_string[1024];
        DWORD error_length = get_windows_error( GetLastError(), error_string, 1024 );
        nxai_vlog( "Error: Could not start process: %.*s\n", error_length, error_string );
        return (nxai_process_t) { 1, NULL };
    }

    // Assign suspended process to job object
    if ( !AssignProcessToJobObject( job_handle, pi.hProcess ) ) {
        TerminateProcess( pi.hProcess, 0 );
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
        CloseHandle( job_handle );
        free( argument_string );
        return (nxai_process_t) { 1, NULL };
    }

    ResumeThread( pi.hThread );
    CloseHandle( pi.hThread );
    CloseHandle( pi.hProcess );
    free( argument_string );

    nxai_process_t new_process = { .process_id = pi.dwProcessId, .job_handle = job_handle };
    return new_process;
#else
    // Linux implementation
    pid_t child_pid;
    int cerr_pipe[2];

    // Initialize file actions and attributes objects
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attrp;
    posix_spawn_file_actions_init( &file_actions );
    posix_spawnattr_init( &attrp );

    if ( connect_console == false ) {
        // Redirect stdout to /dev/null
        posix_spawn_file_actions_adddup2( &file_actions, open( "/dev/null", O_WRONLY ), STDOUT_FILENO );
    }

    // Connect stderr
    pipe( cerr_pipe );
    fcntl( cerr_pipe[0], F_SETFL, O_NONBLOCK );
    posix_spawn_file_actions_addclose( &file_actions, cerr_pipe[0] );
    posix_spawn_file_actions_adddup2( &file_actions, cerr_pipe[1], STDERR_FILENO );
    posix_spawn_file_actions_addclose( &file_actions, cerr_pipe[1] );

    // Spawn a new process
    if ( posix_spawn( &child_pid, argv[0], &file_actions, &attrp, argv, environ ) != 0 ) {
        // Could not start
        return 1;
    }

    // Cleanup
    posix_spawn_file_actions_destroy( &file_actions );
    posix_spawnattr_destroy( &attrp );
    close( cerr_pipe[1] );
    *stderr_pipe = cerr_pipe[0];

    return child_pid;
#endif
}

static void sigchld_handler( int signum ) {
    // Empty handler, just to register the signal
    (void) signum;
}

int nxai_process_wait( nxai_process_t process, int timeout_seconds ) {
#if defined( _MSC_VER )
    // Windows implementation
    HANDLE hProcess = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE,
                                   FALSE, process.process_id );

    if ( hProcess == NULL ) {
        char error_string[1024];
        DWORD error_length = get_windows_error( GetLastError(), error_string, 1024 );
        nxai_vlog( "Warning: Could not get process handle: %.*s\n", error_length, error_string );
        return -1;
    }

    DWORD waitResult = WaitForSingleObject( hProcess, timeout_seconds * 1000 );

    switch ( waitResult ) {
        case WAIT_OBJECT_0: {
            DWORD exitCode;
            GetExitCodeProcess( hProcess, &exitCode );
            CloseHandle( hProcess );
            CloseHandle( process.job_handle );
            return exitCode;
        }
        case WAIT_TIMEOUT: {
            CloseHandle( hProcess );
            return -2;
        }
        default:
            char error_string[1024];
            DWORD error_length = get_windows_error( GetLastError(), error_string, 1024 );
            nxai_vlog( "Warning: Could not wait for process kill: %.*s\n", error_length, error_string );
            CloseHandle( hProcess );
            return -1;
    }
#else
    // Linux implementation
    struct sigaction sa;
    sigset_t mask;

    // Register SIGCHLD handler
    sa.sa_handler = sigchld_handler;
    sigemptyset( &sa.sa_mask );
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if ( sigaction( SIGCHLD, &sa, NULL ) == -1 ) {
        perror( "sigaction" );
        return -1;
    }

    // Block SIGCHLD
    sigemptyset( &mask );
    sigaddset( &mask, SIGCHLD );
    if ( sigprocmask( SIG_BLOCK, &mask, NULL ) == -1 ) {
        perror( "sigprocmask" );
        return -1;
    }

    // Set up timeout
    struct timespec ts = {
            .tv_sec = timeout_seconds,
            .tv_nsec = 0,
    };

    while ( 1 ) {
        // Reset timeout for each iteration
        int sig = sigtimedwait( &mask, NULL, &ts );

        if ( sig == SIGCHLD ) {
            int status;
            waitpid( process, &status, WNOHANG );
            return WEXITSTATUS( status );
        } else if ( sig == -1 ) {
            if ( errno == ETIMEDOUT ) {
                return -2;// Timeout expired
            } else if ( errno != EINTR ) {
                return -1;// Other errors
            }
            // Handle EINTR by breaking
            break;
        }
    }
#endif
}

char *nxai_read_pipe_to_string( nxai_pipe_t pipe ) {
    char *out_string = malloc( sizeof( char ) * 1024 );
    size_t total_bytes_read = 0;
    char buffer[1024];
#if defined( _MSC_VER )
    // Windows implementation
    OVERLAPPED ov = {};
    ov.Offset = 0;
    ov.OffsetHigh = 0;

    while ( true ) {
        memset( &ov, 0, sizeof( OVERLAPPED ) );
        DWORD bytes_read;

        // Use OVERLAPPED I/O for non-blocking reads
        BOOL success = ReadFile( pipe->handle, buffer, sizeof( buffer ),
                                 &bytes_read, &ov );

        if ( !success ) {
            DWORD error = GetLastError();
            if ( error == ERROR_IO_PENDING ) {
                // No bytes to read
                CancelIo( pipe->handle );
                break;
            } else {
                free( out_string );
                return NULL;
            }
        }

        if ( bytes_read == 0 ) {
            break;
        }

        out_string = realloc( out_string, total_bytes_read + bytes_read + 1 );
        memcpy( out_string + total_bytes_read, buffer, bytes_read );
        total_bytes_read += bytes_read;
    }
#else
    // Linux implementation
    ssize_t bytes_read;
    while ( ( bytes_read = read( pipe, buffer, sizeof( buffer ) ) ) > 0 ) {
        out_string = realloc( out_string, total_bytes_read + bytes_read + 1 );
        memcpy( out_string + total_bytes_read, buffer, bytes_read );
        total_bytes_read += bytes_read;
    }
#endif
    out_string[total_bytes_read] = '\0';
    return out_string;
}

int nxai_kill_process( nxai_process_t process ) {
#if defined( _MSC_VER )
    // Windows implementation

    DWORD exitCode = 9;
    HANDLE hProcess = OpenProcess( PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION,
                                   FALSE, process.process_id );
    if ( hProcess == NULL ) {
        char error_string[1024];
        DWORD error_length = get_windows_error( GetLastError(), error_string, 1024 );
        nxai_vlog( "Warning: Could not get process handle to terminate: %.*s\n", error_length, error_string );
        return -1;
    }
    // Terminate the process
    nxai_vlog( "Sending termination signal...\n" );
    if ( !TerminateProcess( hProcess, 9 ) ) {
        CloseHandle( hProcess );
        return -1;
    }
    return exitCode;
#else
    // Linux implementation
    return kill( process, SIGKILL );
#endif
}

bool nxai_check_process_status( nxai_process_t process, int *status ) {
#if defined( _MSC_VER )
    // Windows implementation
    HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION, FALSE, process.process_id );

    if ( hProcess == NULL ) {
        return false;
    }

    DWORD exitCode;
    if ( GetExitCodeProcess( hProcess, &exitCode ) ) {
        *status = exitCode;
    } else {
        char error_string[1024];
        DWORD error_length = get_windows_error( GetLastError(), error_string, 1024 );
        nxai_vlog( "Error: Could not get exit code of process: %.*s\n", error_length, error_string );
        *status = 1;
    }
    CloseHandle( hProcess );
    return exitCode == STILL_ACTIVE;
#else
    // Linux implementation
    int result = waitpid( process, status, WNOHANG );
    if ( result == 0 ) {
        return true;
    } else {
        *status = WEXITSTATUS( *status );
        return false;
    }
#endif
}

// Lock mutex function
void nxai_lock_mutex( nxai_mutex_t *mutex ) {
#if defined( _MSC_VER )
    // Windows implementation
    WaitForSingleObject( *mutex, INFINITE );
#else
    // Linux implementation
    pthread_mutex_lock( mutex );
#endif
}

// Unlock mutex function
void nxai_unlock_mutex( nxai_mutex_t *mutex ) {
#if defined( _MSC_VER )
    // Windows implementation
    ReleaseMutex( *mutex );
#else
    // Linux implementation
    pthread_mutex_unlock( mutex );
#endif
}

// Initialize mutex function
nxai_mutex_t nxai_initialize_mutex() {
#if defined( _MSC_VER )
    // Windows implementation using CreateMutex
    return CreateMutex( NULL, FALSE, NULL );
#else
    // Linux implementation using pthread_mutex_t
    static pthread_mutex_t new_mutex = PTHREAD_MUTEX_INITIALIZER;
    return new_mutex;
#endif
}

void nxai_process_set_sigs( void ( *handler )( int ) ) {
#if !defined( _MSC_VER )
    signal( SIGQUIT, handler );
    // We expect write failures to occur but we want to handle them where
    // the error occurs rather than in a SIGPIPE handler.
    signal( SIGPIPE, SIG_IGN );
#endif
    // Linux implementation
    signal( SIGINT, handler );
    signal( SIGTERM, handler );
    signal( SIGABRT, handler );

    // Set death signal when parent is terminated
    nxai_ensure_child_cleanup();
}