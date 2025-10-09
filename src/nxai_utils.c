#include "nxai_utils.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_MSC_VER)
    // Windows stuff
    #include <basetsd.h>
    #include <direct.h>
    #include <errno.h>
    #include <io.h>

    #include "windows.h"
#else
    // Linux stuff
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include "nxai_threading_utils.h"

// Variable definitions
char* _start_log_filepath = NULL;
char* _rotating_log_filepath = NULL;
char* _log_log_filepath = NULL;
char* _log_prefix = NULL;
char* _old_logfile_path = NULL;

static uint64_t last_timestamp = 0;
size_t logfile_max_size_mb = 10;
static bool start_logfile_full = false;
static size_t logfile_last_size = 0;
static bool _log_to_console = false;
static bool _log_to_file = true;
static int _log_verbosity_level = 1;
FILE* start_logfile;
FILE* rotating_logfile;
nxai_mutex_t rotating_logfile_lock;

// Forward declarations
static void nxai_vvlog(const char* fmt, va_list* args);

#if defined(_MSC_VER)
    #define NXAI_SET_FILE_PERMS _chmod
#else
    #define NXAI_SET_FILE_PERMS chmod
#endif

void nxai_initialize_logging(
    const char* start_log_filepath,
    const char* rotating_log_filepath,
    const char* log_prefix,
    bool log_to_console,
    bool log_to_file,
    int log_verbosity_level)
{
    _start_log_filepath = strdup(start_log_filepath);
    _rotating_log_filepath = strdup(rotating_log_filepath);
    _log_prefix = strdup(log_prefix);
    _log_to_console = log_to_console;
    _log_verbosity_level = log_verbosity_level;
    _log_to_file = log_to_file;
    if (_log_to_file == true)
    {
        // Create and clear log files
        start_logfile = fopen(_start_log_filepath, "w");
        if (start_logfile == NULL)
        {
            printf("Failed to initialise logfile: %s\n", _start_log_filepath);
        }
        NXAI_SET_FILE_PERMS(_start_log_filepath, 0666);
        rotating_logfile = fopen(_rotating_log_filepath, "w");
        if (rotating_logfile == NULL)
        {
            printf("Failed to initialise logfile: %s\n", _rotating_log_filepath);
        }
        rotating_logfile_lock = nxai_initialize_mutex();
        NXAI_SET_FILE_PERMS(_rotating_log_filepath, 0666);
        // Create old log file path
        size_t old_filepath_length = strlen(_rotating_log_filepath) + 5 + 1;
        _old_logfile_path = (char*) malloc(old_filepath_length);
        strcpy(_old_logfile_path, _rotating_log_filepath);
        strcat(_old_logfile_path, ".old");
    }
}

void nxai_finalise_logging()
{
    free(_start_log_filepath);
    free(_rotating_log_filepath);
    free(_log_prefix);
    free(_old_logfile_path);
    if (start_logfile_full == false)
    {
        fclose(start_logfile);
    }
    fclose(rotating_logfile);
}

void nxai_vlog_verbose(const char* fmt, ...)
{
    if (_log_verbosity_level > 1)
    {
        va_list args;
        va_start(args, fmt);
        nxai_vvlog(fmt, &args);
        va_end(args);
    }
}

void nxai_error_log(const char* fmt, ...)
{
    va_list args;
    va_list args_copy;

    // Initialize both va_lists at once
    va_start(args, fmt);
    va_copy(args_copy, args);

    // First, process arguments for stderr printing
    vfprintf(stderr, fmt, args);

    // Process the same arguments for vvlog
    nxai_vvlog(fmt, &args_copy);

    // Clean up both va_lists
    va_end(args);
    va_end(args_copy);
}

void nxai_vlog(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    nxai_vvlog(fmt, &args);
    va_end(args);
}

