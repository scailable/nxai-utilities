#include "sclbl_socket_utils.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// Socket stuff
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>



const uint8_t MESSAGE_HEADER_LENGTH = 4;

bool sclbl_socket_interrupt_signal = false;

// Create timeout structure for socket connections
static struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };

// TODO: Add functionality to reuse buffer for return message #3
char* sclbl_socket_send_receive_message(const char *socket_path, const char *message_to_send, const uint32_t output_message_length){ 
    // Create new socket
    int32_t socket_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( socket_fd < 0 ) {
        fprintf( stderr,"Warning: socket() creation failed\n" );
        close( socket_fd );
        return NULL;
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
        fprintf( stderr,"Warning: access to sclblmod socket failed at %s\n", socket_path );
        close( socket_fd );
        return NULL;
    }

    // Connect to socket
    if ( connect( socket_fd, (struct sockaddr *) &addr,sizeof( struct sockaddr_un ) ) == -1 ) {
        fprintf(stderr, "Warning: connect to sclblmod socket failed\n" );
        close( socket_fd );
        return NULL;
    }

    const int32_t flags = MSG_NOSIGNAL;

    // Calculate header (needs to be 4 bytes)
    size_t header_sent_total = 0;
    // Send header
    for ( ssize_t sent_now = 0; header_sent_total < sizeof( output_message_length ); header_sent_total += (size_t) sent_now ) {
        sent_now = send( socket_fd, ( (char *) &output_message_length ) + header_sent_total,
                         sizeof( output_message_length ) - header_sent_total, flags );
        if ( sent_now == -1 ) {
            fprintf(stderr, "Warning: send to sclblmod socket failed\n" );
            close( socket_fd );
            return NULL;
        }
    }

    if ( header_sent_total != sizeof( output_message_length ) ) {
        fprintf(stderr, "Warning: Could not send header!\n" );
    }

    // Send string message
    size_t sent_total = 0;
    for ( ssize_t sent_now = 0; sent_total < output_message_length; sent_total += (size_t) sent_now ) {
        sent_now = send( socket_fd, ( (char *) message_to_send ) + sent_total, output_message_length - sent_total, flags );
        if ( sent_now == -1 ) {
            fprintf( stderr,"Warning: send to sclblmod socket failed\n" );
            close( socket_fd );
            return NULL;
        }
    }

    uint32_t input_message_length;
    ssize_t num_read = 0;
    // Read message header, which tells us the full message length
    num_read = recv(socket_fd, &input_message_length, MESSAGE_HEADER_LENGTH, flags);
    if ((size_t) num_read != MESSAGE_HEADER_LENGTH) {
        fprintf(stderr,"Warning: Sclblmod Socket Invalid socket header received. Ignoring.\n");
        close(socket_fd);
        return NULL;
    }

    // Allocate necessary space for incoming message
    char* output_string = malloc(input_message_length);

    // receive
    ssize_t num_read_cumulative = 0;
    while ((num_read = recv(socket_fd, output_string + num_read_cumulative, input_message_length - num_read_cumulative, flags)) > 0) {
        num_read_cumulative += num_read;

        // When multiple stacked input messages, it's possible for the socket to read further than the current message. So hard break.
        if (num_read_cumulative == input_message_length) {
            // Full message received
            break;
        }
    }

    if ( num_read == -1 ) {
        fprintf(stderr, "Warning: Error when receiving socket message!\n" );
        free(output_string);
        return NULL;
    }

    // Close socket connection
    close(socket_fd);
    return output_string;
}

/**
 * @brief Listen on socket for incoming messages
 *
 */
