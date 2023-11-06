#include "sclbl_socket_utils.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Socket stuff
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

bool sclbl_socket_interrupt_signal = false;

// Create timeout structure for socket connections
static struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

/**
 * @brief Listen on socket for incoming messages
 *
 */
void sclbl_socket_start_listener( const char *socket_path, void ( *callback_function )( const char *, uint32_t ) ) {
    // Init socket to receive data
    struct sockaddr_un addr;

    // Create socket to listen on
    int sfd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( sfd == -1 ) {
        printf( "Error: Sender socket error. Exiting.\n" );
        fprintf( stderr, "{\"error\":\"Sender socket error. Exiting.\"}\n" );
    }
    if ( strlen( socket_path ) > sizeof( addr.sun_path ) - 1 ) {
        printf( "Error: Sender socket path too long error. Exiting.\n" );
        fprintf( stderr,
                 "{\"error\":\"Sender socket path too long error. Exiting.\"}\n" );
    }
    if ( remove( socket_path ) == -1 && errno != ENOENT ) {
        printf( "Error: Sender remove socket error. Exiting.\n" );
        fprintf( stderr, "{\"error\":\"Sender remove socket error. Exiting.\"}\n" );
    }
    memset( &addr, 0, sizeof( struct sockaddr_un ) );

    addr.sun_family = AF_UNIX;
    strncpy( addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1 );

    // Bind to socket
    if ( bind( sfd, (struct sockaddr *) &addr, sizeof( struct sockaddr_un ) ) == -1 ) {
        printf( "Error: Sender socket bind error. Exiting.\n" );
        fprintf( stderr, "{\"error\":\"Sender socket bind error. Exiting.\"}\n" );
    }

    // Start listening on socket
    if ( listen( sfd, 30 ) == -1 ) {
        printf( "Error: Sender socket listen error. Exiting.\n" );
        fprintf( stderr, "{\"error\":\"Sender socket listen error. Exiting.\"}\n" );
    }

    size_t num_read_cumulitive = 0;
    const size_t MSG_HEADER_LENGTH = 4;
    ssize_t num_read;
    const int flags = MSG_NOSIGNAL;

    // Buffer for input message
    char *message_input_buffer = NULL;
    size_t allocated_buffer_size = 0;

    uint32_t message_length;

    // Listen in a loop
    while ( sclbl_socket_interrupt_signal == 0 ) {
        // Wait for incoming connection
        setsockopt( sfd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv );
        int cfd = accept( sfd, NULL, NULL );
        // Set timeout for socket receive

        // Read message header, which tells us the full message length
        setsockopt( cfd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv );
        num_read = recv( cfd, &message_length, MSG_HEADER_LENGTH, flags );
        if ( (size_t) num_read != MSG_HEADER_LENGTH ) {
            continue;
        }

        // Allocate space for incoming message.
        if ( message_length > allocated_buffer_size ) {
            // Incoming message is larger than allocated buffer. Free and reallocate.
            allocated_buffer_size = message_length;
            free( message_input_buffer );
            message_input_buffer =
                    (char *) malloc( allocated_buffer_size * sizeof( char ) );
        }

        // Read at most read_buffer_size bytes from the socket into read_buffer,
        // then copy into read_buffer. Reset read count
        num_read_cumulitive = 0;
        while ( ( num_read = recv( cfd, message_input_buffer + num_read_cumulitive,
                                   message_length - num_read_cumulitive, flags ) )
                > 0 ) {

            num_read_cumulitive += (size_t) num_read;
            if ( num_read_cumulitive >= message_length ) {
                // Full message received. Stop reading.
                break;
            }
        }
        if ( num_read == -1 ) {
            printf( "Warning: Error when receiving socket message!\n" );
        }

        if ( sclbl_socket_interrupt_signal == false ) {
            callback_function( message_input_buffer, message_length );
        }

        // Close connection
        if ( close( cfd ) == -1 ) {
            printf( "Warning: Sender socket close error!\n" );
        }
    }
    free( message_input_buffer );
}

/**
 * @brief Send a string to a socket
 *
 * @param socket_path Path to socket
 * @param string_to_send String to send
 */
void sclbl_socket_send( const char *socket_path, const char *string_to_send) {

    // Create new socket
    int32_t socket_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( socket_fd < 0 ) {
        printf( "Warning: socket() creation failed\n" );
        close( socket_fd );
        return;
    }
    setsockopt( socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const char *) &tv, sizeof tv );

    // Generate socket address
    struct sockaddr_un addr;
    memset( &addr, 0, sizeof( struct sockaddr_un ) );
    addr.sun_family = AF_UNIX;

    strncpy( addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1 );

    // Check if we have access to socket
    if ( access( socket_path, F_OK ) != 0 ) {
        printf( "Warning: access to sclblmod socket failed at %s\n", socket_path );
        close( socket_fd );
        return;
    }

    // Connect to socket
    if ( connect( socket_fd, (struct sockaddr *) &addr,
                  sizeof( struct sockaddr_un ) )
         == -1 ) {
        printf( "Warning: connect to sclblmod socket failed\n" );
        close( socket_fd );
        return;
    }

    const int32_t flags = MSG_NOSIGNAL;

    // Calculate header (needs to be 4 bytes)
    uint32_t message_length = (uint32_t) strlen( string_to_send );
    size_t header_sent_total = 0;
    // Send header
    for ( ssize_t sent_now = 0; header_sent_total < sizeof( message_length ); header_sent_total += (size_t) sent_now ) {
        sent_now = send( socket_fd, ( (char *) &message_length ) + header_sent_total,
                         sizeof( message_length ) - header_sent_total, flags );
        if ( sent_now == -1 ) {
            printf( "Warning: send to sclblmod socket failed\n" );
            close( socket_fd );
            return;
        }
    }

    if ( header_sent_total != sizeof( message_length ) ) {
        printf( "Warning: Could not send header!\n" );
    }

    // Send string message
    size_t sent_total = 0;
    for ( ssize_t sent_now = 0; sent_total < message_length; sent_total += (size_t) sent_now ) {
        sent_now = send( socket_fd, ( (char *) string_to_send ) + sent_total,
                         message_length - sent_total, flags );
        if ( sent_now == -1 ) {
            printf( "Warning: send to sclblmod socket failed\n" );
            close( socket_fd );
            return;
        }
    }

    // Close socket
    close( socket_fd );
}
