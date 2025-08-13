#include "nxai_shm_utils.h"

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

#ifdef __MUSL__
// musl crosscompiler doesn't find time.h otherwise
#include "musl_time.h"
#else
#include <time.h>
#endif

#if defined( _MSC_VER )
// Windows stuff
#include "winsock2.h"
#include "windows.h"
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#else
// Linux stuff
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <unistd.h>
#include <poll.h>
#endif

#include "nxai_process_utils.h"

// SHM stuff
#include <fcntl.h>

#define HEADER_BYTES 4

char *nxai_shm_key_to_string( nxai_shm_t shm ) {
#if defined( _MSC_VER )
    // Windows implementation
    // Copy string so it can be freed
    char *shm_string = malloc( strlen( shm.key ) );
#else
    // Linux implementation
    char *shm_string = nxai_sprintf( 32, "%d", shm.key );
#endif
    return shm_string;
}

void nxai_shm_string_to_key( nxai_shm_t *result, const char *str ) {
#if defined( _MSC_VER )
    // Windows implementation
    // Convert string to integer using atoi
    strcpy( result->key, str );
#else
    // Linux implementation
    // Convert string to integer using strtol for better error handling
    char *endptr;
    errno = 0;
    result->key = strtol( str, &endptr, 10 );

    // Check for errors
    if ( errno == ERANGE || *endptr != '\0' ) {
        // Handle conversion error
        result->key = 0;
    }
#endif
}

char *nxai_shm_id_to_string( nxai_shm_t shm ) {
#if defined( _MSC_VER )
    // Windows implementation
    // Copy string so it can be freed
    char *id_string = malloc( 20 );
    sprintf( id_string, L"%p", shm.id );
#else
    // Linux implementation
    char *id_string = nxai_sprintf( 32, "%d", shm.id );
#endif
    return id_string;
}

nxai_shm_t nxai_shm_id_from_string( const char *str ) {
    nxai_shm_t shm;

#if defined( _MSC_VER )
    // Windows implementation
    sscanf( str, "%p", &shm.id );
#else
    // Linux implementation
    shm.id = atoi( str );
#endif

    return shm;
}

char *nxai_sprintf( size_t initial_size, char *fmt, ... ) {
    // Initial allocation
    char *return_string = malloc( initial_size );
    if ( !return_string ) {
        return NULL;
    }

    va_list args;
    va_start( args, fmt );

    // First attempt to format string
    size_t len = vsnprintf( return_string, initial_size, fmt, args );
    va_end( args );

    // Check if buffer was too small
    if ( len >= initial_size ) {
        // Need larger buffer
        return_string = realloc( return_string, len + 1 );
        if ( !return_string ) {
            return NULL;
        }

        // Restart va_list for second formatting attempt
        va_start( args, fmt );

        // Format string again with larger buffer
        vsnprintf( return_string, len + 1, fmt, args );
        va_end( args );
    }

    return return_string;
}

char *nxai_pointer_to_string( void *pointer ) {
    return nxai_sprintf( 32, "%p", pointer );// 32 bytes is typically enough for pointers
}

char *nxai_pipe_to_string( nxai_pipe_t pipe ) {
#if defined( _MSC_VER )
    // Windows implementation
    char *pipe_string = nxai_pointer_to_string( pipe );
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
    DWORD dflags;
    if ( hPipe == INVALID_HANDLE_VALUE || !GetHandleInformation( hPipe, &dflags ) ) {
        return INVALID_HANDLE_VALUE;
    }

    return hPipe;
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
    return direction == UP ? pipe.up_pipe[0] : pipe.down_pipe[0];
}

bidirectional_pipe_t nxai_initialize_pipe( nxai_pipe_t up_pipe_read, nxai_pipe_t up_pipe_write, nxai_pipe_t down_pipe_read, nxai_pipe_t down_pipe_write ) {
    bidirectional_pipe_t created_pipe = { { up_pipe_read, up_pipe_write }, { down_pipe_read, down_pipe_write } };
    return created_pipe;
}

