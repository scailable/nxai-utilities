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
    int ret = pthread_create( thread, NULL, (void *) function, NULL );
    return ret == 0;
#endif
}

void nxai_ensure_child_cleanup() {
#if defined( _MSC_VER )
    // Windows implementation
    HANDLE job = CreateJobObject( NULL, NULL );
    JOBOBJECT_ASSOCIATE_COMPLETION_PORT jobInfo;
    jobInfo.CompletionPort = CreateIoCompletionPort( INVALID_HANDLE_VALUE, NULL, 1, 0 );
    jobInfo.CompletionKey = NULL;

    SetInformationJobObject( job, JobObjectAssociateCompletionPortInformation,
                             &jobInfo, sizeof( jobInfo ) );

    AssignProcessToJobObject( job, GetCurrentProcess() );

    CloseHandle( job );// Parent keeps handle closed
#else
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
#if defined( _MSC_VER )
    // Windows implementation
    FILETIME ft;
    ULARGE_INTEGER ui;

    GetSystemTimeAsFileTime( &ft );
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;

    // Convert from 100ns intervals to milliseconds
    const uint64_t HUNDRED_NANOSECONDS_TO_MILLISECONDS = 10000;
    return ui.QuadPart / HUNDRED_NANOSECONDS_TO_MILLISECONDS;
#else
    // Linux implementation
    struct timeval te;
    gettimeofday( &te, NULL );                                    // get current time
    int64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000;// calculate milliseconds
    return milliseconds;
#endif
}

