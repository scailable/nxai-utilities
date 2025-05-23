#include "nxai_shm_utils.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef NXAI_DEBUG
#include "memory_leak_detector.h"
#endif

#ifdef __MUSL__
// musl crosscompiler doesn't find time.h otherwise
#include "musl_time.h"
#else
#include <time.h>
#endif

#if defined( __WIN32__ )
// Windows stuff
#include "windows.h"
// Windows equivalent of Linux shared memory header
#else
// Pipe stuff
#include <sys/select.h>
#include <sys/shm.h>
#include <sys/stat.h>
#endif

// SHM stuff
#include <fcntl.h>

#define HEADER_BYTES 4

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
#if defined( __WIN32__ )
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

char nxai_pipe_read( bidirectional_pipe_t pipe_fd, PIPE_DIRECTION direction ) {
#if defined( __WIN32__ )
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

    bytes_read = read( nxai_pipe_get_read_fd( pipe_fd, direction ), &buffer, 1 );
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
#if defined( __WIN32__ )
    // Windows implementation
    DWORD bytes_written;
    if ( !WriteFile( nxai_pipe_get_write_pipe( pipe_fd, direction ), &signal, 1, &bytes_written, NULL ) ) {
        return -1;
    }
    return bytes_written;
#else
    // Linux implementation
    return write( nxai_pipe_get_write_fd( pipe_fd, direction ), &signal, 1 );
#endif
}

char nxai_pipe_timed_read( bidirectional_pipe_t pipe_fd, PIPE_DIRECTION direction, int timeout ) {
#if defined( __WIN32__ )
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
    FD_SET( nxai_pipe_get_read_fd( pipe_fd, direction ), &read_fds );
    tv.tv_sec = timeout;
    tv.tv_usec = 0;

    // Wait for data or timeout
    if ( select( nxai_pipe_get_read_fd( pipe_fd, direction ) + 1, &read_fds, NULL, NULL, &tv ) <= 0 ) {
        return -3;
    }

    // Read data if available
    bytes_read = read( nxai_pipe_get_read_fd( pipe_fd, direction ), &buffer, 1 );
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
#if defined( __WIN32__ )
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
        close( nxai_pipe_get_write_fd( pipe, UP ) );
        close( nxai_pipe_get_read_fd( pipe, DOWN ) );
    } else {
        // Close writing down and reading up
        close( nxai_pipe_get_write_fd( pipe, DOWN ) );
        close( nxai_pipe_get_read_fd( pipe, UP ) );
    }
#endif
}

nxai_shm_t nxai_shm_create_random( size_t size ) {
#if defined( __WIN32__ )
    // Windows implementation
    // Generate random name for anonymous mapping
    nxai_shm_t new_shm;
    swprintf_s( new_shm.key, MAX_PATH, L"\\\\\\.\\Global\\RandomSHM_%08X", rand() );

    // Create file mapping object
    HANDLE hMapFile = CreateFileMappingW(
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
#if defined( __WIN32__ )
    // Windows implementation
    wchar_t wPath[MAX_PATH];
    mbstowcs_s( NULL, wPath, MAX_PATH, path, _TRUNCATE );

    nxai_shm_t new_shm;
    swprintf_s( new_shm.key, MAX_PATH, L"\\\\\\.\\Global\\SHM_%S_%d", wPath, project_id );

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof( SECURITY_ATTRIBUTES );
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    new_shm.id = CreateFileMappingW(
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
    *shm_id = shmget( shm_key, size + HEADER_BYTES, 0666 | IPC_CREAT );
    if ( *shm_id == -1 ) {
        perror( "Failed to create SHM:" );
    }
    return (nxai_shm_t) { .id = new_id, .key = shm_key };
#endif
}

bool nxai_shm_get_id( nxai_shm_t *shm ) {
#if defined( __WIN32__ )
    // Windows implementation
    shm->id = OpenFileMappingW( FILE_MAP_ALL_ACCESS, FALSE, shm->key );
    return true;
#else
    // Linux implementation
    shm->id = = shmget( shm_key, 0, 0 );
    if ( shm_id == -1 ) {
        printf( "Could not get SHM %d : %s\n", __LINE__, strerror( errno ) );
        return false;
    }
    return true;
#endif
}

void *nxai_shm_attach( nxai_shm_t shm ) {
#if defined( __WIN32__ )
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
#if defined( __WIN32__ )
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
    void *shm_pointer = nxai_shm_attach( shm->id );
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
#if defined( __WIN32__ )
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
    void *shm_pointer = shmat( shm_id, NULL, 0 );
    if ( shm_pointer == (void *) -1 ) {
        return NULL;
    }
    nxai_shm_read_from_attached( shm_pointer, data_length, payload_data );
    // Return pointer to data after size
    return shm_pointer;
#endif
}

void nxai_shm_close( void *memory_address ) {
#if defined( __WIN32__ )
    // Windows implementation
    UnmapViewOfFile( memory_address );
#else
    // Linux implementation
    // Detach memory from this process
    shmdt( memory_address );
#endif
}

int nxai_shm_destroy( const nxai_shm_t *shm ) {
#if defined( __WIN32__ )
    // Windows implementation
    int result = CloseHandle( shm->id );
    return result ? 0 : -1;
#else
    // Linux implementation
    return shmctl( shm_id, IPC_RMID, NULL );
#endif
}

bool nxai_shm_realloc( nxai_shm_t *shm, size_t new_size ) {
#if defined( __WIN32__ )
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
    if ( nxai_shm_destroy( shm->id ) != 0 ) {
        fprintf( stderr, "Error! Could not destroy Shared Memory segment with ID %d.\n", shm->id );
        return false;
    }

    shm->id = shmget( shm->key, new_size + HEADER_BYTES, 0666 | IPC_CREAT );

    return true;
#endif
}

size_t nxai_shm_get_size( nxai_shm_t *shm ) {
#if defined( __WIN32__ )
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