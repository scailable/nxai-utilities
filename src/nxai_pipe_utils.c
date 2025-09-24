#include "nxai_pipe_utils.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NXAI_DEBUG
#include "memory_leak_detector.h"
#endif

#if defined( _MSC_VER )
// Windows stuff
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include "winsock2.h"
#include <errno.h>
#include <basetsd.h>
typedef SSIZE_T ssize_t;
static volatile long PipeSerialNumber;
#else
// Linux stuff
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#endif

#include "nxai_process_utils.h"
#include "nxai_utils.h"

char *nxai_pipe_to_string( nxai_pipe_t pipe ) {
#if defined( _MSC_VER )
    // Windows implementation
    char *pipe_string = nxai_pointer_to_string( pipe->handle );
#else
    // Linux implementation
    char *pipe_string = nxai_sprintf( 32, "%d", pipe );
#endif
    return pipe_string;
}

nxai_pipe_t nxai_string_to_pipe( const char *str ) {
#if defined( _MSC_VER )
    // Windows implementation
    HANDLE hPipe = INVALID_HANDLE_VALUE;

    // Parse hex string representation of handle
    sscanf_s( str, "%p", &hPipe );

    // Validate the handle
    nxai_pipe_t return_pipe = malloc( sizeof( _nxai_pipe_t ) );
    *return_pipe = (_nxai_pipe_t) NXAI_PIPE_INITIALIZER;
    DWORD dflags;
    if ( hPipe == INVALID_HANDLE_VALUE || !GetHandleInformation( hPipe, &dflags ) ) {
        return return_pipe;
    }

    return_pipe->handle = hPipe;

    return return_pipe;
#else
    // Linux implementation
    int pipe_fd;

    // Convert string to integer
    char *endptr;
    pipe_fd = strtol( str, &endptr, 10 );

    // Validate the conversion
    if ( *endptr != '\0' || pipe_fd < 0 ) {
        return -1;
    }

    // Verify it's a valid pipe
    struct stat sb;
    if ( fstat( pipe_fd, &sb ) == -1 || !S_ISFIFO( sb.st_mode ) ) {
        return -1;
    }

    return pipe_fd;
#endif
}

nxai_pipe_t nxai_pipe_get_write_pipe( bidirectional_pipe_t pipe, PIPE_DIRECTION direction ) {
    return direction == UP ? pipe.up_pipe[1] : pipe.down_pipe[1];
}

nxai_pipe_t nxai_pipe_get_read_pipe( bidirectional_pipe_t pipe, PIPE_DIRECTION direction ) {
    nxai_pipe_t p = direction == UP ? pipe.up_pipe[0] : pipe.down_pipe[0];
    return direction == UP ? pipe.up_pipe[0] : pipe.down_pipe[0];
}

bidirectional_pipe_t nxai_initialize_pipe( nxai_pipe_t up_pipe_read, nxai_pipe_t up_pipe_write, nxai_pipe_t down_pipe_read, nxai_pipe_t down_pipe_write ) {
    bidirectional_pipe_t created_pipe = { { up_pipe_read, up_pipe_write }, { down_pipe_read, down_pipe_write } };
    return created_pipe;
}

#if defined( _MSC_VER )
nxai_pipe_t nxai_create_empty_pipe() {
    nxai_pipe_t new_pipe = malloc( sizeof( _nxai_pipe_t ) );
    *new_pipe = (_nxai_pipe_t) NXAI_PIPE_INITIALIZER;
    return new_pipe;
}

bool nxai_create_pipe_handles( HANDLE *read_handle, HANDLE *write_handle ) {
    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof( SECURITY_ATTRIBUTES );
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;
    // Create pipe name
    UCHAR PipeNameBuffer[MAX_PATH];
    sprintf( PipeNameBuffer,
             "\\\\.\\Pipe\\NXAI_MODULE_PIPE.%08x.%08x",
             GetCurrentProcessId(),
             InterlockedIncrement( &PipeSerialNumber ) );
    HANDLE ReadPipeHandle = CreateNamedPipeA(
            PipeNameBuffer,
            PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_WAIT,
            1,  // Number of pipes
            512,// Out buffer size
            512,// In buffer size
            0,  // Timeout in ms
            &saAttr );
    if ( !ReadPipeHandle ) {
        return false;
    }

    HANDLE WritePipeHandle = CreateFileA(
            PipeNameBuffer,
            GENERIC_WRITE,
            0,// No sharing
            &saAttr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL// Template file
    );

    *read_handle = ReadPipeHandle;
    *write_handle = WritePipeHandle;

    return true;
}

