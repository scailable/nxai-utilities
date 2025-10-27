#include "nxai_socket_utils.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "nxai_utils.h"

#if defined(_MSC_VER)
    // Windows stuff
    #include <afunix.h>
    #include <basetsd.h>
    #include <errno.h>
    #include <ws2tcpip.h>
typedef SSIZE_T ssize_t;
#else
    // Socket stuff
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <sys/types.h>
    #include <sys/un.h>
    #include <unistd.h>
#endif

#ifdef NXAI_DEBUG
    #include "memory_leak_detector.h"
#endif

const uint8_t MESSAGE_HEADER_LENGTH = 4;

bool nxai_socket_interrupt_signal = false;

// Create timeout structure for socket connections
static struct timeval default_socket_timeout = {.tv_sec = 1, .tv_usec = 0};

// Helper function to convert Windows errors to errno values
#if defined(_MSC_VER)
// Windows implementation
static int win32_error_to_errno(DWORD error)
{
    switch (error)
    {
        case ERROR_FILE_NOT_FOUND:
            return ENOENT;
        case ERROR_PATH_NOT_FOUND:
            return ENOTDIR;
        case ERROR_ACCESS_DENIED:
            return EACCES;
        case ERROR_ALREADY_EXISTS:
            return EEXIST;
        case ERROR_INVALID_NAME:
            return EINVAL;
        case ERROR_NO_MORE_ITEMS:
            return ENOBUFS;
        case ERROR_INSUFFICIENT_BUFFER:
            return ENOMEM;
        default:
            return EINVAL;
    }
}
#endif

int nxai_socket_initialize_sockets()
{
#if defined(_MSC_VER)
    // Windows implementation
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0)
    {
        nxai_vlog("WSAStartup failed: %d\n", result);
        return 1;
    }
#endif
    return 0;
}

int nxai_socket_finalize_sockets()
{
#if defined(_MSC_VER)
    // Windows implementation
    WSACleanup();
#endif
    return 0;
}

uint32_t nxai_socket_send_receive_message(
    const char* socket_path,
    const char* message_to_send,
    const uint32_t sending_message_length,
    char** return_message_buffer,
    size_t* allocated_message_length)
{
#if defined(_MSC_VER)
    // Windows implementation
    SOCKET connection_fd = INVALID_SOCKET;

    // Create new socket
    connection_fd = WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0, 0);
    if (connection_fd == INVALID_SOCKET)
    {
        errno = win32_error_to_errno(WSAGetLastError());
        return 0;
    }

    // Convert path to wide characters for Windows
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, socket_path, -1, wpath, MAX_PATH);

    // Connect to socket
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    wcscpy_s((wchar_t*) addr.sun_path, MAX_PATH, wpath);

    if (connect(connection_fd, (struct sockaddr*) &addr, sizeof(struct sockaddr_un))
        == SOCKET_ERROR)
    {
        closesocket(connection_fd);
        errno = win32_error_to_errno(WSAGetLastError());
        return 0;
    }

    // Send message
    bool send_success =
        nxai_socket_send_to_connection(connection_fd, message_to_send, sending_message_length);
    if (!send_success)
    {
        closesocket(connection_fd);
        return 0;
    }

    // Receive response
    uint32_t received_message_length;
    nxai_socket_receive_on_connection(
        connection_fd,
        allocated_message_length,
        return_message_buffer,
        &received_message_length);

    // Close socket connection
    closesocket(connection_fd);
    return received_message_length;
#else
    // Linux implementation

    // Create new socket
    int32_t connection_fd = nxai_socket_connect(socket_path);
    if (connection_fd == -1)
    {
        return 0;
    }

    // Send message to connection
    bool send_success =
        nxai_socket_send_to_connection(connection_fd, message_to_send, sending_message_length);
    if (send_success == false)
    {
        close(connection_fd);
        return 0;
    }

    // Receive response on connection
    uint32_t received_message_length;
    nxai_socket_receive_on_connection(
        connection_fd,
        allocated_message_length,
        return_message_buffer,
        &received_message_length);

    // Close socket connection
    close(connection_fd);

    return received_message_length;
#endif
}

bool nxai_socket_is_valid(const nxai_socket_t* socket)
{
#if defined(_MSC_VER)
    // Windows implementation
    if (*socket == INVALID_SOCKET)
    {
        return false;
    }
#else
    // Linux implementation
    if (*socket == -1)
    {
        return false;
    }
#endif
    return true;
}

