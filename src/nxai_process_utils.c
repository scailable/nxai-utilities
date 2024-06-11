#include <spawn.h>
#include <stdlib.h>
#include "nxai_process_utils.h"

pid_t nxai_start_process( char *const argv[] ) {
    pid_t child_pid;

    char *envp[] = { NULL };

    // Initialize file actions and attributes objects
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attrp;
    posix_spawn_file_actions_init( &file_actions );
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