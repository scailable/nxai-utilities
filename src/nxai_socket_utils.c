#include "nxai_socket_utils.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Socket stuff
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>

#ifdef NXAI_DEBUG
#include "memory_leak_detector.h"
#endif

const uint8_t MESSAGE_HEADER_LENGTH = 4;

bool nxai_socket_interrupt_signal = false;

// Create timeout structure for socket connections
static struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

uint32_t nxai_socket_send_receive_message( const char *socket_path, const char *message_to_send, const uint32_t sending_message_length, char **return_message_buffer, size_t *allocated_message_length ) {
    // Create new socket
    int32_t connection_fd = nxai_socket_connect( socket_path );
    if ( connection_fd == -1 ) {
        return 0;
    }

    // Send message to connection
    bool send_success = nxai_socket_send_to_connection( connection_fd, message_to_send, sending_message_length );
    if ( send_success == false ) {
        close( connection_fd );
        return 0;
    }

    // Receive response on connection
    uint32_t received_message_length;
    nxai_socket_receive_on_connection( connection_fd, allocated_message_length, return_message_buffer, &received_message_length );

    // Close socket connection
    close( connection_fd );

    return received_message_length;
}

int nxai_socket_create_listener( const char *socket_path ) {
    // Init socket to receive data
    struct sockaddr_un addr;

    // Create socket to listen on
    int socket_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( socket_fd == -1 ) {
        printf( "Error: Sender socket error.\n" );
        return -1;
    }
    if ( strlen( socket_path ) > sizeof( addr.sun_path ) - 1 ) {
        printf( "Error: Sender socket path too long error.\n" );
        return -1;
    }
    if ( remove( socket_path ) == -1 && errno != ENOENT ) {
        printf( "Error: Sender remove socket error.\n" );
        return -1;
    }
    memset( &addr, 0, sizeof( struct sockaddr_un ) );

    addr.sun_family = AF_UNIX;
    strncpy( addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1 );

    // Bind to socket
    if ( bind( socket_fd, (struct sockaddr *) &addr, sizeof( struct sockaddr_un ) ) == -1 ) {
        printf( "Error: Sender socket bind error.\n" );
        return -1;
    }

    // Set the socket file permissions to open
    chmod( socket_path, 0777 );

    // Start listening on socket
    if ( listen( socket_fd, 30 ) == -1 ) {
        printf( "Error: Sender socket listen error.\n" );
        return -1;
    }

    // Change file permissions so anyone can write to it
    chmod( socket_path, S_IRGRP | S_IRUSR | S_IROTH | S_IWGRP | S_IWOTH | S_IWUSR );

    // Set timeout for socket
    setsockopt( socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv );

    return socket_fd;
}

void nxai_socket_receive_on_connection( int connection_fd, size_t *allocated_buffer_size, char **message_input_buffer, uint32_t *message_length ) {

    size_t num_read_cumulitive = 0;
    ssize_t num_read;
    const int flags = MSG_NOSIGNAL;

    // Set timeout for socket receive
    setsockopt( connection_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv );
    // Read message header, which tells us the full message length
    num_read = recv( connection_fd, message_length, MESSAGE_HEADER_LENGTH, flags );
    if ( (size_t) num_read != MESSAGE_HEADER_LENGTH ) {
        return;
    }

    // Allocate space for incoming message.
    if ( ( *message_length ) > ( *allocated_buffer_size ) || ( *message_input_buffer ) == NULL ) {
        // Incoming message is larger than allocated buffer. Reallocate.
        char *new_pointer = realloc( ( *message_input_buffer ), ( *message_length ) * sizeof( char ) );
        if ( new_pointer == NULL ) {
            printf( "Error: Could not allocate buffer with length: %d. Ignoring message.\n", ( *message_length ) );
            return;
        }
        // Reallocation succesful
        *allocated_buffer_size = *message_length;
        *message_input_buffer = new_pointer;
    }

    // Read at most read_buffer_size bytes from the socket into read_buffer,
    // then copy into read_buffer. Reset read count
    num_read_cumulitive = 0;
    while ( ( num_read = recv( connection_fd, ( *message_input_buffer ) + num_read_cumulitive, ( *message_length ) - num_read_cumulitive, flags ) ) > 0 ) {
        num_read_cumulitive += (size_t) num_read;
        if ( num_read_cumulitive >= *message_length ) {
            // Full message received. Stop reading.
            break;
        }
    }
    if ( num_read == -1 ) {
        printf( "Warning: Error when receiving socket message!\n" );
    }
}