nxai_socket_t nxai_socket_create_listener(const char* socket_path)
{
    // Ensure socket file is deleted
    nxai_delete_socket_file(socket_path);

#if defined(_MSC_VER)
    // Windows implementation

    SOCKET socket_fd = INVALID_SOCKET;
    char unix_path[MAX_PATH];

    // Convert Windows path to Unix-style path
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, socket_path, -1, wpath, MAX_PATH);

    // Convert back to narrow string with Unix-style path
    WideCharToMultiByte(CP_UTF8, 0, wpath, -1, unix_path, MAX_PATH, NULL, NULL);

    // Create socket to listen on
    socket_fd = WSASocketA(AF_UNIX, SOCK_STREAM, 0, NULL, 0, 0);
    if (socket_fd == INVALID_SOCKET)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
        nxai_vlog("Error: Sender socket error: %.*s\n", error_length, error_string);
        return INVALID_SOCKET;
    }

    // Set up address structure
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, unix_path, sizeof(addr.sun_path) - 1);

    // Bind to socket
    if (bind(socket_fd, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == SOCKET_ERROR)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
        nxai_vlog("Error: Sender socket bind error: %.*s\n", error_length, error_string);
        closesocket(socket_fd);
        return INVALID_SOCKET;
    }

    // Set security attributes
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    // Start listening on socket
    if (listen(socket_fd, SOMAXCONN) == SOCKET_ERROR)
    {
        closesocket(socket_fd);
        errno = win32_error_to_errno(WSAGetLastError());
        nxai_vlog("Error: Sender socket listen error.\n");
        return INVALID_SOCKET;
    }

    return socket_fd;
#else
    // Linux implementation

    // Init socket to receive data
    struct sockaddr_un addr;

    // Create socket to listen on
    int socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd == -1)
    {
        nxai_vlog("Error: Sender socket error.\n");
        return -1;
    }
    if (strlen(socket_path) > sizeof(addr.sun_path) - 1)
    {
        nxai_vlog("Error: Sender socket path too long error.\n");
        return -1;
    }
    if (remove(socket_path) == -1 && errno != ENOENT)
    {
        nxai_vlog("Error: Sender remove socket error.\n");
        return -1;
    }
    memset(&addr, 0, sizeof(struct sockaddr_un));

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Bind to socket
    if (bind(socket_fd, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == -1)
    {
        nxai_vlog("Error: Sender socket bind error.\n");
        return -1;
    }

    // Set the socket file permissions to open
    nxai_chmod(socket_path, 0777);

    // Start listening on socket
    if (listen(socket_fd, 30) == -1)
    {
        nxai_vlog("Error: Sender socket listen error.\n");
        return -1;
    }

    // Change file permissions so anyone can write to it
    nxai_chmod(socket_path, S_IRGRP | S_IRUSR | S_IROTH | S_IWGRP | S_IWOTH | S_IWUSR);

    // Set timeout for socket
    setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        (const char*) &default_socket_timeout,
        sizeof default_socket_timeout);

    return socket_fd;
#endif
}

void nxai_socket_receive_on_connection(
    nxai_socket_t connection_fd,
    size_t* allocated_buffer_size,
    char** message_input_buffer,
    uint32_t* message_length)
{
#if defined(_MSC_VER)
    // Windows implementation
    int flags = 0;
    size_t num_read_cumulative = 0;
    int num_read;

    WSAPOLLFD poll_fd;
    poll_fd.fd = connection_fd;
    poll_fd.events = POLLRDNORM;

    int result = WSAPoll(&poll_fd, 1, default_socket_timeout.tv_sec * 1000);

    if (result == SOCKET_ERROR)
    {
        DWORD last_error = WSAGetLastError();
        char error_string[1024];
        get_windows_error(last_error, error_string, sizeof(error_string));
        nxai_vlog("Warning: Poll failed: %.*s\n", strlen(error_string), error_string);
        return;
    }
    else if (result == 0)
    {
        // Timeout occurred
        return;
    }

    // Read message header
    num_read = recv(connection_fd, (char*) message_length, sizeof(*message_length), flags);
    if (num_read != sizeof(*message_length))
    {
        if (num_read == SOCKET_ERROR)
        {
            char error_string[1024];
            DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
            nxai_vlog(
                "Warning: Could not receive size of incoming message: %.*s\n",
                error_length,
                error_string);
        }
        return;
    }

    // Allocate space for incoming message
    if ((*message_length) > (*allocated_buffer_size) || (*message_input_buffer) == NULL)
    {
        char* new_pointer =
            (char*) realloc(*message_input_buffer, (*message_length) * sizeof(char));
        if (new_pointer == NULL)
        {
            nxai_vlog(
                "Error: Could not allocate buffer with length: %d. Ignoring message.\n",
                (*message_length));
            *message_length = 0;
            return;
        }
        *allocated_buffer_size = *message_length;
        *message_input_buffer = new_pointer;
    }

    // Read the actual message
    while (num_read_cumulative < *message_length)
    {
        WSAPOLLFD poll_fd = {0};
        poll_fd.fd = connection_fd;
        poll_fd.events = POLLRDNORM;

        int poll_result = WSAPoll(&poll_fd, 1, default_socket_timeout.tv_sec * 1000);

        if (poll_result == SOCKET_ERROR)
        {
            // Handle poll error
            *message_length = 0;
            return;
        }
        if (poll_result == 0)
        {
            // Handle timeout
            *message_length = 0;
            return;
        }

        num_read = recv(
            connection_fd,
            (*message_input_buffer) + num_read_cumulative,
            (*message_length) - num_read_cumulative,
            flags);
        num_read_cumulative += num_read;
        if (num_read_cumulative >= *message_length)
            break;
    }

    if (num_read == SOCKET_ERROR)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
        nxai_vlog(
            "Warning: Error when receiving socket message: %.*s\n",
            error_length,
            error_string);
        *message_length = 0;
    }
