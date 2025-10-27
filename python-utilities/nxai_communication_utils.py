import ctypes
from ctypes import wintypes
import sys
from typing import Optional, Union, Callable
import os
import msgpack
import struct

script_location = os.path.dirname(os.path.realpath(__file__))

# Define platform-specific types
if sys.platform == "win32":
    # Windows definitions
    shm_id_t = wintypes.HANDLE
    shm_key_t = wintypes.LPSTR
    _utilities_libary_name = "nxai-c-utilities-shared.dll"
    # On Windows, SOCKET is a UINT_PTR, so ctypes.c_size_t is a safe equivalent
    nxai_socket_t = ctypes.c_size_t
else:
    # Unix/Linux definitions
    shm_id_t = ctypes.c_int
    shm_key_t = ctypes.c_long  # key_t is typically long int
    _utilities_libary_name = "libnxai-c-utilities-shared.so"
    # On Unix/Linux, socket descriptors are standard integers
    nxai_socket_t = ctypes.c_int


# Define the function signature for the listener callback
LISTENER_CALLBACK = ctypes.CFUNCTYPE(None, ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int)


# Define the shared memory structure
class nxai_shm_t(ctypes.Structure):
    _fields_ = [("key", shm_key_t), ("id", shm_id_t)]


# Declare the shared library
global _lib
_lib = None


class nxai_shm_t(ctypes.Structure):
    _fields_ = [("key", shm_key_t), ("id", shm_id_t)]


class SharedMemoryError(Exception):
    """Custom exception for shared memory operations"""

    pass


class ExitSignal:
    """Custom class to signal exit"""

    pass


class SocketError(Exception):
    """Custom exception for socket operations."""

    pass


class SocketTimeout(Exception):
    """Custom exception for socket operations."""

    pass


class SharedMemory:
    """
    A Pythonic wrapper around the shared memory library.

    Provides automatic resource management and convenient methods for
    working with shared memory segments.
    """

    def __init__(self, size: int = 0, key: str = None):
        if _lib is None:
            initializeLibrary()

        self._handle = None
        self._attached_memory = None

        if key is not None:
            self.open_from_key(key)

        if size > 0:
            self.create(size)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    @property
    def handle(self) -> Optional[nxai_shm_t]:
        """Get the underlying shared memory handle"""
        return self._handle

    @property
    def is_valid(self) -> bool:
        """Check if the shared memory handle is valid"""
        if not self._handle:
            return False
        return _lib.nxai_shm_is_valid(ctypes.byref(self._handle))

    @property
    def key(self) -> str:
        key_c = _lib.nxai_shm_key_to_string(self._handle)
        return key_c.decode("utf-8")

    def create(self, size: int) -> None:
        """Create a new shared memory segment"""
        self._handle = _lib.nxai_shm_create_random(ctypes.c_size_t(size))
        if not self.is_valid:
            raise SharedMemoryError(f"Failed to create shared memory segment!")

    def open_from_key(self, key: str) -> None:
        """
        Open a shared memory segment using a string key.

        This function populates the handle by calling the C library's
        nxai_shm_key_from_string function.
        """
        if self.is_valid:
            raise SharedMemoryError("This object already manages a valid shared memory handle.")

        # The C function expects a pointer to a handle to populate it
        handle_to_populate = nxai_shm_t()
        encoded_key = key.encode("utf-8")

        _lib.nxai_shm_key_from_string(ctypes.byref(handle_to_populate), encoded_key)

        _lib.nxai_shm_get_id(ctypes.byref(handle_to_populate))

        # Assign the now-populated handle to the instance
        self._handle = handle_to_populate

        # Verify that the handle is now valid
        if not self.is_valid:
            raise SharedMemoryError(f"Failed to open shared memory with key: '{key}'")

    def attach(self) -> memoryview:
        """Attach to the shared memory segment and return a memory view"""
        if not self.is_valid:
            raise SharedMemoryError("Invalid shared memory handle")

        self._attached_memory = _lib.nxai_shm_attach(self._handle)
        if not self._attached_memory:
            raise SharedMemoryError("Failed to attach to shared memory")

        size = _lib.nxai_shm_get_size(ctypes.byref(self._handle))
        return memoryview((ctypes.cast(self._attached_memory, ctypes.POINTER(ctypes.c_char)), size))

    def detach(self) -> None:
        """Detach from the shared memory segment"""
        if self._attached_memory:
            _lib.nxai_shm_close(self._attached_memory)
            self._attached_memory = None

    def close(self) -> None:
        """Close the shared memory segment"""
        if self._handle:
            self.detach()
            status = _lib.nxai_shm_destroy(ctypes.byref(self._handle))
            if status != 0:
                raise SharedMemoryError(f"Failed to destroy shared memory (status: {status})")

    def resize(self, new_size: int) -> bool:
        """Resize the shared memory segment"""
        if not self.is_valid:
            raise SharedMemoryError("Invalid shared memory handle")

        return _lib.nxai_shm_realloc(ctypes.byref(self._handle), new_size)

    def write(self, data: Union[str, bytes]) -> None:
        """Write data to the shared memory segment"""
        if isinstance(data, str):
            data = data.encode()

        if not self.is_valid:
            raise SharedMemoryError("Invalid shared memory handle")

        success = _lib.nxai_shm_write(ctypes.byref(self._handle), data, len(data))
        if not success:
            raise SharedMemoryError("Failed to write to shared memory")

    def read(self) -> bytes:
        """Read data from the shared memory segment"""
        if not self.is_valid:
            raise SharedMemoryError("Invalid shared memory handle")

        size = ctypes.c_size_t()
        payload_ptr = ctypes.c_char_p()
        data = _lib.nxai_shm_read(ctypes.byref(self._handle), ctypes.byref(size), ctypes.byref(payload_ptr))
        # Get raw address and don't allow ctypes to implicitly convert to bytes until first NULL
        address = ctypes.cast(payload_ptr, ctypes.c_void_p).value

        if not data:
            raise SharedMemoryError("Failed to read from shared memory")

        result = ctypes.string_at(address, size.value)
        return result


