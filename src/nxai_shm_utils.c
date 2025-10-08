#include "nxai_shm_utils.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef NXAI_DEBUG
    #include "memory_leak_detector.h"
#endif

#if defined(_MSC_VER)
    // Windows stuff
    #define WIN32_LEAN_AND_MEAN
    #include <basetsd.h>
    #include <errno.h>

    #include "windows.h"
    #include "winsock2.h"
#else
    // Linux stuff
    #include <poll.h>
    #include <sys/select.h>
    #include <sys/shm.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include "nxai_process_utils.h"
#include "nxai_utils.h"

// SHM stuff
#include <fcntl.h>

#define HEADER_BYTES 4

#define SHM_MAX_SIZE 200 * 1024 * 1024 // 200 MB

char* nxai_shm_key_to_string(nxai_shm_t shm)
{
#if defined(_MSC_VER)
    // Windows implementation
    // Copy string so it can be freed
    char* shm_key_string = (char*) malloc(strlen(shm.key));
    strcpy(shm_key_string, shm.key);
#else
    // Linux implementation
    char* shm_key_string = nxai_sprintf(32, "%d", shm.key);
#endif
    return shm_key_string;
}

void nxai_shm_key_from_string(nxai_shm_t* shm, const char* str)
{
#if defined(_MSC_VER)
    // Windows implementation
    shm->key = (char*) malloc(sizeof(char) * MAX_PATH);
    strcpy(shm->key, str);
#else
    // Linux implementation
    // Convert string to integer using strtol for better error handling
    char* endptr;
    errno = 0;
    shm->key = strtol(str, &endptr, 10);

    // Check for errors
    if (errno == ERANGE || *endptr != '\0')
    {
        // Handle conversion error
        shm->key = 0;
    }
#endif
}

char* nxai_shm_id_to_string(nxai_shm_t shm)
{
#if defined(_MSC_VER)
    // Windows implementation
    char* id_string = nxai_pointer_to_string(shm.id);
#else
    // Linux implementation
    char* id_string = nxai_sprintf(32, "%d", shm.id);
#endif
    return id_string;
}

nxai_shm_t nxai_shm_id_from_string(const char* str)
{
    nxai_shm_t shm;

#if defined(_MSC_VER)
    // Windows implementation
    sscanf(str, "%p", &shm.id);
#else
    // Linux implementation
    shm.id = atoi(str);
#endif

    return shm;
}

bool nxai_shm_is_valid(const nxai_shm_t* shm)
{
#if defined(_MSC_VER)
    // Windows implementation
    if (shm->id == NULL)
    {
        return false;
    }
    else
    {
        return true;
    }
#else
    // Linux implementation
    if (shm->id == -1)
    {
        return false;
    }
    else
    {
        return true;
    }
#endif
}

nxai_shm_t nxai_shm_create_random(size_t size)
{
    // Seed randomizer to ensure unique SHM names
    srand((unsigned int) nxai_current_timestamp_us());
#if defined(_MSC_VER)
    // Windows implementation
    nxai_shm_t new_shm;
    new_shm.id = NULL;
    new_shm.key = (char*) malloc(sizeof(char) * MAX_PATH);

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    for (size_t retry_counter = 0; retry_counter < 5; retry_counter++)
    {
        // Generate random name for anonymous mapping
        sprintf_s(new_shm.key, MAX_PATH, "Global_RandomSHM_%08X", rand());

        // Create file mapping object
        HANDLE hMapFile = CreateFileMappingA(
            INVALID_HANDLE_VALUE, // Use paging file
            &saAttr, // Default security attributes
            PAGE_READWRITE | SEC_RESERVE, // Read/write access
            0, // High DWORD of size
            SHM_MAX_SIZE, // Low DWORD of size
            new_shm.key // Name of mapping object
        );

        if (hMapFile == NULL)
        {
            char error_string[1024];
            get_windows_error(GetLastError(), error_string, 1024);
            nxai_vlog("Warning: Could not create SHM: %s\n", error_string);
            break;
        }
        else if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            // SHM with key exists, regenerate key and try again
            nxai_vlog("SHM with key %s already exists. Trying again...\n", new_shm.key);
            continue;
        }
        else
        {
            // Creation succesful
            new_shm.id = hMapFile;
            break;
        }
    }

    // Commit memory
    nxai_shm_realloc(&new_shm, size);

    // Use process ID as identifier
    return new_shm;