#else
    // Linux implementation
    size_t num_read_cumulitive = 0;
    ssize_t num_read;
    const int flags = MSG_NOSIGNAL;

    // Set timeout for socket receive
    setsockopt(
        connection_fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        (const char*) &default_socket_timeout,
        sizeof default_socket_timeout);
    // Read message header, which tells us the full message length
    num_read = recv(connection_fd, message_length, MESSAGE_HEADER_LENGTH, flags);
    if ((size_t) num_read != MESSAGE_HEADER_LENGTH)
    {
        return;
    }

    // Allocate space for incoming message.
    if ((*message_length) > (*allocated_buffer_size) || (*message_input_buffer) == NULL)
    {
        // Incoming message is larger than allocated buffer. Reallocate.
        char* new_pointer =
            (char*) realloc((*message_input_buffer), (*message_length) * sizeof(char));
        if (new_pointer == NULL)
        {
            nxai_vlog(
                "Error: Could not allocate buffer with length: %d. Ignoring message.\n",
                (*message_length));
            return;
        }
        // Reallocation succesful
        *allocated_buffer_size = *message_length;
        *message_input_buffer = new_pointer;
    }

    // Read at most read_buffer_size bytes from the socket into read_buffer,
    // then copy into read_buffer. Reset read count
    num_read_cumulitive = 0;
    while ((num_read = recv(
                connection_fd,
                (*message_input_buffer) + num_read_cumulitive,
                (*message_length) - num_read_cumulitive,
                flags))
           > 0)
    {
        num_read_cumulitive += (size_t) num_read;
        if (num_read_cumulitive >= *message_length)
        {
            // Full message received. Stop reading.
            break;
        }
    }
    if (num_read == -1)
    {
        nxai_vlog("Warning: Error when receiving socket message!\n");
        *message_length = 0;
    }
#endif
}

nxai_socket_t nxai_socket_await_message(
    nxai_socket_t socket_fd,
    size_t* allocated_buffer_size,
    char** message_input_buffer,
    uint32_t* message_length)
{
#if defined(_MSC_VER)
    // Windows implementation
    // Create event for accepting connections
    WSAEVENT accept_event = WSACreateEvent();
    if (accept_event == WSA_INVALID_EVENT)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
        nxai_vlog("Error: Failed to create accept event: %.*s\n", error_length, error_string);
        return -1;
    }

    // Associate event with network events
    if (WSAEventSelect(socket_fd, accept_event, FD_ACCEPT | FD_CONNECT) == SOCKET_ERROR)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
        nxai_vlog("Error: Failed to select accept event: %.*s\n", error_length, error_string);
        WSACloseEvent(accept_event);
        return INVALID_SOCKET;
    }

    // Wait for connection or timeout
    DWORD wait_result = WSAWaitForMultipleEvents(
        1,
        &accept_event,
        FALSE,
        default_socket_timeout.tv_sec * 1000,
        FALSE);
    if (wait_result == WSA_WAIT_FAILED)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
        nxai_vlog("Error: Accept wait failed: %.*s\n", error_length, error_string);
        WSACloseEvent(accept_event);
        return INVALID_SOCKET;
    }

    if (wait_result == WSA_WAIT_TIMEOUT)
    {
        WSAResetEvent(accept_event);
        WSACloseEvent(accept_event);
        errno = ETIMEDOUT;
        return INVALID_SOCKET;
    }

    // Check if there's actually a connection pending
    fd_set read_fds;
    struct timeval zero_time = {0};
    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);

    int select_result = select(0, &read_fds, NULL, NULL, &zero_time);
    if (select_result <= 0)
    {
        WSACloseEvent(accept_event);
        return INVALID_SOCKET;
    }

    // Accept the connection
    SOCKET client_socket = accept(socket_fd, NULL, NULL);
    if (client_socket == INVALID_SOCKET)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
        nxai_vlog("Error: Accept failed: %.*s\n", error_length, error_string);
        WSACloseEvent(accept_event);
        return INVALID_SOCKET;
    }

    WSACloseEvent(accept_event);

    // Receive message on connection
    nxai_vlog("Receiving on connection...\n");
    nxai_socket_receive_on_connection(
        client_socket,
        allocated_buffer_size,
        message_input_buffer,
        message_length);

    return client_socket;