# Function prototypes
def initializeLibrary(library_path: str = None):

    library_search_paths = [
        os.path.join(os.path.dirname(sys.argv[0]), _utilities_libary_name),
        os.path.join(os.getcwd(), _utilities_libary_name),
        os.path.join(script_location, _utilities_libary_name),
    ]

    if library_path is None:
        for search_path in library_search_paths:
            print("Looking for library at path:", search_path)
            if os.path.exists(search_path):
                library_path = search_path
                break
        else:
            print("Error! Could not find", _utilities_libary_name, "! Call 'initializeLibrary' function and provide path to file.")
            raise Exception

    global _lib
    _lib = ctypes.CDLL(library_path)  # Adjust path as needed

    ###################################################################
    #####################   SHM FUNCTIONALITY   #######################
    ###################################################################

    # String conversion functions
    _lib.nxai_shm_key_to_string.argtypes = [nxai_shm_t]
    _lib.nxai_shm_key_to_string.restype = ctypes.c_char_p

    _lib.nxai_shm_key_from_string.argtypes = [ctypes.POINTER(nxai_shm_t), ctypes.c_char_p]
    _lib.nxai_shm_key_from_string.restype = None

    _lib.nxai_shm_id_to_string.argtypes = [nxai_shm_t]
    _lib.nxai_shm_id_to_string.restype = ctypes.c_char_p

    _lib.nxai_shm_id_from_string.argtypes = [ctypes.c_char_p]
    _lib.nxai_shm_id_from_string.restype = nxai_shm_t

    # Validation functions
    _lib.nxai_shm_is_valid.argtypes = [ctypes.POINTER(nxai_shm_t)]
    _lib.nxai_shm_is_valid.restype = ctypes.c_bool

    _lib.nxai_shm_get_id.argtypes = [ctypes.POINTER(nxai_shm_t)]
    _lib.nxai_shm_get_id.restype = ctypes.c_bool

    _lib.nxai_shm_pointer_valid.argtypes = [ctypes.c_void_p]
    _lib.nxai_shm_pointer_valid.restype = ctypes.c_bool

    # Memory management functions
    _lib.nxai_shm_attach.argtypes = [nxai_shm_t]
    _lib.nxai_shm_attach.restype = ctypes.c_void_p

    _lib.nxai_shm_close.argtypes = [ctypes.c_void_p]
    _lib.nxai_shm_close.restype = None

    _lib.nxai_shm_destroy.argtypes = [ctypes.POINTER(nxai_shm_t)]
    _lib.nxai_shm_destroy.restype = ctypes.c_int

    # Data operations
    _lib.nxai_shm_write.argtypes = [ctypes.POINTER(nxai_shm_t), ctypes.c_char_p, ctypes.c_uint]
    _lib.nxai_shm_write.restype = ctypes.c_bool

    _lib.nxai_shm_read.argtypes = [ctypes.POINTER(nxai_shm_t), ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char_p)]
    _lib.nxai_shm_read.restype = ctypes.c_void_p

    # Size management
    _lib.nxai_shm_get_size.argtypes = [ctypes.POINTER(nxai_shm_t)]
    _lib.nxai_shm_get_size.restype = ctypes.c_size_t

    _lib.nxai_shm_realloc.argtypes = [ctypes.POINTER(nxai_shm_t), ctypes.c_size_t]
    _lib.nxai_shm_realloc.restype = ctypes.c_bool

    _lib.nxai_shm_create_random.argtypes = [ctypes.c_size_t]  # Input parameter
    _lib.nxai_shm_create_random.restype = nxai_shm_t  # Return type

    ###################################################################
    #####################   SOCKET FUNCTIONALITY   ####################
    ###################################################################

    # nxai_socket_initialize_sockets
    _lib.nxai_socket_initialize_sockets.argtypes = []
    _lib.nxai_socket_initialize_sockets.restype = ctypes.c_int

    # nxai_socket_is_valid
    _lib.nxai_socket_is_valid.argtypes = [ctypes.POINTER(nxai_socket_t)]
    _lib.nxai_socket_is_valid.restype = ctypes.c_bool

    # nxai_socket_create_listener
    _lib.nxai_socket_create_listener.argtypes = [ctypes.c_char_p]
    _lib.nxai_socket_create_listener.restype = nxai_socket_t

    # nxai_socket_receive_on_connection
    _lib.nxai_socket_receive_on_connection.argtypes = [nxai_socket_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_uint32)]
    _lib.nxai_socket_receive_on_connection.restype = None

    # nxai_socket_await_message
    _lib.nxai_socket_await_message.argtypes = [nxai_socket_t, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_uint32)]
    _lib.nxai_socket_await_message.restype = nxai_socket_t

    # nxai_socket_start_listener
    _lib.nxai_socket_start_listener.argtypes = [ctypes.c_char_p, LISTENER_CALLBACK]
    _lib.nxai_socket_start_listener.restype = ctypes.c_int32

    # nxai_delete_socket_file
    _lib.nxai_delete_socket_file.argtypes = [ctypes.c_char_p]
    _lib.nxai_delete_socket_file.restype = None

    # nxai_socket_connect
    _lib.nxai_socket_connect.argtypes = [ctypes.c_char_p]
    _lib.nxai_socket_connect.restype = nxai_socket_t

    # nxai_socket_send
    _lib.nxai_socket_send.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32]
    _lib.nxai_socket_send.restype = None

    # nxai_close_socket
    _lib.nxai_close_socket.argtypes = [nxai_socket_t]
    _lib.nxai_close_socket.restype = ctypes.c_int

    # nxai_socket_send_receive_message
    _lib.nxai_socket_send_receive_message.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_size_t)]
    _lib.nxai_socket_send_receive_message.restype = ctypes.c_uint32

    # nxai_socket_send_to_connection
    _lib.nxai_socket_send_to_connection.argtypes = [nxai_socket_t, ctypes.c_char_p, ctypes.c_uint32]
    _lib.nxai_socket_send_to_connection.restype = ctypes.c_bool

    # Initialize the underlying socket system (e.g., WSAStartup on Windows)
    if _lib.nxai_socket_initialize_sockets() != 0:
        raise SocketError("Failed to initialize the socket system.")