#endif

bidirectional_pipe_t nxai_create_pipe( int *error ) {
#if defined( _MSC_VER )
    // Windows implementation
    typedef struct {
        OVERLAPPED overlap;
        HANDLE event;
        HANDLE pipe;
        char read_buffer;
        bool active;
    } nxai_pipe_t;
    bidirectional_pipe_t created_pipe = { { nxai_create_empty_pipe(), nxai_create_empty_pipe() },
                                          { nxai_create_empty_pipe(), nxai_create_empty_pipe() } };

    // Create UP pipe
    bool success = nxai_create_pipe_handles( &( created_pipe.up_pipe[0]->handle ), &( created_pipe.up_pipe[1]->handle ) );
    if ( success == false ) {
        nxai_vlog( "Error! Couldn't create up pipe handles!\n" );
        *error = -2;
        return created_pipe;
    }

    // Create DOWN pipe
    success = nxai_create_pipe_handles( &( created_pipe.down_pipe[0]->handle ), &( created_pipe.down_pipe[1]->handle ) );
    if ( success == false ) {
        nxai_vlog( "Error! Couldn't create up pipe handles!\n" );
        *error = -2;
        return created_pipe;
    }

    *error = 0;
    return created_pipe;
#else
    // Linux implementation
    bidirectional_pipe_t created_pipe = { { -1, -1 }, { -1, -1 } };

    // Create up pipe
    if ( pipe( created_pipe.up_pipe ) == -1 ) {
        *error = -1;
        return created_pipe;
    }

    // Create down pipe
    if ( pipe( created_pipe.down_pipe ) == -1 ) {
        close( created_pipe.up_pipe[0] );
        close( created_pipe.up_pipe[1] );
        *error = -2;
        return created_pipe;
    }

    *error = 0;
    return created_pipe;
#endif
}