#else
    // Linux implementation

    // Wait for incoming connection
    int connection_fd = accept(socket_fd, NULL, NULL);

    nxai_socket_receive_on_connection(
        connection_fd,
        allocated_buffer_size,
        message_input_buffer,
        message_length);

    return connection_fd;
#endif
}

/**
 * @brief Listen on socket for incoming messages
 *
 */
int32_t nxai_socket_start_listener(
    const char* socket_path,
    void (*callback_function)(const char*, uint32_t, int))
{
#if defined(_MSC_VER)
    // Windows implementation
    SOCKET socket_fd = INVALID_SOCKET;
    uint32_t message_length;
    char* message_input_buffer = NULL;
    size_t allocated_buffer_size = 0;

    // Create socket listener
    socket_fd = nxai_socket_create_listener(socket_path);
    if (socket_fd == INVALID_SOCKET)
    {
        errno = win32_error_to_errno(WSAGetLastError());
        return 1;
    }

    // Listen in a loop until interrupted
    while (nxai_socket_interrupt_signal == 0)
    {
        SOCKET connection_fd = nxai_socket_await_message(
            socket_fd,
            &allocated_buffer_size,
            &message_input_buffer,
            &message_length);

        if (connection_fd == INVALID_SOCKET)
        {
            // Socket likely timed out, continue listening
            continue;
        }

        if (nxai_socket_interrupt_signal == false)
        {
            callback_function(message_input_buffer, message_length, connection_fd);
        }

        // Close connection
        if (closesocket(connection_fd) == SOCKET_ERROR)
        {
            nxai_vlog("Warning: Sender socket close error!\n");
        }
    }

    // Cleanup
    if (message_input_buffer != NULL)
    {
        free(message_input_buffer);
    }

    closesocket(socket_fd);

    // Delete socket file
    DeleteFileA(socket_path);

    return 0;
#else
    // Linux implementation
    // Create socket
    int socket_fd = nxai_socket_create_listener(socket_path);
    if (socket_fd == -1)
    {
        return 1;
    }

    uint32_t message_length;

    // Buffer for input message
    char* message_input_buffer = NULL;
    size_t allocated_buffer_size = 0;

    // Listen in a loop
    while (nxai_socket_interrupt_signal == 0)
    {
        int connection_fd = nxai_socket_await_message(
            socket_fd,
            &allocated_buffer_size,
            &message_input_buffer,
            &message_length);

        if (connection_fd == -1)
        {
            // Socket likely timed out, start waiting again ( effectively checking for interrupt
            // signal )
            continue;
        }

        if (nxai_socket_interrupt_signal == false)
        {
            callback_function(message_input_buffer, message_length, connection_fd);
        }

        // Close connection
        if (close(connection_fd) == -1)
        {
            nxai_vlog("Warning: Sender socket close error!\n");
        }
    }
    free(message_input_buffer);

    // Unlink socket file so it can be used again
    unlink(socket_path);

    return 0;
#endif
}

void nxai_delete_socket_file(const char* socket_path)
{
#if defined(_MSC_VER)
    // Windows implementation
    DeleteFileA(socket_path);
#else
    // Linux implementation
    unlink(socket_path);
#endif
}