class SocketConnection:
    """Represents an active socket connection (either client or accepted)."""

    def __init__(self, socket_fd: nxai_socket_t, socket_path: str):
        if _lib is None:
            initializeLibrary()
        self._socket_fd = socket_fd
        self._socket_path = socket_path

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    @property
    def is_valid(self) -> bool:
        """Check if the socket file descriptor is valid."""
        # A common convention is that invalid sockets are -1 or 0.
        # This might need adjustment based on the C library's behavior.
        return self._socket_fd is not None and self._socket_fd.value > 0

    def send(self, data: Union[str, bytes]) -> None:
        """Sends data over the connection."""
        if not self.is_valid:
            raise SocketError("Socket is not valid or has been closed.")
        if isinstance(data, str):
            data = data.encode("utf-8")

        success = _lib.nxai_socket_send_to_connection(self._socket_fd, data, len(data))
        if not success:
            raise SocketError("Failed to send data to the connection.")

    def receive(self) -> bytes:
        """Receives data from the connection."""
        if not self.is_valid:
            raise SocketError("Socket is not valid or has been closed.")

        allocated_size = ctypes.c_size_t()
        message_length = ctypes.c_uint32()
        payload_ptr = ctypes.c_char_p()  # Will hold the char* set by the C function

        # The C function is called, which allocates and fills the buffer
        _lib.nxai_socket_receive_on_connection(self._socket_fd, ctypes.byref(allocated_size), ctypes.byref(payload_ptr), ctypes.byref(message_length))
        # Get raw address and don't allow ctypes to implicitly convert to bytes until first NULL
        address = ctypes.cast(payload_ptr, ctypes.c_void_p).value
        if not address or message_length.value == 0:
            raise SocketTimeout("Timed out waiting for message on connection")

        # Copy the data from the C buffer into a Python bytes object
        result = ctypes.string_at(address, message_length.value)

        # NOTE: If the C library has a function to free the payload_ptr,
        # it should be called here to prevent memory leaks.
        # e.g., _lib.nxai_free_buffer(payload_ptr)

        return result

    def close(self) -> None:
        """Closes the socket connection."""
        if self.is_valid:
            _lib.nxai_close_socket(self._socket_fd)
            self._socket_fd = None


