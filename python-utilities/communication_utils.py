import requests
import os
import struct
import socket
import time
import json

def patchSettings(settings_contents: dict, uiprovider_url = "http://127.0.0.1:8081") -> bool:
    print("Loading test settings...")
    # Send test settings as a PATCH request
    try:
        response = requests.patch(uiprovider_url + "/settings", data=settings_contents)
        response.raise_for_status()  # raise exception for non-2xx status codes
    except requests.exceptions.Timeout:
        print("Request timed out")
        return False
    except requests.exceptions.RequestException as e:
        print("Error:", e)
        return False
    return True

def startUnixSocketServer(socket_path = "/opt/sclbl/sockets/output_socket.sock") -> socket.socket:
    # Create a Unix socket server
    # Set the path for the Unix socket
    # remove the socket file if it already exists
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
    message_bytes = bytes(message, "utf-8")
    message_length = struct.pack("<I", len(message_bytes))
    connection.sendall(message_length)
    connection.sendall(message_bytes)

def sendSocketMessage(message: str, sclbl_input_socket_path : str = "/opt/sclbl/sockets/sclblmod.sock"):
    client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    client.settimeout(10)
    client.connect(sclbl_input_socket_path)
    sendMessageOverConnection(client,message)
    client.close()

def sendSocketBytes(message: str, bytes_array: bytes, sclbl_input_socket_path: str = "/opt/sclbl/sockets/sclblmod.sock"):
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
    return executeGetRequest(uiprovider_url+"/start", timeout=10)

def stopScailableRuntime(uiprovider_url = "http://127.0.0.1:8081"):
    return executeGetRequest(uiprovider_url+"/stop", timeout=10)

def getScailableSettings(uiprovider_url = "http://127.0.0.1:8081") -> dict:
    response = executeGetRequest(uiprovider_url+"/settings", timeout=10)
    settings_object = json.loads(response.text)
    return settings_object

def getScailableStatus(uiprovider_url = "http://127.0.0.1:8081") -> dict:
    response = executeGetRequest(uiprovider_url+"/status", timeout=10)
    settings_object = json.loads(response.text)
    return settings_object

def executeGetRequest(url, timeout=10):
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