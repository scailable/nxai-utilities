#include "nxai_process_utils.h"

#include <errno.h>
#include <fcntl.h>
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

extern char** environ;

char *_start_log_filepath = NULL;
char *_rotating_log_filepath = NULL;
char *_log_prefix = NULL;
static uint64_t last_timestamp = 0;
size_t logfile_max_size_mb = 1;
static bool start_logfile_full = false;

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

void nxai_initialise_logging( const char *start_log_filepath, const char *rotating_log_filepath, const char *log_prefix ) {
    _start_log_filepath = strdup( start_log_filepath );
    _rotating_log_filepath = strdup( rotating_log_filepath );
    _log_prefix = strdup( log_prefix );
    // Create and clear log files
    FILE *logfile = fopen( _start_log_filepath, "w" );
    if ( logfile == NULL ) {
        printf( "Failed to initialise logfile: %s\n", _start_log_filepath );
    }
    fclose( logfile );
    chmod( _start_log_filepath, 0666 );
    logfile = fopen( _rotating_log_filepath, "w" );
    if ( logfile == NULL ) {
        printf( "Failed to initialise logfile: %s\n", _rotating_log_filepath );
    }
    fclose( logfile );
    chmod( _rotating_log_filepath, 0666 );
}

void nxai_finalise_logging() {
    free( _start_log_filepath );
    free( _rotating_log_filepath );
    free( _log_prefix );
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
    // Print to console
    printf( "%s%ld %09lld: ", _log_prefix, timestamp / 1000, (long long) duration );
    va_start( ap, fmt );
    vprintf( fmt, ap );
    va_end( ap );

    if ( _start_log_filepath == NULL || _rotating_log_filepath == NULL ) {
        return;
    }

    va_start( ap, fmt );
    FILE *flogfile = NULL;

    // Determine which file to log to
    if ( start_logfile_full == false ) {
        // Check if start logfile is full
        struct stat file_stat;
        if ( stat( _start_log_filepath, &file_stat ) < 0 ) {
            switch ( errno ) {
                case ENOENT:// No such file or directory
                    flogfile = fopen( _start_log_filepath, "w" );
                    printf( "Created log file: %s\n", _start_log_filepath );
                    break;
                case EACCES:// Permission denied
                    printf( "Permission denied trying to open logfile: %s.\n", _start_log_filepath );
                    break;
                default:
                    printf( "An unexpected error occurred accessing log file: %s %s\n", _start_log_filepath, strerror( errno ) );
                    break;
            }
        } else {
            size_t file_size = file_stat.st_size;
            if ( file_size < logfile_max_size_mb * 1000000 ) {
                // Write to start_log
                flogfile = fopen( _start_log_filepath, "a" );
                if ( flogfile == NULL ) {
                    printf( "Couldn't open logfile: %s\n", _start_log_filepath );
                }
            } else {
                start_logfile_full = true;
            }
        }
    }

    if ( flogfile == NULL ) {
        // Start logfile was full, open rotating logfile
        struct stat file_stat;
        if ( stat( _rotating_log_filepath, &file_stat ) < 0 ) {
            switch ( errno ) {
                case ENOENT:// No such file or directory
                    flogfile = fopen( _rotating_log_filepath, "w" );
                    printf( "Created log file: %s\n", _start_log_filepath );
                    break;
                case EACCES:// Permission denied
                    printf( "Permission denied trying to open logfile: %s.\n", _rotating_log_filepath );
                    break;
                default:
                    printf( "An unexpected error occurred accessing log file: %s %s\n", _rotating_log_filepath, strerror( errno ) );
                    break;
            }
        } else {
            size_t file_size = file_stat.st_size;
            if ( file_size > logfile_max_size_mb * 1000000 ) {
                // Rotating logfile is full, rename to ".old"
                size_t new_filepath_length = strlen( _rotating_log_filepath ) + 4 + 1;
                char *new_filepath = malloc( new_filepath_length );
                strcpy( new_filepath, _rotating_log_filepath );
                strcat( new_filepath, ".old" );
                rename( _rotating_log_filepath, new_filepath );
                free( new_filepath );
            }
            // Write to rotating log
            flogfile = fopen( _rotating_log_filepath, "a" );
            if ( flogfile == NULL ) {
                printf( "Couldn't open logfile: %s\n", _rotating_log_filepath );
                return;
            }
        }
    }

    // Write to logfile
    fprintf( flogfile, "%s%ld %09lld: ", _log_prefix, timestamp / 1000, (long long) duration );
    va_start( ap, fmt );
    vfprintf( flogfile, fmt, ap );
    va_end( ap );
    fclose( flogfile );
}

pid_t nxai_start_process( char *const argv[] ) {
    pid_t child_pid;

    // Initialize file actions and attributes objects
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attrp;
    posix_spawn_file_actions_init( &file_actions );
    posix_spawnattr_init( &attrp );

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