int nxai_socket_await_message( int socket_fd, size_t *allocated_buffer_size, char **message_input_buffer, uint32_t *message_length ) {

    // Wait for incoming connection
    int connection_fd = accept( socket_fd, NULL, NULL );

    nxai_socket_receive_on_connection( connection_fd, allocated_buffer_size, message_input_buffer, message_length );

    return connection_fd;
}

/**
 * @brief Listen on socket for incoming messages
 *
 */
void nxai_socket_start_listener( const char *socket_path, void ( *callback_function )( const char *, uint32_t, int ) ) {

    // Create socket
    int socket_fd = nxai_socket_create_listener( socket_path );
    if ( socket_fd == -1 ) {
        printf( "Error: Failed to create listening socket.\n" );
        return;
    }

    uint32_t message_length;

    // Buffer for input message
    char *message_input_buffer = NULL;
    size_t allocated_buffer_size = 0;

    // Listen in a loop
    while ( nxai_socket_interrupt_signal == 0 ) {

        int connection_fd = nxai_socket_await_message( socket_fd, &allocated_buffer_size, &message_input_buffer, &message_length );

        if ( connection_fd == -1 ) {
            // Socket likely timed out, start waiting again ( effectively checking for interrupt signal )
            continue;
        }

        if ( nxai_socket_interrupt_signal == false ) {
            callback_function( message_input_buffer, message_length, connection_fd );
        }

        // Close connection
        if ( close( connection_fd ) == -1 ) {
            printf( "Warning: Sender socket close error!\n" );
        }
    }
    free( message_input_buffer );

    // Unlink socket file so it can be used again
    unlink( socket_path );
}

int32_t nxai_socket_connect( const char *socket_path ) {
    // Create new socket
    int32_t socket_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( socket_fd < 0 ) {
        printf( "Warning: socket() creation failed\n" );
        close( socket_fd );
        return -1;
    }
    setsockopt( socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *) &tv, sizeof tv );
    setsockopt( socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv );

    // Generate socket address
    struct sockaddr_un addr;
    memset( &addr, 0, sizeof( struct sockaddr_un ) );
    addr.sun_family = AF_UNIX;

    strncpy( addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1 );

    // Check if we have access to socket
    if ( access( socket_path, F_OK ) != 0 ) {
        printf( "Warning: access to socket failed at %s\n", socket_path );
        close( socket_fd );
        return -1;
    }

    // Connect to socket
    if ( connect( socket_fd, (struct sockaddr *) &addr,
                  sizeof( struct sockaddr_un ) )
         == -1 ) {
        printf( "Warning: connect to socket [%s] failed: %s\n", socket_path, strerror( errno ) );
        close( socket_fd );
        return -1;
    }

    return socket_fd;
}

void nxai_socket_send( const char *socket_path, const char *message_to_send, uint32_t message_length ) {

    int32_t connection_fd = nxai_socket_connect( socket_path );

    // Send message to newly created socket
    nxai_socket_send_to_connection( connection_fd, message_to_send, message_length );

    // Close socket
    close( connection_fd );
}

bool nxai_socket_send_to_connection( const int connection_fd, const char *message_to_send, uint32_t message_length ) {

    setsockopt( connection_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *) &tv, sizeof tv );

    const int32_t flags = MSG_NOSIGNAL;

    size_t header_sent_total = 0;
    // Send header
    for ( ssize_t sent_now = 0; header_sent_total < sizeof( message_length ); header_sent_total += (size_t) sent_now ) {
        sent_now = send( connection_fd, ( (char *) &message_length ) + header_sent_total, sizeof( message_length ) - header_sent_total, flags );
        if ( sent_now == -1 ) {
            printf( "Warning: send to socket failed\n" );
            return false;
        }
    }

    if ( header_sent_total != sizeof( message_length ) ) {
        printf( "Warning: Could not send header!\n" );
        return false;
    }

    // Send message
    size_t sent_total = 0;
    for ( ssize_t sent_now = 0; sent_total < message_length; sent_total += (size_t) sent_now ) {
        sent_now = send( connection_fd, ( (char *) message_to_send ) + sent_total,
                         message_length - sent_total, flags );
        if ( sent_now == -1 ) {
            printf( "Warning: send to socket failed\n" );
            return false;
        }
    }

    return true;
}