nxai_socket_t nxai_socket_connect(const char* socket_path)
{
#if defined(_MSC_VER)
    // Windows implementation
    SOCKET socket_fd = INVALID_SOCKET;

    // Create new socket
    socket_fd = WSASocketW(AF_UNIX, SOCK_STREAM, 0, NULL, 0, 0);
    if (socket_fd == INVALID_SOCKET)
    {
        nxai_vlog("Warning: socket() creation failed\n");
        return INVALID_SOCKET;
    }

    // Generate socket address
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;

    // Copy narrow path directly
    strncpy_s(addr.sun_path, sizeof(addr.sun_path), socket_path, _TRUNCATE);

    // Connect to socket
    if (connect(socket_fd, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == SOCKET_ERROR)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(WSAGetLastError(), error_string, 1024);
        nxai_vlog(
            "Warning: Connect to socket [%s] failed: %.*s\n",
            socket_path,
            error_length,
            error_string);
        closesocket(socket_fd);
        return INVALID_SOCKET;
    }

    return socket_fd;
#else
    // Linux implementation
    // Create new socket
    int32_t socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        nxai_vlog("Warning: socket() creation failed\n");
        close(socket_fd);
        return -1;
    }
    setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        (const char*) &default_socket_timeout,
        sizeof default_socket_timeout);
    setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        (const char*) &default_socket_timeout,
        sizeof default_socket_timeout);

    // Generate socket address
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;

    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    // Check if we have access to socket
    if (access(socket_path, F_OK) != 0)
    {
        nxai_vlog("Warning: access to socket failed at %s\n", socket_path);
        close(socket_fd);
        return -1;
    }

    // Connect to socket
    if (connect(socket_fd, (struct sockaddr*) &addr, sizeof(struct sockaddr_un)) == -1)
    {
        nxai_vlog("Warning: connect to socket [%s] failed: %s\n", socket_path, strerror(errno));
        close(socket_fd);
        return -1;
    }

    return socket_fd;
#endif
}

void nxai_socket_send(
    const char* socket_path,
    const char* message_to_send,
    uint32_t message_length)
{
    nxai_socket_t connection_fd = nxai_socket_connect(socket_path);

    // Send message to newly created socket
    nxai_socket_send_to_connection(connection_fd, message_to_send, message_length);

    nxai_close_socket(connection_fd);
}

int nxai_close_socket(nxai_socket_t connection_fd)
{
// Close socket
#if defined(_MSC_VER)
    // Windows implementation
    return closesocket(connection_fd);
#else
    // Linux implementation
    return close(connection_fd);
#endif
}

bool nxai_socket_send_to_connection(
    const nxai_socket_t connection_fd,
    const char* message_to_send,
    uint32_t message_length)
{
#if defined(_MSC_VER)
    // Windows implementation
    // Set timeout for sending
    setsockopt(
        connection_fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        (const char*) &default_socket_timeout,
        sizeof(default_socket_timeout));

    size_t header_sent_total = 0;

    // Send header (message length)
    for (ssize_t sent_now = 0; header_sent_total < sizeof(message_length);
         header_sent_total += (size_t) sent_now)
    {
        sent_now = send(
            connection_fd,
            (char*) &message_length + header_sent_total,
            sizeof(message_length) - header_sent_total,
            0);
        if (sent_now == SOCKET_ERROR)
        {
            nxai_vlog("Warning: send to socket failed\n");
            return false;
        }
    }

    if (header_sent_total != sizeof(message_length))
    {
        nxai_vlog("Warning: Could not send header!\n");
        return false;
    }

    // Send message
    size_t sent_total = 0;
    for (ssize_t sent_now = 0; sent_total < message_length; sent_total += (size_t) sent_now)
    {
        sent_now = send(
            connection_fd,
            (char*) message_to_send + sent_total,
            message_length - sent_total,
            0);
        if (sent_now == SOCKET_ERROR)
        {
            nxai_vlog("Warning: send to socket failed\n");
            return false;
        }
    }

    return true;
#else
    // Linux implementation
    setsockopt(
        connection_fd,
        SOL_SOCKET,
        SO_SNDTIMEO,
        (const char*) &default_socket_timeout,
        sizeof default_socket_timeout);

    const int32_t flags = MSG_NOSIGNAL;

    size_t header_sent_total = 0;
    // Send header
    for (ssize_t sent_now = 0; header_sent_total < sizeof(message_length);
         header_sent_total += (size_t) sent_now)
    {
        sent_now = send(
            connection_fd,
            ((char*) &message_length) + header_sent_total,
            sizeof(message_length) - header_sent_total,
            flags);
        if (sent_now == -1)
        {
            nxai_vlog("Warning: send to socket failed\n");
            return false;
        }
    }

    if (header_sent_total != sizeof(message_length))
    {
        nxai_vlog("Warning: Could not send header!\n");
        return false;
    }

    // Send message
    size_t sent_total = 0;
    for (ssize_t sent_now = 0; sent_total < message_length; sent_total += (size_t) sent_now)
    {
        sent_now = send(
            connection_fd,
            ((char*) message_to_send) + sent_total,
            message_length - sent_total,
            flags);
        if (sent_now == -1)
        {
            nxai_vlog("Warning: send to socket failed\n");
            return false;
        }
    }

    return true;
#endif
}
