/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * @file pid1_example.c
 * @brief Example program demonstrating the PID 1 service API
 * 
 * This example shows how to:
 * 1. Initialize the PID 1 service
 * 2. Spawn processes with various configurations
 * 3. Wait for processes to complete
 * 4. Handle process termination
 * 5. Clean up resources
 */

#include "../shared/pid1_common.h"
#include "../shared/logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

/**
 * @brief Example 1: Simple process spawn and wait
 */
static int example_simple_spawn(void)
{
    pid1_process_options_t opts = {0};
    pid1_process_handle_t handle;
    int exit_code;

    printf("\n=== Example 1: Simple Process Spawn ===\n");

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    // Windows: run cmd.exe with a simple command
    const char* args[] = {"cmd.exe", "/C", "echo Hello from PID 1 service!", NULL};
    opts.command = "C:\\Windows\\System32\\cmd.exe";
#else
    // Linux: run bash with a simple command
    const char* args[] = {"bash", "-c", "echo Hello from PID 1 service!", NULL};
    opts.command = "/bin/bash";
#endif

    opts.args = args;
    opts.environment = NULL;
    opts.working_directory = NULL;
    opts.log_path = NULL;
    opts.memory_limit_bytes = 0;
    opts.cpu_percent = 0;
    opts.process_limit = 0;
    opts.uid = 0;
    opts.gid = 0;
    opts.wait_for_exit = 0;
    opts.forward_signals = 1;

    // Spawn the process
    if (pid1_spawn_process(&opts, &handle) != 0) {
        fprintf(stderr, "Failed to spawn process\n");
        return -1;
    }

    printf("Process spawned successfully, waiting for completion...\n");

    // Wait for the process to complete
    if (pid1_wait_process(handle, &exit_code) != 0) {
        fprintf(stderr, "Failed to wait for process\n");
        return -1;
    }

    printf("Process exited with code: %d\n", exit_code);
    return 0;
}

/**
 * @brief Example 2: Spawn multiple processes
 */
static int example_multiple_processes(void)
{
    printf("\n=== Example 2: Multiple Processes ===\n");

    const int num_processes = 3;
    pid1_process_handle_t handles[3];

    // Spawn multiple processes
    for (int i = 0; i < num_processes; i++) {
        pid1_process_options_t opts = {0};

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
        char cmd_buf[256];
        snprintf(cmd_buf, sizeof(cmd_buf), "echo Process %d", i + 1);
        const char* args[] = {"cmd.exe", "/C", cmd_buf, NULL};
        opts.command = "C:\\Windows\\System32\\cmd.exe";
#else
        char cmd_buf[256];
        snprintf(cmd_buf, sizeof(cmd_buf), "echo Process %d && sleep 1", i + 1);
        const char* args[] = {"bash", "-c", cmd_buf, NULL};
        opts.command = "/bin/bash";
#endif

        opts.args = args;
        opts.environment = NULL;

        if (pid1_spawn_process(&opts, &handles[i]) != 0) {
            fprintf(stderr, "Failed to spawn process %d\n", i + 1);
            return -1;
        }

        printf("Spawned process %d\n", i + 1);
    }

    printf("Active processes: %d\n", pid1_get_process_count());

    // Wait for all processes
    for (int i = 0; i < num_processes; i++) {
        int exit_code;
        if (pid1_wait_process(handles[i], &exit_code) == 0) {
            printf("Process %d exited with code %d\n", i + 1, exit_code);
        }
    }

    printf("All processes completed\n");
    return 0;
}

/**
 * @brief Example 3: Process termination
 */
static int example_process_termination(void)
{
    pid1_process_options_t opts = {0};
    pid1_process_handle_t handle;

    printf("\n=== Example 3: Process Termination ===\n");

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    // Windows: run a long-running command
    const char* args[] = {"cmd.exe", "/C", "ping -n 100 127.0.0.1", NULL};
    opts.command = "C:\\Windows\\System32\\cmd.exe";
#else
    // Linux: run a long-running command
    const char* args[] = {"bash", "-c", "sleep 100", NULL};
    opts.command = "/bin/bash";
#endif

    opts.args = args;
    opts.environment = NULL;

    // Spawn the process
    if (pid1_spawn_process(&opts, &handle) != 0) {
        fprintf(stderr, "Failed to spawn process\n");
        return -1;
    }

    printf("Long-running process spawned, waiting 2 seconds...\n");
    SLEEP_MS(2000);

    // Terminate the process
    printf("Terminating process...\n");
    if (pid1_kill_process(handle) != 0) {
        fprintf(stderr, "Failed to kill process\n");
        return -1;
    }

    // Wait for it to exit
    int exit_code;
    if (pid1_wait_process(handle, &exit_code) == 0) {
        printf("Process terminated with exit code: %d\n", exit_code);
    }

    return 0;
}

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[])
{
    int status = 0;

    printf("PID 1 Service Example Program\n");
    printf("==============================\n");

    // Initialize logging
    if (pid1_log_init(NULL, PID1_LOG_DEBUG) != 0) {
        fprintf(stderr, "Failed to initialize logging\n");
        return 1;
    }

    // Initialize PID 1 service
    printf("Initializing PID 1 service...\n");
    if (pid1_init() != 0) {
        fprintf(stderr, "Failed to initialize PID 1 service\n");
        pid1_log_close();
        return 1;
    }

    printf("PID 1 service initialized successfully\n");

    // Run examples
    if (example_simple_spawn() != 0) {
        status = 1;
        goto cleanup;
    }

    if (example_multiple_processes() != 0) {
        status = 1;
        goto cleanup;
    }

    if (example_process_termination() != 0) {
        status = 1;
        goto cleanup;
    }

    printf("\n=== All Examples Completed Successfully ===\n");

cleanup:
    // Clean up
    printf("\nCleaning up...\n");
    pid1_cleanup();
    pid1_log_close();

    printf("Done!\n");
    return status;
}
