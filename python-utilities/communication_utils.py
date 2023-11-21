import requests
import os
import struct
import socket
import time
import json

def patchSettings(settings_contents: dict, uiprovider_url = "http://127.0.0.1:8081") -> bool:
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
        print("Request timed out")  # If the request times out, print an error message and return False
        return False
    except requests.exceptions.RequestException as e:
        # If there is a different request exception, print the error message and return False
        print("Error:", e)
        return False

    # If the PATCH request was successful, return True
    return True

def startUnixSocketServer(socket_path = "/opt/sclbl/sockets/output_socket.sock") -> socket.socket:
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


def waitForSocketMessage(server: socket.socket, timeout=10) -> (str,socket.socket):
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
    # receive data from the client
    data = connection.recv(4)
    message_length = struct.unpack("<I", data)[0]

    data_received = 0
    data: bytes = b""
    while data_received < message_length:
        data += connection.recv(message_length - data_received)
        data_received = len(data)
        if not data:
            break

    decoded = data.decode("utf-8")

    return decoded,connection

def sendMessageOverConnection(connection: socket.socket, message: str):
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

    # Convert the message to bytes
    message_bytes = bytes(message, "utf-8")

    # Calculate the length of the message
    message_length = struct.pack("<I", len(message_bytes))

    # Send the length of the message over the connection
    connection.sendall(message_length)

    # Send the message over the connection
    connection.sendall(message_bytes)


def sendSocketMessage(message: str, sclbl_input_socket_path: str = "/opt/sclbl/sockets/sclblmod.sock"):
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


def sendSocketBytes(message: str, bytes_array: bytes, sclbl_input_socket_path: str = "/opt/sclbl/sockets/sclblmod.sock"):
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

def startScailableRuntime(uiprovider_url = "http://127.0.0.1:8081"):
    """
    Starts the Scailable Runtime by making a GET request to the provided URL.

    :param uiprovider_url: The URL of the UI provider. Defaults to "http://127.0.0.1:8081".
    :type uiprovider_url: str
    :return: The response from the GET request.
    """
    return executeGetRequest(uiprovider_url+"/start", timeout=10)

def stopScailableRuntime(uiprovider_url = "http://127.0.0.1:8081"):
    """
    Sends a GET request to stop the Scailable runtime.

    :param uiprovider_url: The URL of the UI provider. Default is "http://127.0.0.1:8081".
    :type uiprovider_url: str
    :return: The response from the GET request.
    """
    return executeGetRequest(uiprovider_url+"/stop", timeout=10)

def getScailableSettings(uiprovider_url = "http://127.0.0.1:8081") -> dict:
    """
    Sends a GET request to the provided UI provider URL to retrieve settings and returns them as a dictionary.

    :param uiprovider_url: The URL of the UI provider. Defaults to "http://127.0.0.1:8081".
    :type uiprovider_url: str
    :return: A dictionary containing the settings retrieved from the UI provider.
    :rtype: dict
    """
    response = executeGetRequest(uiprovider_url+"/settings", timeout=10)
    settings_object = json.loads(response.text)
    return settings_object

def getScailableStatus(uiprovider_url = "http://127.0.0.1:8081") -> dict:
    """
    Sends a GET request to the specified URL and returns the response as a dictionary.

    :param uiprovider_url: The URL to send the GET request to. Defaults to "http://127.0.0.1:8081".
    :type uiprovider_url: str
    :return: The response from the GET request as a dictionary.
    :rtype: dict
    """
    response = executeGetRequest(uiprovider_url+"/status", timeout=10)
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