uint64_t nxai_current_timestamp_us() {
#if defined( _MSC_VER )
    // Windows implementation
    FILETIME ft;
    ULARGE_INTEGER ui;

    GetSystemTimeAsFileTime( &ft );
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;

    // Convert from 100ns intervals to microseconds
    const uint64_t HUNDRED_NANOSECONDS_TO_MICROSECONDS = 10;
    return ui.QuadPart / HUNDRED_NANOSECONDS_TO_MICROSECONDS;
#else
    // Linux implementation
    struct timeval te;
    gettimeofday( &te, NULL );// get current time
    int64_t microseconds = te.tv_sec * 1000000LL + te.tv_usec;
    return microseconds;
#endif
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

    // First, process arguments for stderr printing
    va_start( args, fmt );
    vfprintf( stderr, fmt, args );
    va_end( args );

    // Make a copy of the arguments for nxai_vvlog
    va_copy( args, args );
    nxai_vvlog( fmt, &args );
    va_end( args );
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

    int64_t duration = 0;
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
        printf( "%s%ld %09lld: ", _log_prefix, timestamp / 1000, (long long) duration );
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
    int bytes_written = fprintf( flogfile, "%s%ld %09lld: ", _log_prefix, timestamp / 1000, (long long) duration );
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
    if ( process == 1 ) {
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

    si.dwFlags |= STARTF_USESTDHANDLES;

    char *argument_string = convert_input_arguments( argv );

    nxai_vlog( "Arg string: %s\n", argument_string );

    // Create process
    if ( !CreateProcessA(
                 NULL,           // lpApplicationName
                 argument_string,// lpCommandLine
                 NULL,           // lpProcessAttributes
                 NULL,           // lpThreadAttributes
                 TRUE,           // bInheritHandles
                 0,              // dwCreationFlags
                 NULL,           // lpEnvironment
                 NULL,           // lpCurrentDirectory
                 &si,            // lpStartupInfo
                 &pi             // lpProcessInformation
                 ) ) {
        return 1;
    }

    CloseHandle( pi.hThread );
    return pi.dwProcessId;
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

int nxai_process_wait( nxai_process_t process_id, int timeout_seconds ) {
#if defined( _MSC_VER )
    // Windows implementation
    HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION | PROCESS_TERMINATE,
                                   FALSE, process_id );

    if ( hProcess == NULL ) {
        return -1;
    }

    DWORD waitResult = WaitForSingleObject( hProcess, timeout_seconds * 1000 );

    switch ( waitResult ) {
        case WAIT_OBJECT_0: {
            DWORD exitCode;
            GetExitCodeProcess( hProcess, &exitCode );
            CloseHandle( hProcess );
            return exitCode;
        }
        case WAIT_TIMEOUT: {
            TerminateProcess( hProcess, 1 );
            CloseHandle( hProcess );
            return -1;
        }
        default:
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
    struct timespec ts;
    clock_gettime( CLOCK_REALTIME, &ts );
    ts.tv_sec += timeout_seconds;

    int sig;
    while ( ( sig = sigtimedwait( &mask, NULL, &ts ) ) == -1 && errno == EINTR );

    // Check if SIGCHLD was received or timeout occurred
    if ( sig == SIGCHLD ) {
        // Child process finished within timeout
        int status;
        waitpid( process_id, &status, WNOHANG );
        return WEXITSTATUS( status );
    } else if ( sig == -1 && errno == ETIMEDOUT ) {
        // Timeout expired before child finished
        return -1;// Indicate timeout
    } else {
        // Error in sigtimedwait
        return -1;
    }
#endif
}

char *nxai_read_pipe_to_string( nxai_pipe_t pipe ) {
    char *out_string = (char *) malloc( 1024 );
    size_t total_bytes_read = 0;
    char buffer[1024];
#if defined( _MSC_VER )
    // Windows implementation
    DWORD bytes_read;
    while ( ( bytes_read = ReadFile( pipe, buffer, sizeof( buffer ), &bytes_read, NULL ) ) > 0 ) {
        out_string = realloc( out_string, total_bytes_read + bytes_read );
        memcpy( out_string + total_bytes_read, buffer, bytes_read );
        total_bytes_read += bytes_read;
    }
#else
    // Linux implementation
    ssize_t bytes_read;
    while ( ( bytes_read = read( pipe, buffer, sizeof( buffer ) ) ) > 0 ) {
        out_string = realloc( out_string, total_bytes_read + bytes_read );
        memcpy( out_string + total_bytes_read, buffer, bytes_read );
        total_bytes_read += bytes_read;
    }
#endif
    return out_string;
}

int nxai_kill_process( nxai_process_t process ) {
#if defined( _MSC_VER )
    // Windows implementation

    DWORD exitCode = 9;
    HANDLE hProcess = OpenProcess( PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION,
                                   FALSE, process );
    if ( hProcess != NULL ) {
        // Terminate the process
        nxai_vlog( "Sending termination signal...\n" );
        if ( !TerminateProcess( hProcess, 9 ) ) {
            CloseHandle( hProcess );
            return false;
        }

        // Wait for process to exit
        nxai_vlog( "Waiting for exit\n" );
        DWORD waitResult = WaitForSingleObject( hProcess, INFINITE );
        nxai_vlog( "Done waiting\n" );

        if ( waitResult == WAIT_OBJECT_0 ) {
            // Process terminated successfully
            nxai_vlog( "Exited succesfully\n" );
            CloseHandle( hProcess );
            return 0;
        }

        GetExitCodeProcess( hProcess, &exitCode );
        CloseHandle( hProcess );
    }
    return exitCode;
#else
    // Linux implementation
    return kill( process, SIGTERM );
#endif
}

int nxai_shutdown_process( nxai_process_t process ) {
#if defined( _MSC_VER )
    // Windows implementation
    // Get handle to process with full permissions
    HANDLE hProcess = OpenProcess( PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION,
                                   FALSE, process );

    if ( hProcess == NULL ) {
        return 0;// Process wasn't running
    }

    // Try graceful shutdown first (equivalent to SIGTERM)
    if ( !PostMessage( FindWindow( NULL, NULL ), WM_CLOSE, 0, 0 ) ) {
        // Fall back to force termination if windowless process
        TerminateProcess( hProcess, 0 );
    }

    DWORD status;
    GetExitCodeProcess( hProcess, &status );

#else
    // Linux implementation
    // Send SIGTERM to the process
    int result = kill( process, SIGTERM );
    if ( result == -1 ) {
        // Process wasn't running. Consider not running
        return 0;
    }

    // Wait for the child process to finish
    int status;
    waitpid( process, &status, 0 );

#endif
    nxai_vlog( "Prcess finished with status: %d\n", status );
    return status;
}

bool nxai_check_process_status( nxai_process_t process, int *status ) {
#if defined( _MSC_VER )
    // Windows implementation
    HANDLE hProcess = OpenProcess( PROCESS_QUERY_INFORMATION,
                                   FALSE, process );

    if ( hProcess == NULL ) {
        return false;
    }

    DWORD exitCode;
    if ( GetExitCodeProcess( hProcess, &exitCode ) ) {
        *status = exitCode;
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

void nxai_process_set_sigs( nxai_handler_return_t ( *handler )( nxai_signal_t ) ) {
#if defined( _MSC_VER )
    // Windows implementation
    // Set up console control handler
    SetConsoleCtrlHandler( handler, TRUE );

    // Windows doesn't have direct SIGPIPE equivalent
    // Instead, we'll handle write failures in the code where they occur
#else
    // Linux implementation
    signal( SIGINT, handler );
    signal( SIGTERM, handler );
    signal( SIGQUIT, handler );
    signal( SIGABRT, handler );

    // We expect write failures to occur but we want to handle them where
    // the error occurs rather than in a SIGPIPE handler.
    signal( SIGPIPE, SIG_IGN );

#endif
    // Set death signal when parent is terminated
    nxai_ensure_child_cleanup();
}