bidirectional_pipe_t nxai_create_pipe( int *error ) {
#if defined( _MSC_VER )
    // Windows implementation
    bidirectional_pipe_t created_pipe = { { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE },
                                          { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE } };

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof( SECURITY_ATTRIBUTES );
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    // Create up pipe
    if ( !CreatePipe( &( created_pipe.up_pipe[0] ),
                      &( created_pipe.up_pipe[1] ),
                      &saAttr,
                      0 ) ) {
        *error = -1;
        return created_pipe;
    }

    // Create down pipe
    if ( !CreatePipe( &( created_pipe.down_pipe[0] ),
                      &( created_pipe.down_pipe[1] ),
                      &saAttr,
                      0 ) ) {
        CloseHandle( created_pipe.up_pipe[0] );
        CloseHandle( created_pipe.up_pipe[1] );
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

size_t nxai_pipe_poll( bidirectional_pipe_t *pipes_array, size_t pipes_length, PIPE_DIRECTION direction, int8_t *return_byte ) {
    const int timeout_ms = 1000;

#if defined( _MSC_VER )
    // Windows implementation
    // Allocate array for OVERLAPPED structures
    OVERLAPPED *overlapped_array = (OVERLAPPED *) malloc( sizeof( OVERLAPPED ) * pipes_length );

    // Initialize OVERLAPPED structures
    for ( size_t index = 0; index < pipes_length; index++ ) {
        ZeroMemory( &overlapped_array[index], sizeof( OVERLAPPED ) );
        overlapped_array[index].hEvent = CreateEvent( NULL, TRUE, FALSE, NULL );
        if ( overlapped_array[index].hEvent == NULL ) {
            // Clean up previously allocated events
            for ( size_t i = 0; i < index; i++ ) {
                CloseHandle( overlapped_array[i].hEvent );
            }
            free( overlapped_array );
            return pipes_length;
        }
    }

    nxai_vlog( "Waiting for output from any inference engine.\n" );

    // Wait for any pipe to have data
    DWORD wait_result = WaitForMultipleObjects(
            pipes_length,
            (HANDLE *) overlapped_array,
            FALSE,// Don't wait for all events
            timeout_ms );

    if ( wait_result == WAIT_FAILED ) {
        DWORD last_error = GetLastError();
        nxai_vlog( "Error! Could not poll inference engine pipe! %d\n", last_error );
        fprintf( stderr, "\nError! Could not poll inference engine pipe! %d", last_error );
        raise( SIGABRT );
        free( overlapped_array );
        return pipes_length;
    }

    if ( wait_result == WAIT_TIMEOUT ) {
        nxai_vlog( "Timed out waiting for client output\n" );
        free( overlapped_array );
        return pipes_length;
    }

    // Determine which inference engine signalled
    size_t index;
    for ( index = 0; index < pipes_length; index++ ) {
        if ( wait_result == WAIT_OBJECT_0 + index ) {
            // Pipe has data to read
            DWORD bytesRead;
            int8_t read_byte;
            if ( !ReadFile(
                         nxai_pipe_get_read_pipe( pipes_array[index], direction ),
                         &read_byte,
                         sizeof( read_byte ),
                         &bytesRead,
                         &overlapped_array[index] ) ) {
                if ( GetLastError() != ERROR_IO_PENDING ) {
                    nxai_vlog( "Error! Could not read from pipe %zu\n", index );
                    fprintf( stderr, "\nError! Could not read from pipe %zu", index );
                    raise( SIGABRT );
                    free( overlapped_array );
                    return pipes_length;
                }
            }
            *return_byte = read_byte;
            break;
        }
    }

    // Clean up
    for ( size_t i = 0; i < pipes_length; i++ ) {
        CloseHandle( overlapped_array[i].hEvent );
    }
    free( overlapped_array );
#else
    // Linux implementation

    // Gather pipes into single array
    struct pollfd *poll_fds = (struct pollfd *) malloc( sizeof( struct pollfd ) * pipes_length );
    for ( size_t index = 0; index < pipes_length; index++ ) {
        poll_fds[index].fd = nxai_pipe_get_read_pipe( pipes_array[index], direction );
        poll_fds[index].events = POLLIN;// Monitor for input
    }

    // Wait for any pipe to write data
    nxai_vlog( "Waiting for output from any inference engine.\n" );
    int ready = poll( poll_fds, pipes_length, timeout_ms );

    if ( ready < 0 ) {
        free( poll_fds );
        if ( errno != EINTR ) {
            // Error is something other than receiving interrupt signal. Raise error
            nxai_vlog( "Error! Could not poll inference engine pipe! %d %s\n", ready, strerror( errno ) );
            fprintf( stderr, "\nError! Could not poll inference engine pipe! %d %s", ready, strerror( errno ) );
            raise( SIGABRT );
        }
        return pipes_length;
    }

    if ( ready == 0 ) {
        nxai_vlog( "Timed out waiting for client output\n" );
        free( poll_fds );
        return pipes_length;
    }

    // Determine which inference engine signalled
    size_t index;
    for ( index = 0; index < pipes_length; index++ ) {
        if ( poll_fds[index].revents & POLLIN ) {
            // Pipe has data to read. Read byte and clear flag
            int8_t read_byte = (int8_t) nxai_pipe_read( pipes_array[index], direction );
            if ( read_byte == -1 ) {
                nxai_vlog( "Error! Could not read from pipe %zu\n", index );
                fprintf( stderr, "\nError! Could not read from pipe %zu", index );
                raise( SIGABRT );
                free( poll_fds );
                return pipes_length;
            }
            poll_fds[index].events &= ~POLLIN;
            // Return the inference ID received from sclbld
            *return_byte = read_byte;
            break;
        } else if ( poll_fds[index].revents & POLLERR || poll_fds[index].revents & POLLHUP ) {
            nxai_vlog( "Error! Failed to communicate with inference engine %zu\n", index );
            fprintf( stderr, "\nError! Failed to communicate with inference engine %zu", index );
            close( poll_fds[index].fd );
            poll_fds[index].fd = -1;
            raise( SIGABRT );
            free( poll_fds );
            return pipes_length;
        }
    }

    free( poll_fds );
#endif
}

char nxai_pipe_read( bidirectional_pipe_t pipe_fd, PIPE_DIRECTION direction ) {
#if defined( _MSC_VER )
    // Windows implementation
    char buffer;
    DWORD bytes_read;
    if ( !ReadFile( nxai_pipe_get_read_pipe( pipe_fd, direction ), &buffer, 1, &bytes_read, NULL ) ) {
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
    if ( !WriteFile( nxai_pipe_get_write_pipe( pipe_fd, direction ), &signal, 1, &bytes_written, NULL ) ) {
        return -1;
    }
    return bytes_written;
#else
    // Linux implementation
    return write( nxai_pipe_get_write_pipe( pipe_fd, direction ), &signal, 1 );
#endif
}

char nxai_pipe_timed_read( bidirectional_pipe_t pipe_fd, PIPE_DIRECTION direction, int timeout ) {
#if defined( _MSC_VER )
    // Windows implementation
    char buffer;
    DWORD bytes_read;
    DWORD start_time = GetTickCount();

    while ( GetTickCount() - start_time < timeout * 1000 ) {
        if ( !ReadFile( nxai_pipe_get_read_pipe( pipe_fd, direction ), &buffer, 1, &bytes_read, NULL ) ) {
            return -1;
        }
        if ( bytes_read > 0 ) {
            return buffer;
        }

        Sleep( 100 );// Prevent busy waiting
    }
    return -3;
#else
    // Linux implementation
    char buffer;
    fd_set read_fds;
    struct timeval tv;
    ssize_t bytes_read;

    // Set up select parameters
    FD_ZERO( &read_fds );
    FD_SET( nxai_pipe_get_read_pipe( pipe_fd, direction ), &read_fds );
    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    // Wait for data or timeout
    if ( select( nxai_pipe_get_read_pipe( pipe_fd, direction ) + 1, &read_fds, NULL, NULL, &tv ) <= 0 ) {
        return -3;
    }

    // Read data if available
    bytes_read = read( nxai_pipe_get_read_pipe( pipe_fd, direction ), &buffer, 1 );
    if ( bytes_read == -1 ) {
        printf( "Error in read function during pipe timed read: %s\n", strerror( errno ) );
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
        CloseHandle( nxai_pipe_get_write_pipe( pipe, UP ) );
        CloseHandle( nxai_pipe_get_read_pipe( pipe, DOWN ) );
    } else {
        CloseHandle( nxai_pipe_get_write_pipe( pipe, DOWN ) );
        CloseHandle( nxai_pipe_get_read_pipe( pipe, UP ) );
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

nxai_shm_t nxai_shm_create_random( size_t size ) {
#if defined( _MSC_VER )
    // Windows implementation
    // Generate random name for anonymous mapping
    nxai_shm_t new_shm;
    sprintf_s( new_shm.key, MAX_PATH, "\\\\\\.\\Global\\RandomSHM_%08X", rand() );

    // Create file mapping object
    HANDLE hMapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE,// Use paging file
            NULL,                // Default security attributes
            PAGE_READWRITE,      // Read/write access
            0,                   // High DWORD of size
            size + HEADER_BYTES, // Low DWORD of size
            new_shm.key          // Name of mapping object
    );

    new_shm.id = hMapFile;

    // Use process ID as identifier
    return new_shm;
#else
    // Linux implementation
    int new_id = -1;
    key_t shm_key;
    // Keep trying random keys until unused is found
    while ( new_id == -1 ) {
        shm_key = rand();
        new_id = shmget( shm_key, size + HEADER_BYTES, 0666 | IPC_CREAT | IPC_EXCL );
    }
    return (nxai_shm_t) { .id = new_id, .key = shm_key };
#endif
}

nxai_shm_t nxai_shm_create( const char *path, int project_id, size_t size ) {
#if defined( _MSC_VER )
    // Windows implementation
    nxai_shm_t new_shm;
    sprintf_s( new_shm.key, MAX_PATH, L"\\\\\\.\\Global\\SHM_%S_%d", path, project_id );

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof( SECURITY_ATTRIBUTES );
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    new_shm.id = CreateFileMappingA(
            INVALID_HANDLE_VALUE,// Use paging file
            &saAttr,             // Security attributes
            PAGE_READWRITE,      // Read/write access
            0,                   // High DWORD of size
            size + HEADER_BYTES, // Low DWORD of size
            new_shm.key          // Name of mapping object
    );

    // Use process ID as identifier
    return new_shm;
#else
    // Linux implementation
    key_t shm_key = ftok( path, project_id );
    shm_id_t new_id = shmget( shm_key, size + HEADER_BYTES, 0666 | IPC_CREAT );
    if ( new_id == -1 ) {
        perror( "Failed to create SHM:" );
    }
    return (nxai_shm_t) { .id = new_id, .key = shm_key };
#endif
}

bool nxai_shm_get_id( nxai_shm_t *shm ) {
#if defined( _MSC_VER )
    // Windows implementation
    shm->id = OpenFileMappingW( FILE_MAP_ALL_ACCESS, FALSE, shm->key );
    return true;
#else
    // Linux implementation
    shm->id = shmget( shm->key, 0, 0 );
    if ( shm->id == -1 ) {
        printf( "Could not get SHM %d : %s\n", __LINE__, strerror( errno ) );
        return false;
    }
    return true;
#endif
}

void *nxai_shm_attach( nxai_shm_t shm ) {
#if defined( _MSC_VER )
    // Windows implementation
    return MapViewOfFile(
            shm.key,            // Handle to map object
            FILE_MAP_ALL_ACCESS,// Desired access
            0,                  // File offset (high DWORD)
            0,                  // File offset (low DWORD)
            0                   // Number of bytes to map
    );
#else
    // Linux implementation
    // Attach the shared memory segment to the process's address space.
    // This is done by calling the shmat() function with the shared memory ID.
    // The function returns a pointer to the attached shared memory segment.
    void *result = shmat( shm.id, NULL, 0 );
    return result;
#endif
}

void nxai_shm_write_to_attached( void *shm_buffer, const char *data, uint32_t size ) {
    // Write the size of the data to the beginning of the shared memory segment.
    // This is done by copying the size (which is an integer) to the shared memory segment.
    // The size is copied as a 4-byte value, as the size is represented as a 32-bit unsigned integer.
    memcpy( shm_buffer, &size, HEADER_BYTES );

    // Write the data to the shared memory segment.
    // This is done by copying the data to the shared memory segment, starting from the 4th byte,
    // as the first 4 bytes are used to store the size of the data.
    memcpy( shm_buffer + HEADER_BYTES, data, size );
}

bool nxai_shm_write( const nxai_shm_t *shm, const char *data, uint32_t size ) {
#if defined( _MSC_VER )
    // Windows implementation
    LPVOID view = MapViewOfFile(
            shm->id,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            size + HEADER_BYTES );

    if ( view == NULL ) {
        return false;
    }

    nxai_shm_write_to_attached( view, data, size );

    UnmapViewOfFile( view );
    return true;
#else
    // Linux implementation
    void *shm_pointer = nxai_shm_attach( *shm );
    if ( shm_pointer == (void *) -1 ) {
        return false;
    }

    nxai_shm_write_to_attached( shm_pointer, data, size );

    // Detach the shared memory segment from the process's address space.
    // This is done by calling the shmdt() function with the pointer to the shared memory segment.
    shmdt( shm_pointer );

    return true;
#endif
}

void nxai_shm_read_from_attached( void *shm_pointer, size_t *data_length, char **payload_data ) {
    // The first 4 bytes of the shared memory is always the size of the data
    uint32_t size;
    memcpy( &size, shm_pointer, HEADER_BYTES );
    *data_length = (size_t) size;
    // Return pointer to the payload data after the size header
    *payload_data = (char *) shm_pointer + HEADER_BYTES;
}

void *nxai_shm_read( nxai_shm_t *shm, size_t *data_length, char **payload_data ) {
#if defined( _MSC_VER )
    // Windows implementation
    LPVOID view = MapViewOfFile(
            shm->id,
            FILE_MAP_READ,
            0,
            0,
            0 );

    if ( view == NULL ) {
        return NULL;
    }

    nxai_shm_read_from_attached( view, data_length, payload_data );
    return view;
#else
    // Linux implementation
    void *shm_pointer = shmat( shm->id, NULL, 0 );
    if ( shm_pointer == (void *) -1 ) {
        return NULL;
    }
    nxai_shm_read_from_attached( shm_pointer, data_length, payload_data );
    // Return pointer to data after size
    return shm_pointer;
#endif
}

void nxai_shm_close( void *memory_address ) {
#if defined( _MSC_VER )
    // Windows implementation
    UnmapViewOfFile( memory_address );
#else
    // Linux implementation
    // Detach memory from this process
    shmdt( memory_address );
#endif
}

int nxai_shm_destroy( const nxai_shm_t *shm ) {
#if defined( _MSC_VER )
    // Windows implementation
    int result = CloseHandle( shm->id );
    return result ? 0 : -1;
#else
    // Linux implementation
    return shmctl( shm->id, IPC_RMID, NULL );
#endif
}

bool nxai_shm_realloc( nxai_shm_t *shm, size_t new_size ) {
#if defined( _MSC_VER )
    // Windows implementation
    CloseHandle( shm->id );

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof( SECURITY_ATTRIBUTES );
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    shm->id = CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            &saAttr,
            PAGE_READWRITE,
            0,
            new_size + HEADER_BYTES,
            shm->key );

    return true;
#else
    // Linux implementation
    // Remove old SHM
    if ( nxai_shm_destroy( shm ) != 0 ) {
        fprintf( stderr, "Error! Could not destroy Shared Memory segment with ID %d.\n", shm->id );
        return false;
    }

    shm->id = shmget( shm->key, new_size + HEADER_BYTES, 0666 | IPC_CREAT );

    return true;
#endif
}

size_t nxai_shm_get_size( nxai_shm_t *shm ) {
#if defined( _MSC_VER )
    // Windows implementation
    MEMORY_BASIC_INFORMATION memInfo;
    VirtualQuery( MapViewOfFile( shm->id, FILE_MAP_READ, 0, 0, 0 ),
                  &memInfo, sizeof( memInfo ) );
    return memInfo.RegionSize - HEADER_BYTES;
#else
    // Linux implementation
    struct shmid_ds buf;
    shmctl( shm->id, IPC_STAT, &buf );
    return buf.shm_segsz - HEADER_BYTES;
#endif
}