void sclbl_socket_start_listener( const char *socket_path, void ( *callback_function )( const char *, uint32_t ) ) {
    // Init socket to receive data
    struct sockaddr_un addr;

    // Create socket to listen on
    int socket_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( socket_fd == -1 ) {
        fprintf( stderr,"Error: Sender socket error. Exiting.\n" );
        return;
    }
    if ( strlen( socket_path ) > sizeof( addr.sun_path ) - 1 ) {
        fprintf( stderr,"Error: Sender socket path too long error. Exiting.\n" );
        return;
    }
    if ( remove( socket_path ) == -1 && errno != ENOENT ) {
        fprintf( stderr,"Error: Sender remove socket error. Exiting.\n" );
        return;
    }
    memset( &addr, 0, sizeof( struct sockaddr_un ) );

    addr.sun_family = AF_UNIX;
    strncpy( addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1 );

    // Bind to socket
    if ( bind( socket_fd, (struct sockaddr *) &addr, sizeof( struct sockaddr_un ) ) == -1 ) {
        fprintf(stderr, "Error: Sender socket bind error. Exiting.\n" );
        return;
    }

    // Start listening on socket
    if ( listen( socket_fd, 30 ) == -1 ) {
        fprintf(stderr, "Error: Sender socket listen error. Exiting.\n" );
        return;
    }

    size_t num_read_cumulitive = 0;
    ssize_t num_read;
    const int flags = MSG_NOSIGNAL;

    // Buffer for input message
    char *message_input_buffer = NULL;
    size_t allocated_buffer_size = 0;

    uint32_t message_length;

    // Listen in a loop
    while ( sclbl_socket_interrupt_signal == 0 ) {
        // Wait for incoming connection
        setsockopt( socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv );
        int connection_fd = accept( socket_fd, NULL, NULL );
        // Set timeout for socket receive

        // Read message header, which tells us the full message length
        setsockopt( connection_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv );
        num_read = recv( connection_fd, &message_length, MESSAGE_HEADER_LENGTH, flags );
        if ( (size_t) num_read != MESSAGE_HEADER_LENGTH ) {
            continue;
        }

        // Allocate space for incoming message.
        if ( message_length > allocated_buffer_size ) {
            // Incoming message is larger than allocated buffer. Reallocate.
            char* new_pointer = realloc(message_input_buffer,message_length * sizeof( char ));
            if (new_pointer == NULL) {
                fprintf(stderr, "Error: Could not allocate buffer with length: %d. Ignoring message.\n",message_length);
                continue;    
            }
            // Reallocation succesful
            allocated_buffer_size = message_length;
            message_input_buffer = new_pointer;
        }

        // Read at most read_buffer_size bytes from the socket into read_buffer,
        // then copy into read_buffer. Reset read count
        num_read_cumulitive = 0;
        while ( ( num_read = recv( connection_fd, message_input_buffer + num_read_cumulitive, message_length - num_read_cumulitive, flags ) ) > 0 ) {
            num_read_cumulitive += (size_t) num_read;
            if ( num_read_cumulitive >= message_length ) {
                // Full message received. Stop reading.
                break;
            }
        }
        if ( num_read == -1 ) {
            fprintf(stderr, "Warning: Error when receiving socket message!\n" );
        }

        if ( sclbl_socket_interrupt_signal == false ) {
            callback_function( message_input_buffer, message_length );
        }

        // Close connection
        if ( close( connection_fd ) == -1 ) {
            fprintf(stderr, "Warning: Sender socket close error!\n" );
        }
    }
    free( message_input_buffer );
}

/**
 * @brief Send a string to a socket
 *
 * @param socket_path Path to socket
 * @param message_to_send String to send
 */
void sclbl_socket_send( const char *socket_path, const char *message_to_send, uint32_t message_length) {

    // Create new socket
    int32_t socket_fd = socket( AF_UNIX, SOCK_STREAM, 0 );
    if ( socket_fd < 0 ) {
        fprintf(stderr, "Warning: socket() creation failed\n" );
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
        fprintf(stderr, "Warning: access to sclblmod socket failed at %s\n", socket_path );
        close( socket_fd );
        return;
    }

    // Connect to socket
    if ( connect( socket_fd, (struct sockaddr *) &addr,
                  sizeof( struct sockaddr_un ) )
         == -1 ) {
        fprintf(stderr, "Warning: connect to sclblmod socket failed\n" );
        close( socket_fd );
        return;
    }

    const int32_t flags = MSG_NOSIGNAL;

    size_t header_sent_total = 0;
    // Send header
    for ( ssize_t sent_now = 0; header_sent_total < sizeof( message_length ); header_sent_total += (size_t) sent_now ) {
        sent_now = send( socket_fd, ( (char *) &message_length ) + header_sent_total, sizeof( message_length ) - header_sent_total, flags );
        if ( sent_now == -1 ) {
            fprintf(stderr, "Warning: send to sclblmod socket failed\n" );
            close( socket_fd );
            return;
        }
    }

    if ( header_sent_total != sizeof( message_length ) ) {
        fprintf(stderr, "Warning: Could not send header!\n" );
    }

    // Send string message
    size_t sent_total = 0;
    for ( ssize_t sent_now = 0; sent_total < message_length; sent_total += (size_t) sent_now ) {
        sent_now = send( socket_fd, ( (char *) message_to_send ) + sent_total,
                         message_length - sent_total, flags );
        if ( sent_now == -1 ) {
            fprintf(stderr, "Warning: send to sclblmod socket failed\n" );
            close( socket_fd );
            return;
        }
    }

    // Close socket
    close( socket_fd );
}