class SocketClient(SocketConnection):
    """A client socket that connects to a listening socket."""

    def __init__(self, socket_path: str):
        if _lib is None:
            initializeLibrary()

        c_socket_path = socket_path.encode("utf-8")
        client_fd = _lib.nxai_socket_connect(c_socket_path)
        if isinstance(client_fd, int):
            client_fd = ctypes.c_int(client_fd)

        # Assuming invalid socket FD is not a positive number
        if client_fd is None or client_fd.value <= 0:
            raise ConnectionRefusedError(f"Failed to connect to socket at '{socket_path}'")

        super().__init__(client_fd, socket_path)


class SocketListener:
    """A listening socket that can accept incoming connections."""

    def __init__(self, socket_path: str):
        if _lib is None:
            initializeLibrary()

        self._socket_path = socket_path
        c_socket_path = self._socket_path.encode("utf-8")
        self._listener_fd = _lib.nxai_socket_create_listener(c_socket_path)
        if isinstance(self._listener_fd, int):
            self._listener_fd = nxai_socket_t(self._listener_fd)

        if self._listener_fd is None or self._listener_fd.value <= 0:
            raise SocketError(f"Failed to create listener socket at '{self._socket_path}'")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def accept(self) -> tuple[SocketConnection, bytes]:
        """
        Waits for and accepts an incoming connection.

        Returns a tuple containing a new SocketConnection object for the
        accepted connection and the initial message received.
        """
        allocated_size = ctypes.c_size_t()
        message_length = ctypes.c_uint32()
        payload_ptr = ctypes.c_char_p()

        connection_fd = _lib.nxai_socket_await_message(self._listener_fd, ctypes.byref(allocated_size), ctypes.byref(payload_ptr), ctypes.byref(message_length))
        if isinstance(connection_fd, int):
            connection_fd = nxai_socket_t(connection_fd)
        if _lib.nxai_socket_is_valid(ctypes.byref(connection_fd)) == False:
            raise SocketTimeout

        # Get raw address and don't allow ctypes to implicitly convert to bytes until first NULL
        address = ctypes.cast(payload_ptr, ctypes.c_void_p).value
        if not address:
            # This case might mean connection closed or error
            _lib.nxai_close_socket(connection_fd)
            raise ConnectionAbortedError("Accepted connection but failed to receive initial message.")

        # Create the connection object and copy the initial message data
        connection = SocketConnection(connection_fd, self._socket_path)
        initial_message = ctypes.string_at(address, message_length.value)

        return connection, initial_message

    def close(self) -> None:
        """Closes the listener socket and deletes the socket file."""
        if self._listener_fd is not None:
            _lib.nxai_close_socket(self._listener_fd)
            self._listener_fd = None

        # Clean up the socket file from the filesystem
        c_socket_path = self._socket_path.encode("utf-8")
        _lib.nxai_delete_socket_file(c_socket_path)


