import requests
import os
import struct
import socket
import time
import json
import sysv_ipc as ipc
import msgpack


def patchSettings(
    settings_contents: dict, uiprovider_url="http://127.0.0.1:8081"
) -> bool:
    """
    This function sends a PATCH request to update settings on a UI provider.

    Args:
        settings_contents (dict): The settings to be updated. This should be a dictionary where the keys are the setting names and the values are the new settings.
        uiprovider_url (str, optional): The URL of the UI provider. Defaults to "http://127.0.0.1:8081".

    Returns:
        bool: True if the PATCH request was successful, False otherwise.
    """
    print("Loading test settings...")  # Print a loading message

    # Try to send the test settings as a PATCH request
    try:
        # Formulate the PATCH request
        response = requests.patch(uiprovider_url + "/settings", data=settings_contents)
        response.raise_for_status()  # Raise an exception if the response indicates an unsuccessful status code (non-2xx)
    except requests.exceptions.Timeout:
        print(
            "Request timed out"
        )  # If the request times out, print an error message and return False
        return False
    except requests.exceptions.RequestException as e:
        # If there is a different request exception, print the error message and return False
        print("Error:", e)
        return False

    # If the PATCH request was successful, return True
    return True


def startUnixSocketServer(
    socket_path="/opt/sclbl/sockets/output_socket.sock",
) -> socket.socket:
    """
    This function creates and starts a Unix socket server.

    Parameters:
    socket_path (str): The path where the Unix socket file is to be created.
                       Default is "/opt/sclbl/sockets/output_socket.sock"

    Returns:
    socket.socket: a socket object representing the server

    The function first deletes the socket file at the given path if it already exists.
    It then creates a new Unix socket server and binds it to the given path.
    Finally, it makes the server listen for incoming connections.

    If an OSError occurs during the deletion of the existing socket file, the function checks
    if the file still exists. If it does, the OSError is raised again.

    Example usage:

    server = startUnixSocketServer("/tmp/my_socket.sock")

    """

    # Remove the socket file if it already exists
    try:
        os.unlink(socket_path)
    except OSError:
        if os.path.exists(socket_path):
            raise

    # Create the Unix socket server
    server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

    # Bind the socket to the path
    server.bind(socket_path)

    # Listen for incoming connections
    server.listen(1)

    return server


def receiveMessageOverConnection(connection) -> bytes:
    """
    Receives a message over a network connection.

    This function first receives 4 bytes of data from the client which it interprets
    as an unsigned integer representing the length of the incoming message. It then
    continuously receives data until the total length of the received data matches the
    message length. If no data is received in a round, it breaks the loop. Finally, it
    decodes the received bytes into a UTF-8 string and returns it.

    :param connection: The network connection over which to receive the message
    :type connection: socket
    :return: The received message decoded as a UTF-8 string
    :rtype: str
    """
    data = connection.recv(4)
    message_length = struct.unpack("<I", data)[0]

    data_received = 0
    data: bytes = b""
    while data_received < message_length:
        data += connection.recv(message_length - data_received)
        data_received = len(data)
        if not data:
            break

    return data


def waitForSocketMessage(server: socket.socket, timeout=10) -> (bytes, socket.socket):
    """
    This function waits for a message to be received on a socket server.

    It first sets a timeout for the server to listen for a connection, and then another timeout for receiving data from the client. The function continuously receives data from the client until it has received all the data (according to the message length specified by the client) or the connection is closed.

    Args:
        server (socket.socket): The socket server that should listen for and receive the message.
        timeout (int, optional): The number of seconds the server should wait for a connection and for data to be received. Defaults to 10.

    Returns:
        tuple: A tuple where the first element is the received message decoded from bytes to a string, and the second element is the connection on which the message was received.

    Raises:
        socket.timeout: If a timeout occurs while waiting for a connection or while receiving data.
    """
    server.settimeout(timeout)  # timeout for listening
    connection, _ = server.accept()
    connection.settimeout(timeout)  # timeout for receiving data
    # Receive the message
    data = receiveMessageOverConnection(connection)

    return data, connection