bool nxai_get_file_size(const char* filepath, size_t* file_size)
{
#if defined(_MSC_VER)
    // Windows specific implementation
    WIN32_FIND_DATAA fileData;
    HANDLE hFile = FindFirstFileA(filepath, &fileData);

    if (hFile != INVALID_HANDLE_VALUE)
    {
        *file_size = (size_t) fileData.nFileSizeLow;
        FindClose(hFile);
        return true;
    }
    else
    {
        printf(
            "An unexpected error occurred accessing log file: %s %s\n",
            filepath,
            strerror(errno));
        return false;
    }

#else
    // Linux specific implementation
    struct stat file_stat;
    if (stat(_start_log_filepath, &file_stat) < 0)
    {
        switch (errno)
        {
            case EACCES: // Permission denied
                printf("Permission denied trying to open logfile: %s.\n", filepath);
            default:
                printf(
                    "An unexpected error occurred accessing log file: %s %s\n",
                    filepath,
                    strerror(errno));
        }
        return false;
    }
    *file_size = file_stat.st_size;
    return true;
#endif
}

static void nxai_vvlog(const char* fmt, va_list* args)
{
    if (_log_verbosity_level == 0 || (_log_to_file == false && _log_to_console == false))
    {
        // Logging is turned off. Return immediately
        return;
    }

    // Copy argument list
    va_list copied_args;
    va_copy(copied_args, *args);

    // Get the current timestamp
    uint64_t timestamp = nxai_current_timestamp_us();

    uint64_t duration = 0;
    if (last_timestamp == 0)
    {
        last_timestamp = timestamp;
    }

    duration = timestamp - last_timestamp;
    last_timestamp = timestamp;

    const char* log_prefix = _log_prefix;
    if (log_prefix == NULL)
    {
        log_prefix = "";
    }

    if (_log_to_console == true)
    {
        // Print to console
        printf("%s%lu %09lu: ", log_prefix, (uint64_t) timestamp / 1000, duration);
        vprintf(fmt, *args);
    }

    if (_start_log_filepath == NULL || _rotating_log_filepath == NULL || _log_to_file == false)
    {
        return;
    }

    FILE* flogfile = NULL;

    // Determine which file to log to
    if (start_logfile_full == false)
    {
        // Check if start logfile is full
        if ((size_t) logfile_last_size < logfile_max_size_mb * 1000000)
        {
            // Write to start_log
            flogfile = start_logfile;
        }
        else
        {
            start_logfile_full = true;
            logfile_last_size = 0;
            fclose(start_logfile);
        }
    }

    if (flogfile == NULL)
    {
        // Start logfile was full, open rotating logfile
        nxai_lock_mutex(&rotating_logfile_lock);
        if ((size_t) logfile_last_size > logfile_max_size_mb * 1000000)
        {
            // Rotating logfile is full, rename to ".old"
            if (rotating_logfile != NULL)
            {
                fclose(rotating_logfile);
                rotating_logfile = NULL;
                remove(_old_logfile_path);
                int result = rename(_rotating_log_filepath, _old_logfile_path);
                if (result != 0)
                {
                    fprintf(stderr, "Error renaming file: %s\n", strerror(errno));
                    nxai_unlock_mutex(&rotating_logfile_lock);
                    return;
                }
            }
            // Create new log file
            rotating_logfile = fopen(_rotating_log_filepath, "w");
            if (rotating_logfile == NULL)
            {
                fprintf(stderr, "Error creating log file");
                nxai_unlock_mutex(&rotating_logfile_lock);
                return;
            }
            logfile_last_size = 0;
        }
        nxai_unlock_mutex(&rotating_logfile_lock);
        // Write to rotating log
        flogfile = rotating_logfile;
    }

    // Write to logfile
    int bytes_written =
        fprintf(flogfile, "%s%lu %09lu: ", log_prefix, (uint64_t) timestamp / 1000, duration);
    if (bytes_written < 0)
    {
        printf("Failed to write to log file!\n");
        return;
    }
    logfile_last_size += bytes_written;
    bytes_written = vfprintf(flogfile, fmt, copied_args);
    if (bytes_written < 0)
    {
        printf("Failed to write to log file!\n");
        return;
    }
    logfile_last_size += bytes_written;
    if (_log_verbosity_level > 1)
    {
        fflush(flogfile); // Flush writing file to make sure latest prints are logged
        fflush(stdout);
    }
}