def send_message(socket_path: str, data: Union[str, bytes]) -> None:
    """Sends a one-shot message without establishing a persistent connection."""
    if _lib is None:
        initializeLibrary()

    if isinstance(data, str):
        data = data.encode("utf-8")

    c_socket_path = socket_path.encode("utf-8")
    _lib.nxai_socket_send(c_socket_path, data, len(data))


def send_receive_message(socket_path: str, data: Union[str, bytes]) -> bytes:
    """Sends a message and waits for a reply in a single operation."""
    if _lib is None:
        initializeLibrary()

    if isinstance(data, str):
        data = data.encode("utf-8")

    c_socket_path = socket_path.encode("utf-8")

    return_payload_ptr = ctypes.c_char_p()
    allocated_size = ctypes.c_size_t()

    return_length = _lib.nxai_socket_send_receive_message(c_socket_path, data, len(data), ctypes.byref(return_payload_ptr), ctypes.byref(allocated_size))

    if return_length == 0 or not return_payload_ptr.value:
        raise SocketError("Failed to receive a reply.")

    result = ctypes.string_at(return_payload_ptr.value, return_length)

    # NOTE: Free the return_payload_ptr buffer here if required by the C library.

    return result


def start_listener_with_callback(socket_path: str, callback: Callable[[bytes, int], None]) -> None:
    """
    Starts a blocking listener that invokes a Python callback for each message.

    Args:
        socket_path: The path for the socket file.
        callback: A Python function that accepts (bytes, int) where the int is
                  the connection file descriptor.
    """
    if _lib is None:
        initializeLibrary()

    def c_callback_wrapper(msg_ptr, msg_len, conn_fd):
        """Wrapper to convert C types to Python types for the user callback."""
        message = ctypes.string_at(msg_ptr, msg_len)
        callback(message, conn_fd)

    # Keep a reference to the C callback object to prevent it from being garbage collected
    c_callback = LISTENER_CALLBACK(c_callback_wrapper)

    # This is a blocking call
    status = _lib.nxai_socket_start_listener(socket_path.encode("utf-8"), c_callback)
    if status != 0:
        raise SocketError(f"Listener failed with status code: {status}")


def set_interrupt_signal(interrupt: bool) -> None:
    """Sets the global interrupt signal to stop blocking listeners."""
    if _lib is None:
        initializeLibrary()

    interrupt_signal = ctypes.c_bool.in_dll(_lib, "nxai_socket_interrupt_signal")
    interrupt_signal.value = interrupt


def parseInferenceResults(message: bytes) -> dict:
    parsed_response = msgpack.unpackb(message)
    if "EXIT" in parsed_response:
        return ExitSignal()
    if "BBoxes_xyxy" in parsed_response:
        for key, value in parsed_response["BBoxes_xyxy"].items():
            parsed_response["BBoxes_xyxy"][key] = list(struct.unpack("f" * int(len(value) / 4), value))
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
        object["Identity"] = struct.pack("f" * len(object["Identity"]), object["Identity"])
    message_bytes = msgpack.packb(object)
    return message_bytes