#else
    // Linux implementation
    int new_id = -1;
    key_t shm_key;
    // Keep trying random keys until unused is found
    while (new_id == -1)
    {
        shm_key = rand();
        new_id = shmget(shm_key, size + HEADER_BYTES, 0666 | IPC_CREAT | IPC_EXCL);
    }
    return (nxai_shm_t) {.id = new_id, .key = shm_key};
#endif
}

nxai_shm_t nxai_shm_create(const char* path, int project_id, size_t size)
{
#if defined(_MSC_VER)
    // Windows implementation
    nxai_shm_t new_shm;
    sprintf_s(new_shm.key, MAX_PATH, "\\\\\\.\\Global\\SHM_%s_%d", path, project_id);

    SECURITY_ATTRIBUTES saAttr;
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = NULL;

    new_shm.id = CreateFileMappingA(
        INVALID_HANDLE_VALUE, // Use paging file
        &saAttr, // Security attributes
        PAGE_READWRITE | SEC_RESERVE, // Read/write access
        0, // High DWORD of size
        SHM_MAX_SIZE, // Low DWORD of size
        new_shm.key // Name of mapping object
    );

    // Commit memory
    nxai_shm_realloc(&new_shm, size);

    // Use process ID as identifier
    return new_shm;
#else
    // Linux implementation
    key_t shm_key = ftok(path, project_id);
    shm_id_t new_id = shmget(shm_key, size + HEADER_BYTES, 0666 | IPC_CREAT);
    if (new_id == -1)
    {
        perror("Failed to create SHM:");
    }
    return (nxai_shm_t) {.id = new_id, .key = shm_key};
#endif
}

bool nxai_shm_get_id(nxai_shm_t* shm)
{
#if defined(_MSC_VER)
    // Windows implementation
    HANDLE shm_id = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shm->key);
    if (shm_id == NULL)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog("Warning: Could not get SHM ID: %.*s\n", error_length, error_string);
        return false;
    }
    else
    {
        shm->id = shm_id;
        return true;
    }
#else
    // Linux implementation
    shm->id = shmget(shm->key, 0, 0);
    if (shm->id == -1)
    {
        printf("Could not get SHM %d : %s\n", __LINE__, strerror(errno));
        return false;
    }
    return true;
#endif
}

bool nxai_shm_pointer_valid(void* shm_buffer)
{
#if defined(_MSC_VER)
    // Windows implementation
    return shm_buffer != NULL; // In Windows the pointer will be NULL if mapping failed
#else
    // Linux implementation
    return shm_buffer != (void*) -1; // In Linux the pointer will be -1 if mapping failed
#endif
}

void* nxai_shm_attach(nxai_shm_t shm)
{
#if defined(_MSC_VER)
    // Windows implementation
    LPVOID shm_pointer = MapViewOfFile(
        shm.id, // Handle to map object
        FILE_MAP_ALL_ACCESS, // Desired access
        0, // File offset (high DWORD)
        0, // File offset (low DWORD)
        0 // Number of bytes to map
    );
    if (shm_pointer == NULL)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog("Warning: Could not attach shared memory: %.*s\n", error_length, error_string);
    }

    return shm_pointer;
#else
    // Linux implementation
    // Attach the shared memory segment to the process's address space.
    // This is done by calling the shmat() function with the shared memory ID.
    // The function returns a pointer to the attached shared memory segment.
    void* result = shmat(shm.id, NULL, 0);
    return result;
#endif
}

void nxai_shm_write_to_attached(void* shm_buffer, const char* data, uint32_t size)
{
    // Write the size of the data to the beginning of the shared memory segment.
    // This is done by copying the size (which is an integer) to the shared memory segment.
    // The size is copied as a 4-byte value, as the size is represented as a 32-bit unsigned
    // integer.
    memcpy(shm_buffer, &size, HEADER_BYTES);

    // Write the data to the shared memory segment.
    // This is done by copying the data to the shared memory segment, starting from the 4th byte,
    // as the first 4 bytes are used to store the size of the data.
    memcpy(((char*) shm_buffer) + HEADER_BYTES, data, size);
}

