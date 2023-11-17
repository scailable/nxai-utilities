#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief A boolean that can be used to interrupt the socket listener.
 * By default creating a socket listener will listen for new connections in a loop.
 * By setting this variable to true, this loop will be interrupted and the socket destroyed.
 */
extern bool sclbl_socket_interrupt_signal;

/**
 * @brief Create a UNIX socket and start listening on it.
 *
 * This function creates a UNIX socket and binds it to the specified path.
 * It then starts listening on the socket for incoming connections.
 * It also sets the permissions for the socket file to allow any user to write to it.
 * If the socket file already existed at the specified path, it is removed before creating a new one.
 * The function sets a timeout for receiving data on the socket.
 *
 * @param[in] socket_path The path at which to create the socket.
 *
 * @return The file descriptor for the new socket, or -1 if an error occurred.
 *
 * @note The function will return -1 if any of the following errors occur:
 * - An error occurred while creating the socket.
 * - The specified socket path is too long.
 * - An error occurred while removing the existing socket file (if any).
 * - An error occurred while binding the socket to the specified path.
 * - An error occurred while starting to listen on the socket.
 */
int sclbl_socket_create_listener( const char *socket_path );

/**
 * \brief Waits for an incoming socket message, reads it and saves it to the provided buffer.
 *
 * \details This function waits for an incoming connection on a given socket file descriptor, 
 * reads the message header to get the full message length, and saves the incoming message 
 * into the provided buffer. If the buffer is not large enough to hold the message, it reallocates 
 * the buffer. If there is an error when receiving the socket message, a warning message is printed.
 *
 * \param[in] socket_fd The file descriptor of the socket to await the message from.
 * \param[in,out] allocated_buffer_size The size of the allocated buffer. This value will be updated 
 *                                      if the buffer is reallocated.
 * \param[in,out] message_input_buffer The buffer to save the incoming message. This pointer may 
 *                                     be changed if the buffer is reallocated.
 * \param[out] message_length The length of the received message.
 *
 * \return The file descriptor of the incoming connection.
 *
 * \retval connection_fd The function is successfully executed.
 * \retval connection_fd An error occurred while the message header length does not equal MESSAGE_HEADER_LENGTH 
 *                       or while reallocating the buffer.
 */
int sclbl_socket_await_message( int socket_fd, size_t *allocated_buffer_size, char **message_input_buffer, uint32_t *message_length );

/**
 * @brief Creates a socket and sets it to listen for incoming connections.
 * 
 * This function initializes a socket using the provided socket path. It binds the socket 
 * to the specified path and sets it to listen mode. The socket's file permissions are 
 * changed to allow anyone to write to it. The function also sets a timeout for the socket.
 * 
 * @param socket_path The path of the Unix socket to create and listen on.
 * 
 * @return The file descriptor for the newly created socket, or -1 if an error occurred during 
 * socket creation, binding, or listening.
 */
void sclbl_socket_start_listener( const char *socket_path, void ( *callback_function )( const char *, uint32_t, int ) );

/**
 * @brief Send a string to a socket
 *
 * @param socket_path Path to socket
 * @param message_to_send String to send
 */
void sclbl_socket_send( const char *socket_path, const char *string_to_send, uint32_t message_length );

/**
 * \brief Send a message to a socket and receive a response.
 *
 * This function creates a new socket, sets the socket options, and connects to the socket at the given path.
 * It then sends a message header and the actual message to the socket.
 * After sending, it reads a message header from the socket to get the length of the incoming message,
 * allocates the necessary space for the incoming message, and receives the message.
 * If any step fails, the function cleans up and returns NULL.
 *
 * \param socket_path The file system path to the socket.
 * \param message_to_send The message to send to the socket.
 * \param output_message_length The length of the message to send.
 *
 * \return A pointer to the received message, or NULL if any step fails.
 */
char *sclbl_socket_send_receive_message( const char *socket_path, const char *string_to_send, const uint32_t output_message_length );

/**
 * @brief Sends a message to a socket
 *
 * This function sends a message to a given socket. The message to send and its length are also parameters
 * of the function. It first sets the socket options, then sends the length of the message as a header 
 * and finally sends the actual message. If any of the send operations fail, the function will return false.
 *
 * @param socket_fd The file descriptor of the socket to send the message to
 * @param message_to_send The message to be sent to the socket
 * @param message_length The length of the message to be sent
 *
 * @return true if the message was successfully sent, false otherwise
 */
bool sclbl_socket_send_to_socket( const int socket_fd, const char *message_to_send, uint32_t message_length );

#ifdef __cplusplus
}
#endif
