#include "nxai_process_utils.h"
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

pid_t nxai_start_process( char *const argv[], const char *output_filepath, int *output_fd ) {
    pid_t child_pid;
    *output_fd = open( output_filepath, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR );
    if ( *output_fd == -1 ) {
        printf( "Warning: Could not open log filepath for process: %s\n", output_filepath );
        perror( "open" );
        exit( EXIT_FAILURE );
    }

    char *envp[] = { NULL };

    // Initialize file actions and attributes objects
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attrp;
    posix_spawn_file_actions_init( &file_actions );
    posix_spawn_file_actions_adddup2( &file_actions, *output_fd, STDOUT_FILENO );// Redirect STDOUT
    posix_spawn_file_actions_adddup2( &file_actions, *output_fd, STDERR_FILENO );// Redirect STDERR
    posix_spawnattr_init( &attrp );

    // Spawn a new process
    if ( posix_spawn( &child_pid, argv[0], &file_actions, &attrp, argv, envp ) != 0 ) {
        // Could not start
        return 1;
    }

    // Cleanup
    posix_spawn_file_actions_destroy( &file_actions );
    posix_spawnattr_destroy( &attrp );

    return child_pid;
}