void nxai_sleep_ms(int milliseconds)
{
#if defined(_MSC_VER)
    // Windows implementation
    Sleep(milliseconds);
#else
    // Linux implementation
    usleep(milliseconds * 1000);
#endif
}

uint64_t nxai_current_timestamp_ms()
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    uint64_t time_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return time_ms;
}

uint64_t nxai_current_timestamp_us()
{
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    uint64_t time_us = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    return time_us;
}

void nxai_chdir(const char* path)
{
#if defined(_MSC_VER)
    // Windows implementation
    int result = _chdir(path);
#else
    // Linux implementation
    chdir(path);
#endif
}

char* _nxai_path_join(int arg_count, ...)
{
// Add separator
#if defined(_MSC_VER)
    // Windows implementation
    char separator = '\\';
#else
    // Linux implementation
    char separator = '/';
#endif

    va_list args;
    char* result = NULL;
    size_t total_length = 0;

    // First pass: calculate total length
    va_start(args, arg_count);
    const char* arg = va_arg(args, const char*);

    for (size_t arg_index = 0; arg_index < arg_count; arg_index++)
    {
        size_t arg_length = strlen(arg);
        total_length += arg_length;

        if (arg[arg_length - 1] != separator)
        {
            total_length += 1; // Add space for separator
        }
        arg = va_arg(args, const char*);
    }

    va_end(args);

    // Allocate memory for result
    result = (char*) malloc(total_length + 1);
    if (!result)
    {
        return NULL;
    }
    char* current_pos = result;

    // Second pass: construct remaining path
    va_start(args, arg_count);
    arg = va_arg(args, const char*);

    bool separator_added = false;
    for (size_t arg_index = 0; arg_index < arg_count; arg_index++)
    {
        size_t arg_length = strlen(arg);

        // Copy argument
        memcpy(current_pos, arg, arg_length);
        current_pos += arg_length;

        if (arg[arg_length - 1] != separator)
        {
            *current_pos = separator;
            current_pos++;
            separator_added = true;
        }
        else
        {
            separator_added = false;
        }

        arg = va_arg(args, const char*);
    }
    if (separator_added == true)
    {
        // Ensure no trailing separator
        current_pos--;
    }

    va_end(args);
    *current_pos = '\0'; // Null terminate

    return result;
}

int nxai_strcasecmp(const char* str1, const char* str2)
{
#if defined(_MSC_VER)
    // Windows implementation
    return _stricmp(str1, str2);
#else
    // Linux implementation
    return strcasecmp(str1, str2);
#endif
}

void nxai_chmod(const char* filepath, int mode)
{
#if defined(_MSC_VER)
    // Windows implementation
    _chmod(filepath, mode);
#else
    // Linux implementation
    chmod(filepath, mode);
#endif
}

#if defined(_MSC_VER)
DWORD get_windows_error(DWORD errorCode, char* buffer, DWORD bufferSize)
{
    return FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer,
        bufferSize,
        NULL);
}
#endif

char* nxai_sprintf(size_t initial_size, const char* fmt, ...)
{
    // Initial allocation
    char* return_string = (char*) malloc(initial_size);
    if (!return_string)
    {
        return NULL;
    }

    va_list args;
    va_start(args, fmt);

    // First attempt to format string
    size_t len = vsnprintf(return_string, initial_size, fmt, args);
    va_end(args);

    // Check if buffer was too small
    if (len >= initial_size)
    {
        // Need larger buffer
        return_string = (char*) realloc(return_string, len + 1);
        if (!return_string)
        {
            return NULL;
        }

        // Restart va_list for second formatting attempt
        va_start(args, fmt);

        // Format string again with larger buffer
        vsnprintf(return_string, len + 1, fmt, args);
        va_end(args);
    }

    return return_string;
}

char* nxai_pointer_to_string(void* pointer)
{
    return nxai_sprintf(32, "%p", pointer); // 32 bytes is typically enough for pointers
}
