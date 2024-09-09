#include "nxai_process_utils.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdatomic.h>
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
char *_log_prefix = NULL;
static uint64_t last_timestamp = 0;
size_t logfile_max_size_mb = 10;
static bool start_logfile_full = false;
static atomic_int logfile_last_size = -1;
static bool _log_to_console = false;
FILE *start_logfile;
FILE *rotating_logfile;
pthread_mutex_t rotating_logfile_lock = PTHREAD_MUTEX_INITIALIZER;

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

void nxai_initialise_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix, bool log_to_console ) {
    _start_log_filepath = strdup( start_log_filepath );
    _rotating_log_filepath = strdup( rotating_log_filepath );
    _log_prefix = strdup( log_prefix );
    _log_to_console = log_to_console;
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
}

void nxai_vlog( const char *fmt, ... ) {
    va_list ap;

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
        va_start( ap, fmt );
        vprintf( fmt, ap );
        va_end( ap );
    }

    if ( _start_log_filepath == NULL || _rotating_log_filepath == NULL ) {
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
        if ( logfile_last_size > logfile_max_size_mb * 1000000 ) {
            pthread_mutex_lock( &rotating_logfile_lock );
            // Check condition again after acquiring lock
            if ( logfile_last_size > logfile_max_size_mb * 1000000 ) {
                // Rotating logfile is full, rename to ".old"
                fclose( rotating_logfile );
                size_t new_filepath_length = strlen( _rotating_log_filepath ) + 4 + 1;
                char *new_filepath = malloc( new_filepath_length );
                strcpy( new_filepath, _rotating_log_filepath );
                strcat( new_filepath, ".old" );
                rename( _rotating_log_filepath, new_filepath );
                free( new_filepath );
                // Create new log file
                rotating_logfile = fopen( _rotating_log_filepath, "w" );
                logfile_last_size = 0;
            }
            pthread_mutex_unlock( &rotating_logfile_lock );
        }
        // Write to rotating log
        flogfile = rotating_logfile;
    }

    // Write to logfile
    int bytes_written = fprintf( flogfile, "%s%ld %09lld: ", _log_prefix, timestamp / 1000, (long long) duration );
    if ( bytes_written < 0 ) {
        printf( "Failed to write to log file!\n" );
        return;
    }
    __atomic_fetch_add( &logfile_last_size, bytes_written, __ATOMIC_SEQ_CST );
    va_start( ap, fmt );
    bytes_written = vfprintf( flogfile, fmt, ap );
    va_end( ap );
    if ( bytes_written < 0 ) {
        printf( "Failed to write to log file!\n" );
        return;
    }
    __atomic_fetch_add( &logfile_last_size, bytes_written, __ATOMIC_SEQ_CST );
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