size_t nxai_pipe_timed_read_any( bidirectional_pipe_t *pipes_array, size_t pipes_length, PIPE_DIRECTION direction, int8_t *return_byte ) {
    const int timeout_ms = 1000;

#if defined( _MSC_VER )
    // Windows implementation

    // Validate parameters
    if ( !pipes_array || !pipes_length || !return_byte ) {
        raise( SIGABRT );
        return pipes_length;
    }

    HANDLE *handles = (HANDLE *) malloc( sizeof( HANDLE ) * pipes_length );

    // Initialize OVERLAPPED structures
    for ( size_t pipe_index = 0; pipe_index < pipes_length; pipe_index++ ) {
        nxai_pipe_t read_pipe = nxai_pipe_get_read_pipe( pipes_array[pipe_index], direction );
        if ( read_pipe->active == true ) {
            handles[pipe_index] = read_pipe->overlap.hEvent;
            continue;// Already initialized
        }
        ZeroMemory( &( read_pipe->overlap ), sizeof( OVERLAPPED ) );
        read_pipe->overlap.hEvent = CreateEventA( NULL, TRUE, FALSE, NULL );
        handles[pipe_index] = read_pipe->overlap.hEvent;

        // Start overlapped read operation
        DWORD bytesRead;
        if ( !ReadFile( read_pipe->handle, &( read_pipe->read_buffer ), 1, &bytesRead, &( read_pipe->overlap ) ) ) {
            DWORD lastError = GetLastError();
            if ( lastError != ERROR_IO_PENDING ) {
                char error_string[1024];
                get_windows_error( lastError, error_string, 1024 );
                nxai_vlog( "Could not read pipe: %s\n", error_string );
                // Cleanup events
                for ( size_t j = 0; j <= pipe_index; j++ ) {
                    CloseHandle( nxai_pipe_get_read_pipe( pipes_array[j], direction )->overlap.hEvent );
                }
                free( handles );
                raise( SIGABRT );
                return pipes_length;
            }
        }
        // Signal that pipe is waiting for read operation
        read_pipe->active = true;
    }

    // Wait for any operation to complete
    DWORD result = WaitForMultipleObjects( pipes_length,
                                           handles,
                                           FALSE,
                                           timeout_ms );

    size_t completed_index = pipes_length;

    if ( result == WAIT_TIMEOUT ) {
        // Handle timeout case
        nxai_vlog_verbose( "Timed out waiting for pipe read.\n" );
    } else if ( result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + pipes_length ) {
        completed_index = result - WAIT_OBJECT_0;
        //Signal no longer waiting for result on this pipe
        nxai_vlog( "Completed: %zu\n", completed_index );
        nxai_pipe_t completed_pipe = nxai_pipe_get_read_pipe( pipes_array[completed_index], direction );
        completed_pipe->active = false;
        // Get the result of the completed operation
        DWORD bytesRead = 0;
        nxai_vlog( "Getting overlapped: \n" );
        if ( GetOverlappedResult( completed_pipe->handle,
                                  &( completed_pipe->overlap ),
                                  &bytesRead,
                                  TRUE ) ) {
            nxai_vlog( "Bytes read: %zu %d\n", bytesRead, completed_pipe->read_buffer );
            if ( bytesRead > 0 ) {
                *return_byte = completed_pipe->read_buffer;
            }
        }
        CloseHandle( completed_pipe->overlap.hEvent );
    } else {
        char error_string[1024];
        get_windows_error( GetLastError(), error_string, 1024 );
        nxai_vlog( "Error! Failed waiting for any pipe: %s\n", error_string );
        raise( SIGABRT );
    }

    // Cleanup
    free( handles );
    return completed_index;

#else
    // Linux implementation

    // Gather pipes into single array
    struct pollfd *poll_fds = (struct pollfd *) malloc( sizeof( struct pollfd ) * pipes_length );
    for ( size_t index = 0; index < pipes_length; index++ ) {
        poll_fds[index].fd = nxai_pipe_get_read_pipe( pipes_array[index], direction );
        poll_fds[index].events = POLLIN;// Monitor for input
    }

    // Wait for any pipe to write data
    nxai_vlog( "Waiting for output from any pipe.\n" );
    int ready = poll( poll_fds, pipes_length, timeout_ms );

    if ( ready < 0 ) {
        free( poll_fds );
        if ( errno != EINTR ) {
            // Error is something other than receiving interrupt signal. Raise error
            nxai_error_log( "\nError! Could not poll pipe! %d %s", ready, strerror( errno ) );
            raise( SIGABRT );
        }
        return pipes_length;
    }

    if ( ready == 0 ) {
        nxai_vlog( "Timed out waiting for for pipe read.\n" );
        free( poll_fds );
        return pipes_length;
    }

    // Determine which pipe signalled
    size_t index;
    for ( index = 0; index < pipes_length; index++ ) {
        if ( poll_fds[index].revents & POLLIN ) {
            // Pipe has data to read. Read byte and clear flag
            int8_t read_byte = (int8_t) nxai_pipe_read( pipes_array[index], direction );
            if ( read_byte == -1 ) {
                nxai_error_log( "\nError! Could not read from pipe %zu", index );
                raise( SIGABRT );
                free( poll_fds );
                return pipes_length;
            }
            poll_fds[index].events &= ~POLLIN;
            // Return the inference ID received from sclbld
            *return_byte = read_byte;
            break;
        } else if ( poll_fds[index].revents & POLLERR || poll_fds[index].revents & POLLHUP ) {
            nxai_error_log( "\nError! Failed to communicate with pipe %zu", index );
            close( poll_fds[index].fd );
            poll_fds[index].fd = -1;
            raise( SIGABRT );
            free( poll_fds );
            return pipes_length;
        }
    }

    free( poll_fds );

    return index;
#endif
}

char nxai_pipe_read( bidirectional_pipe_t pipe_fd, PIPE_DIRECTION direction ) {
#if defined( _MSC_VER )
    // Windows implementation
    char buffer;
    DWORD bytes_read;
    if ( !ReadFile( nxai_pipe_get_read_pipe( pipe_fd, direction )->handle, &buffer, 1, &bytes_read, NULL ) ) {
        return -1;
    }
    if ( bytes_read == 0 ) {
        return -4;
    }
    return buffer;
#else
    // Linux implementation
    char buffer;
    ssize_t bytes_read;

    bytes_read = read( nxai_pipe_get_read_pipe( pipe_fd, direction ), &buffer, 1 );
    if ( bytes_read == -1 ) {
        return -1;
    }
    if ( bytes_read == 0 ) {
        // No bytes read, possibly pipe closed
        return -4;
    }

    return buffer;
#endif
}

