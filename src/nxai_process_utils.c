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
#include <strings.h>
#include <time.h>
#include <unistd.h>

#if defined( __WIN32__ )
// Windows specific imports
#include <handleapi.h>
#include <ioapiset.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <windows.h>
#else
// Linux specific imports
#include <spawn.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
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
pthread_mutex_t rotating_logfile_lock = PTHREAD_MUTEX_INITIALIZER;

static void nxai_vvlog( const char *fmt, va_list *args );

uint64_t nxai_current_timestamp_ms() {
#if defined( __WIN32__ )
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
#if defined( __WIN32__ )
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
        chmod( _start_log_filepath, 0666 );
        rotating_logfile = fopen( _rotating_log_filepath, "w" );
        if ( rotating_logfile == NULL ) {
            printf( "Failed to initialise logfile: %s\n", _rotating_log_filepath );
        }
        chmod( _rotating_log_filepath, 0666 );
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

void nxai_vlog( const char *fmt, ... ) {
    va_list args;
    va_start( args, fmt );
    nxai_vvlog( fmt, &args );
    va_end( args );
}

bool nxai_get_file_size( const char *filepath, size_t *file_size ) {
#if defined( __WIN32__ )
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
        pthread_mutex_lock( &rotating_logfile_lock );
        if ( (size_t) logfile_last_size > logfile_max_size_mb * 1000000 ) {
            // Rotating logfile is full, rename to ".old"
            if ( rotating_logfile != NULL ) {
                fclose( rotating_logfile );
                rotating_logfile = NULL;
                int result = rename( _rotating_log_filepath, _old_logfile_path );
                if ( result != 0 ) {
                    perror( "Error renaming file" );
                    pthread_mutex_unlock( &rotating_logfile_lock );
                    return;
                }
            }
            // Create new log file
            rotating_logfile = fopen( _rotating_log_filepath, "w" );
            if ( rotating_logfile == NULL ) {
                perror( "Error creating log file" );
                pthread_mutex_unlock( &rotating_logfile_lock );
                return;
            }
            logfile_last_size = 0;
        }
        pthread_mutex_unlock( &rotating_logfile_lock );
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

nxai_process_t nxai_start_process( char *const argv[], bool connect_console, nxai_pipe_t *stderr_pipe ) {
#if defined( __WIN32__ )
    // Windows implementation
    // Create pipe for stderr redirection
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof( SECURITY_ATTRIBUTES );
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    if ( !CreatePipe( &hReadPipe, &hWritePipe, &saAttr, 0 ) ) {
        return INVALID_HANDLE_VALUE;
    }

    // Set read end to non-blocking mode
    DWORD dwFlagsAndAttributes = FILE_FLAG_OVERLAPPED;
    if ( !SetNamedPipeHandleState( hReadPipe, &dwFlagsAndAttributes, NULL, NULL ) ) {
        CloseHandle( hReadPipe );
        CloseHandle( hWritePipe );
        return INVALID_HANDLE_VALUE;
    }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory( &si, sizeof( si ) );
    ZeroMemory( &pi, sizeof( pi ) );

    si.cb = sizeof( si );

    if ( !connect_console ) {
        // Redirect stdout to NUL (Windows equivalent of /dev/null)
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = INVALID_HANDLE_VALUE;
    }

    // Setup stderr redirection
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdError = hWritePipe;

    // Create the process
    if ( !CreateProcessW( NULL, (wchar_t *) ( argv[0] ),
                          NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi ) ) {
        CloseHandle( hReadPipe );
        CloseHandle( hWritePipe );
        return INVALID_HANDLE_VALUE;
    }

    // Cleanup
    CloseHandle( hWritePipe );// Child inherits this handle
    *stderr_pipe = hReadPipe;
    CloseHandle( pi.hThread );

    return pi.hProcess;
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

int waitpid_timeout( pid_t process_id, int timeout_seconds ) {
#if defined( __WIN32__ )
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
        kill( process_id, SIGKILL );// Kill the child process
        return -1;                  // Indicate timeout
    } else {
        // Error in sigtimedwait
        return -1;
    }
#endif
}