def sendMessageOverConnection(connection: socket.socket, message: bytes):
    """
    Sends a message over a socket connection.

    This function takes a socket connection and a message as input.
    The message is converted into bytes, and its length is calculated.
    Both the length of the message and the message itself are sent over the connection.

    Parameters:
    connection (socket.socket): The socket connection over which the message should be sent.
    message (str): The message to be sent.

    Returns:
    None

    Raises:
    OSError: An error occurred while sending data over the socket connection.

    Example:
    >>> import socket
    >>> connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    >>> connection.connect(("/opt/sclbl/sockets/sclblmod.sock"))
    >>> sendMessageOverConnection(connection, "Hello, World!")
    """

    # Calculate the length of the message
    message_length = struct.pack("<I", len(message))

    # Send the length of the message over the connection
    connection.sendall(message_length)

    # Send the message over the connection
    connection.sendall(message)


def sendSocketMessage(
    message: str, sclbl_input_socket_path: str = "/opt/sclbl/sockets/sclblmod.sock"
):
    """
    Sends a message through a socket connection to a specified Unix socket path.

    :param message: The message to be sent over the socket connection.
    :type message: str
    :param sclbl_input_socket_path: The path to the Unix socket. Default is "/opt/sclbl/sockets/sclblmod.sock".
    :type sclbl_input_socket_path: str
    :effects: Establishes a socket connection, sends the message, and then closes the connection.
    """
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(10)
    client.connect(sclbl_input_socket_path)
    sendMessageOverConnection(client, message)
    client.close()


def sendSocketBytes(
    message: str,
    bytes_array: bytes,
    sclbl_input_socket_path: str = "/opt/sclbl/sockets/sclblmod.sock",
):
    """
    Sends a string message and binary data to a specified Unix socket.

    This function creates a socket connection to the provided path, sends a string message,
    followed by binary data, and finally closes the connection. The string message is
    sent as UTF-8 encoded bytes prefixed by its length in bytes.

    :param message: The message to be sent to the socket.
    :type message: str
    :param bytes_array: The binary data to be sent after the message.
    :type bytes_array: bytes
    :param sclbl_input_socket_path: The path to the Unix socket. Default is "/opt/sclbl/sockets/sclblmod.sock".
    :type sclbl_input_socket_path: str
    :return: This function doesn't return anything.
    """
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(10)
    client.connect(sclbl_input_socket_path)
    message_bytes = bytes(message, "utf-8")
    message_length = struct.pack("<I", len(message_bytes))
    client.sendall(message_length)
    # Send string message
    client.sendall(message_bytes)
    # Send binary image data
    client.sendall(bytes_array)
    client.close()


def startScailableRuntime(uiprovider_url="http://127.0.0.1:8081"):
    """
    Starts the Scailable Runtime by making a GET request to the provided URL.

    :param uiprovider_url: The URL of the UI provider. Defaults to "http://127.0.0.1:8081".
    :type uiprovider_url: str
    :return: The response from the GET request.
    """
    return executeGetRequest(uiprovider_url + "/start", timeout=10)


def stopScailableRuntime(uiprovider_url="http://127.0.0.1:8081"):
    """
    Sends a GET request to stop the Scailable runtime.

    :param uiprovider_url: The URL of the UI provider. Default is "http://127.0.0.1:8081".
    :type uiprovider_url: str
    :return: The response from the GET request.
    """
    return executeGetRequest(uiprovider_url + "/stop", timeout=10)


def getScailableSettings(uiprovider_url="http://127.0.0.1:8081") -> dict:
    """
    Sends a GET request to the provided UI provider URL to retrieve settings and returns them as a dictionary.

    :param uiprovider_url: The URL of the UI provider. Defaults to "http://127.0.0.1:8081".
    :type uiprovider_url: str
    :return: A dictionary containing the settings retrieved from the UI provider.
    :rtype: dict
    """
    response = executeGetRequest(uiprovider_url + "/settings", timeout=10)
    settings_object = json.loads(response.text)
    return settings_object


def getScailableStatus(uiprovider_url="http://127.0.0.1:8081") -> dict:
    """
    Sends a GET request to the specified URL and returns the response as a dictionary.

    :param uiprovider_url: The URL to send the GET request to. Defaults to "http://127.0.0.1:8081".
    :type uiprovider_url: str
    :return: The response from the GET request as a dictionary.
    :rtype: dict
    """
    response = executeGetRequest(uiprovider_url + "/status", timeout=10)
    settings_object = json.loads(response.text)
    return settings_object