bool nxai_shm_write(const nxai_shm_t* shm, const char* data, uint32_t size)
{
#if defined(_MSC_VER)
    // Windows implementation
    LPVOID view = MapViewOfFile(shm->id, FILE_MAP_ALL_ACCESS, 0, 0, size + HEADER_BYTES);

    if (view == NULL)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog(
            "Warning: Could not obtain shared memory for writing: %.*s\n",
            error_length,
            error_string);
        return false;
    }

    nxai_shm_write_to_attached(view, data, size);

    UnmapViewOfFile(view);
    return true;
#else
    // Linux implementation
    void* shm_pointer = nxai_shm_attach(*shm);
    if (shm_pointer == (void*) -1)
    {
        return false;
    }

    nxai_shm_write_to_attached(shm_pointer, data, size);

    // Detach the shared memory segment from the process's address space.
    // This is done by calling the shmdt() function with the pointer to the shared memory segment.
    shmdt(shm_pointer);

    return true;
#endif
}

void nxai_shm_read_from_attached(void* shm_pointer, size_t* data_length, char** payload_data)
{
    // The first 4 bytes of the shared memory is always the size of the data
    uint32_t size;
    memcpy(&size, shm_pointer, HEADER_BYTES);
    *data_length = (size_t) size;
    // Return pointer to the payload data after the size header
    *payload_data = (char*) shm_pointer + HEADER_BYTES;
}

void* nxai_shm_read(nxai_shm_t* shm, size_t* data_length, char** payload_data)
{
#if defined(_MSC_VER)
    // Windows implementation
    LPVOID view = MapViewOfFile(shm->id, FILE_MAP_READ, 0, 0, 0);

    if (view == NULL)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog("Warning: Could not read shared memory: %.*s\n", error_length, error_string);
        return NULL;
    }

    nxai_shm_read_from_attached(view, data_length, payload_data);
    return view;
#else
    // Linux implementation
    void* shm_pointer = shmat(shm->id, NULL, 0);
    if (shm_pointer == (void*) -1)
    {
        return NULL;
    }
    nxai_shm_read_from_attached(shm_pointer, data_length, payload_data);
    // Return pointer to data after size
    return shm_pointer;
#endif
}

void nxai_shm_close(void* memory_address)
{
#if defined(_MSC_VER)
    // Windows implementation
    UnmapViewOfFile(memory_address);
#else
    // Linux implementation
    // Detach memory from this process
    shmdt(memory_address);
#endif
}

int nxai_shm_destroy(const nxai_shm_t* shm)
{
#if defined(_MSC_VER)
    // Windows implementation
    int result = CloseHandle(shm->id);
    return result ? 0 : -1;
#else
    // Linux implementation
    return shmctl(shm->id, IPC_RMID, NULL);
#endif
}

bool nxai_shm_realloc(nxai_shm_t* shm, size_t new_size)
{
#if defined(_MSC_VER)
    // Windows implementation
    // Commit memory to ensure space on disk
    void* base_address = nxai_shm_attach(*shm);
    if (base_address == NULL)
    {
        return false;
    }
    if (VirtualAlloc(base_address, new_size + HEADER_BYTES, MEM_COMMIT, PAGE_READWRITE) == NULL)
    {
        char error_string[1024];
        DWORD error_length = get_windows_error(GetLastError(), error_string, 1024);
        nxai_vlog("Error: Could not realloc shared memory: %.*s\n", error_length, error_string);
        return false;
    };
    UnmapViewOfFile(base_address);
    return true;
#else
    // Linux implementation
    // Remove old SHM
    if (nxai_shm_destroy(shm) != 0)
    {
        nxai_error_log("Error! Could not destroy Shared Memory segment with ID %d.\n", shm->id);
        return false;
    }

    shm->id = shmget(shm->key, new_size + HEADER_BYTES, 0666 | IPC_CREAT);

    return true;
#endif
}

size_t nxai_shm_get_size(nxai_shm_t* shm)
{
#if defined(_MSC_VER)
    // Windows implementation
    MEMORY_BASIC_INFORMATION memInfo;
    VirtualQuery(MapViewOfFile(shm->id, FILE_MAP_READ, 0, 0, 0), &memInfo, sizeof(memInfo));
    return memInfo.RegionSize - HEADER_BYTES;
#else
    // Linux implementation
    struct shmid_ds buf;
    shmctl(shm->id, IPC_STAT, &buf);
    return buf.shm_segsz - HEADER_BYTES;
#endif
}