ssize_t nxai_pipe_send( bidirectional_pipe_t pipe_fd, PIPE_DIRECTION direction, char signal ) {
#if defined( _MSC_VER )
    // Windows implementation
    DWORD bytes_written;
    if ( !WriteFile( nxai_pipe_get_write_pipe( pipe_fd, direction )->handle, &signal, 1, &bytes_written, NULL ) ) {
        return -1;
    }
    return bytes_written;
#else
    // Linux implementation
    return write( nxai_pipe_get_write_pipe( pipe_fd, direction ), &signal, 1 );
#endif
}

char nxai_pipe_timed_read( bidirectional_pipe_t pipe_fd, PIPE_DIRECTION direction, int timeout_s ) {
#if defined( _MSC_VER )
    // Windows implementation
    char buffer;
    DWORD bytes_read;
    OVERLAPPED overlapped = { 0 };
    HANDLE pipe_handle = nxai_pipe_get_read_pipe( pipe_fd, direction )->handle;

    // Initialize overlapped structure
    overlapped.hEvent = CreateEventA( NULL, TRUE, FALSE, NULL );
    if ( !overlapped.hEvent ) {
        nxai_vlog( "Could not create event!\n" );
        return -1;
    }

    // Start overlapped read operation
    if ( !ReadFile( pipe_handle, &buffer, 1, &bytes_read, &overlapped ) ) {
        DWORD last_error = GetLastError();
        if ( last_error != ERROR_IO_PENDING ) {
            CloseHandle( overlapped.hEvent );
            nxai_vlog( "Could not read from pipe!\n" );
            return -1;
        }
    }

    // Wait for completion with timeout_s
    DWORD wait_result = WaitForSingleObject( overlapped.hEvent, timeout_s * 1000 );// Convert to ms

    switch ( wait_result ) {
        case WAIT_OBJECT_0:
            // Operation completed successfully
            if ( !GetOverlappedResult( pipe_handle, &overlapped, &bytes_read, FALSE ) ) {
                CloseHandle( overlapped.hEvent );
                nxai_vlog( "GetOverlappedResult failed!\n" );
                return -1;
            }
            if ( bytes_read > 0 ) {
                CloseHandle( overlapped.hEvent );
                return buffer;
            }
            break;

        case WAIT_TIMEOUT:
            // Cancel pending operation
            CancelIo( pipe_handle );
            CloseHandle( overlapped.hEvent );
            return -3;// Timeout

        default:
            // Other wait errors
            CloseHandle( overlapped.hEvent );
            nxai_vlog( "WaitForSingleObject failed!\n" );
            return -1;
    }

    CloseHandle( overlapped.hEvent );
    return -3;// No data available
#else
    // Linux implementation
    nxai_pipe_t read_pipe = nxai_pipe_get_read_pipe( pipe_fd, direction );

    char buffer;
    fd_set read_fds;
    struct timeval tv;
    ssize_t bytes_read;

    // Set up select parameters
    FD_ZERO( &read_fds );
    FD_SET( read_pipe, &read_fds );
    tv.tv_sec = timeout_s;
    tv.tv_usec = 0;

    // Wait for data or timeout_s
    int select_return = select( read_pipe + 1, &read_fds, NULL, NULL, &tv );
    if ( select_return <= 0 ) {
        return -3;
    }

    // Read data if available
    bytes_read = read( read_pipe, &buffer, 1 );
    if ( bytes_read == -1 ) {
        nxai_error_log( "Error in read function during pipe timed read: %s\n", strerror( errno ) );
        return -1;
    }
    if ( bytes_read == 0 ) {
        // No bytes read, possibly pipe closed. Return
        return -4;
    }
    return buffer;
#endif
}

void nxai_pipe_close( bidirectional_pipe_t pipe, PIPE_DIRECTION direction ) {
#if defined( _MSC_VER )
    // Windows implementation
    if ( direction == DOWN ) {
        CloseHandle( nxai_pipe_get_write_pipe( pipe, UP )->handle );
        CloseHandle( nxai_pipe_get_read_pipe( pipe, DOWN )->handle );
    } else {
        CloseHandle( nxai_pipe_get_write_pipe( pipe, DOWN )->handle );
        CloseHandle( nxai_pipe_get_read_pipe( pipe, UP )->handle );
    }
#else
    // Linux implementation

    if ( direction == DOWN ) {
        // Close writing up and reading down
        close( nxai_pipe_get_write_pipe( pipe, UP ) );
        close( nxai_pipe_get_read_pipe( pipe, DOWN ) );
    } else {
        // Close writing down and reading up
        close( nxai_pipe_get_write_pipe( pipe, DOWN ) );
        close( nxai_pipe_get_read_pipe( pipe, UP ) );
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