def executeGetRequest(url, timeout=10):
    """
    Executes a GET request to a specified URL with a timeout.

    This function sends a GET request to the provided URL and returns the response.
    If the request times out or if there's an error with the request, it raises an exception.

    :param url: The URL to send the GET request to.
    :type url: str
    :param timeout: The timeout for the GET request in seconds. Default is 10 seconds.
    :type timeout: int
    :return: The response from the GET request.
    :rtype: requests.Response
    :raises Exception: If the request times out or if there's a request error.

    :Example:

    >>> executeGetRequest('https://www.example.com')
    <Response [200]>
    """
    try:
        response = requests.get(url, timeout=timeout)
        time.sleep(1)
        response.raise_for_status()  # raise exception for non-2xx status codes
        return response
    except requests.exceptions.Timeout:
        raise Exception(
            f"No response from Edge AI Manager after {timeout} seconds. \n URL: {url}"
        )
    except requests.exceptions.RequestException as e:
        raise Exception(f"Request error: {e}. \n URL: {url}")


def create_shm(size: int) -> ipc.SharedMemory:
    """
    Creates a new shared memory segment with a specified size and initializes it.

    This function allocates a new shared memory segment with a size large enough to hold
    the requested data plus an additional 4 bytes for a header. The header is intended
    to store metadata about the data stored in the shared memory, such as its size.

    Parameters:
    - size (int): The size of the data to be stored in the shared memory, excluding the header.

    Returns:
    - ipc.SharedMemory: A new shared memory segment object initialized with the specified size.
                          The segment is created with IPC_CREAT and IPC_EXCL flags, meaning
                          it will only be created if it does not already exist, and an attempt
                          to attach to an existing segment with the same key will fail.

    Note: The actual size of the shared memory segment is size + 4 bytes to accommodate
          the header.
    """
    # Add enough space for header
    shm_size = size + 4
    shm = ipc.SharedMemory(None, size=shm_size, flags=(ipc.IPC_CREAT | ipc.IPC_EXCL))
    return shm


def write_shm(shm: ipc.SharedMemory, data: bytes):
    """
    Writes data to a shared memory segment.

    This function writes both the size of the data and the data itself to a shared memory segment.
    The size is written first, followed by the data, ensuring that the receiver knows how much data to read.

    @param shm SharedMemory object representing the shared memory segment to write to.
    @type shm ipc.SharedMemory
    @param data Bytes to write to the shared memory segment.
    @type data bytes
    @return None
    @throws Exception If an error occurs during the write operation.
    """
    data_size = len(data)
    # Parse size to bytes
    data_size_bytes = struct.pack("<I", data_size)
    # Write data size
    shm.write(data_size_bytes)
    # Write data
    shm.write(data, offset=4)


def read_shm(shm_key: int) -> bytes:
    """
    Reads data from a shared memory segment using a given key.

    :param shm_key: The key of the shared memory segment to read from.
    :return: The raw data read from the shared memory.

    The function first attaches to the shared memory using the provided key.
    It then reads the size of the data to be read from the first 4 bytes of the shared memory.
    The function then reads the data of the specified size from the shared memory.
    Finally, it detaches from the shared memory and returns the raw data read from the memory.
    """
    # Attach shared memory using key
    shm = ipc.SharedMemory(shm_key, 0, 0)
    # Read header which is always the size of the following message
    size_buf = shm.read(4)
    # Parse header to number
    data_size = struct.unpack("<I", size_buf)[0]
    # Read data of size
    buf = shm.read(data_size, offset=4)
    # Detach from memory again
    shm.detach()
    # Return the raw data read from memory
    return buf


def parseInferenceResults(message: bytes) -> dict:
    parsed_response = msgpack.unpackb(message)
    if "BBoxes_xyxy" in parsed_response:
        for key, value in parsed_response["BBoxes_xyxy"].items():
            parsed_response["BBoxes_xyxy"][key] = list(
                struct.unpack("f" * int(len(value) / 4), value)
            )
    if "Identity" in parsed_response:
        parsed_response["Identity"] = list(
            struct.unpack(
                "f" * int(len(parsed_response["Identity"]) / 4),
                parsed_response["Identity"],
            )
        )
    return parsed_response


def writeInferenceResults(object: dict) -> bytes:
    if "BBoxes_xyxy" in object:
        for key, value in object["BBoxes_xyxy"].items():
            object["BBoxes_xyxy"][key] = struct.pack("f" * len(value), *value)
    if "Identity" in object:
        object["Identity"] = struct.pack(
            "f" * len(object["Identity"]), object["Identity"]
        )
    message_bytes = msgpack.packb(object)
    return message_bytes
