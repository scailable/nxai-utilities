#include "nxai_process_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

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
static int logfile_last_size = -1;
static bool _log_to_console = false;
static bool _log_to_file = true;
static int _log_verbosity_level = 1;
FILE *start_logfile;
FILE *rotating_logfile;
pthread_mutex_t rotating_logfile_lock = PTHREAD_MUTEX_INITIALIZER;

static void nxai_vvlog( const char *fmt, va_list *args );

uint64_t nxai_current_timestamp_ms() {
    struct timeval te;
    gettimeofday( &te, NULL );                                    // get current time
    int64_t milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000;// calculate milliseconds
    return milliseconds;
}

uint64_t nxai_current_timestamp_us() {
    struct timeval te;
    gettimeofday( &te, NULL );// get current time
    int64_t microseconds = te.tv_sec * 1000000LL + te.tv_usec;
    return microseconds;
}

void nxai_initialise_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix, bool log_to_console, bool log_to_file, int log_verbosity_level ) {
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
#ifndef NXAI_DEBUG
    free( _start_log_filepath );
    free( _rotating_log_filepath );
    free( _log_prefix );
#endif
    if ( start_logfile_full == false ) {
        fclose( start_logfile );
    }
    fclose( rotating_logfile );
    free( _old_logfile_path );
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
        struct stat file_stat;
        if ( logfile_last_size == -1 ) {
            if ( stat( _start_log_filepath, &file_stat ) < 0 ) {
                switch ( errno ) {
                    case EACCES:// Permission denied
                        printf( "Permission denied trying to open logfile: %s.\n", _start_log_filepath );
                        break;
                    default:
                        printf( "An unexpected error occurred accessing log file: %s %s\n", _start_log_filepath, strerror( errno ) );
                        break;
                }
                return;
            }
            logfile_last_size = file_stat.st_size;
        }
        // Check if start logfile is full
        if ( logfile_last_size < logfile_max_size_mb * 1000000 ) {
            // Write to start_log
            flogfile = start_logfile;
        } else {
            start_logfile_full = true;
            logfile_last_size = -1;
            fclose( start_logfile );
        }
    }

    if ( flogfile == NULL ) {
        // Start logfile was full, open rotating logfile
        struct stat file_stat;
        if ( logfile_last_size == -1 ) {
            if ( stat( _rotating_log_filepath, &file_stat ) < 0 ) {
                switch ( errno ) {
                    case EACCES:// Permission denied
                        printf( "Permission denied trying to open logfile: %s.\n", _rotating_log_filepath );
                        break;
                    default:
                        printf( "An unexpected error occurred accessing log file: %s %s\n", _rotating_log_filepath, strerror( errno ) );
                        break;
                }
                return;
            }
            logfile_last_size = file_stat.st_size;
        }
        pthread_mutex_lock( &rotating_logfile_lock );
        if ( logfile_last_size > logfile_max_size_mb * 1000000 ) {
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
#ifdef NXAI_DEBUG
    fflush( flogfile );// Flush writing file to make sure latest prints are logged
#endif
}

pid_t nxai_start_process( char *const argv[], bool connect_console ) {
    pid_t child_pid;

    // Initialize file actions and attributes objects
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attrp;
    posix_spawn_file_actions_init( &file_actions );
    posix_spawnattr_init( &attrp );

    if ( connect_console == false ) {
        // Redirect stdout to /dev/null
        posix_spawn_file_actions_adddup2( &file_actions, open( "/dev/null", O_WRONLY ), STDOUT_FILENO );
    }

    // Spawn a new process
    if ( posix_spawn( &child_pid, argv[0], &file_actions, &attrp, argv, environ ) != 0 ) {
        // Could not start
        return 1;
    }

    // Cleanup
    posix_spawn_file_actions_destroy( &file_actions );
    posix_spawnattr_destroy( &attrp );

    return child_pid;
}