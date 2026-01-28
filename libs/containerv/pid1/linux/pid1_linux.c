/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#define _GNU_SOURCE
#include "pid1_linux.h"
#include "../shared/logging.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

// Simple linked list for tracking child processes
typedef struct pid1_process_node {
    pid_t pid;
    struct pid1_process_node* next;
} pid1_process_node_t;

static pid1_process_node_t* g_process_list = NULL;
static int                  g_process_count = 0;
static volatile sig_atomic_t g_sigchld_received = 0;
static volatile sig_atomic_t g_shutdown_requested = 0;

// External functions from pid1_common.c
extern int pid1_common_init(void);
extern int pid1_common_cleanup(void);
extern int pid1_validate_spawn(const pid1_process_options_t* options);
extern int pid1_is_initialized(void);

/**
 * @brief Signal handler for SIGCHLD
 */
static void __sigchld_handler(int signo)
{
    (void)signo;
    g_sigchld_received = 1;
}

/**
 * @brief Signal handler for SIGTERM/SIGINT
 */
static void __sigterm_handler(int signo)
{
    (void)signo;
    g_shutdown_requested = 1;
}

/**
 * @brief Add a process to the tracking list
 */
static int __add_process(pid_t pid)
{
    pid1_process_node_t* node = malloc(sizeof(pid1_process_node_t));
    if (node == NULL) {
        return -1;
    }

    node->pid = pid;
    node->next = g_process_list;
    g_process_list = node;
    g_process_count++;

    PID1_DEBUG("Added process %d to tracking list (total: %d)", pid, g_process_count);
    return 0;
}

/**
 * @brief Remove a process from the tracking list
 */
static void __remove_process(pid_t pid)
{
    pid1_process_node_t** current = &g_process_list;

    while (*current != NULL) {
        if ((*current)->pid == pid) {
            pid1_process_node_t* to_free = *current;
            *current = (*current)->next;
            free(to_free);
            g_process_count--;
            PID1_DEBUG("Removed process %d from tracking list (total: %d)", pid, g_process_count);
            return;
        }
        current = &(*current)->next;
    }
}

int pid1_linux_init(void)
{
    struct sigaction sa_chld;
    struct sigaction sa_term;

    // Initialize common components
    if (pid1_common_init() != 0) {
        return -1;
    }

    // Set up SIGCHLD handler
    memset(&sa_chld, 0, sizeof(sa_chld));
    sa_chld.sa_handler = __sigchld_handler;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa_chld, NULL) != 0) {
        PID1_ERROR("Failed to set up SIGCHLD handler: %s", strerror(errno));
        return -1;
    }

    // Set up SIGTERM/SIGINT handlers
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = __sigterm_handler;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = SA_RESTART;

    if (sigaction(SIGTERM, &sa_term, NULL) != 0) {
        PID1_ERROR("Failed to set up SIGTERM handler: %s", strerror(errno));
        return -1;
    }

    if (sigaction(SIGINT, &sa_term, NULL) != 0) {
        PID1_ERROR("Failed to set up SIGINT handler: %s", strerror(errno));
        return -1;
    }

    PID1_INFO("Linux PID 1 service initialized");
    return 0;
}

int pid1_linux_spawn(const pid1_process_options_t* options, pid_t* pid_out)
{
    pid_t pid;

    // Validate options using common validation
    if (pid1_validate_spawn(options) != 0) {
        return -1;
    }

    // Fork the process
    pid = fork();
    if (pid < 0) {
        PID1_ERROR("fork() failed: %s", strerror(errno));
        return -1;
    }

    if (pid == 0) {
        // Child process
        
        // Change working directory if specified
        if (options->working_directory != NULL) {
            if (chdir(options->working_directory) != 0) {
                PID1_ERROR("chdir() failed: %s", strerror(errno));
                _exit(1);
            }
        }

        // Set user/group if specified (and not already set)
        if (options->uid != 0 || options->gid != 0) {
            if (options->gid != 0 && setgid(options->gid) != 0) {
                PID1_ERROR("setgid(%u) failed: %s", options->gid, strerror(errno));
                _exit(1);
            }
            if (options->uid != 0 && setuid(options->uid) != 0) {
                PID1_ERROR("setuid(%u) failed: %s", options->uid, strerror(errno));
                _exit(1);
            }
        }

        // Execute the command
        if (options->environment != NULL) {
            execve(options->command, (char* const*)options->args, (char* const*)options->environment);
        } else {
            execv(options->command, (char* const*)options->args);
        }

        // If execve returns, it failed
        PID1_ERROR("execve() failed: %s", strerror(errno));
        _exit(127);
    }

    // Parent process
    PID1_INFO("Spawned process %d: %s", pid, options->command);

    // Add to tracking list
    if (__add_process(pid) != 0) {
        PID1_ERROR("Failed to add process %d to tracking list", pid);
        kill(pid, SIGKILL);
        return -1;
    }

    if (pid_out != NULL) {
        *pid_out = pid;
    }

    return 0;
}

int pid1_linux_wait(pid_t pid, int* exit_code_out)
{
    int status;
    pid_t result;

    if (!pid1_is_initialized()) {
        errno = EINVAL;
        return -1;
    }

    PID1_DEBUG("Waiting for process %d", pid);

    result = waitpid(pid, &status, 0);
    if (result < 0) {
        PID1_ERROR("waitpid(%d) failed: %s", pid, strerror(errno));
        return -1;
    }

    // Remove from tracking list
    __remove_process(pid);

    // Extract exit code
    if (exit_code_out != NULL) {
        if (WIFEXITED(status)) {
            *exit_code_out = WEXITSTATUS(status);
            PID1_INFO("Process %d exited with code %d", pid, *exit_code_out);
        } else if (WIFSIGNALED(status)) {
            *exit_code_out = 128 + WTERMSIG(status);
            PID1_INFO("Process %d terminated by signal %d", pid, WTERMSIG(status));
        } else {
            *exit_code_out = -1;
        }
    }

    return 0;
}

int pid1_linux_kill(pid_t pid)
{
    if (!pid1_is_initialized()) {
        errno = EINVAL;
        return -1;
    }

    PID1_INFO("Killing process %d", pid);

    if (kill(pid, SIGTERM) != 0) {
        PID1_ERROR("kill(%d, SIGTERM) failed: %s", pid, strerror(errno));
        return -1;
    }

    return 0;
}

int pid1_linux_reap_zombies(void)
{
    int status;
    pid_t pid;
    int reaped = 0;

    // Reap all available zombie processes
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        __remove_process(pid);
        reaped++;

        if (WIFEXITED(status)) {
            PID1_DEBUG("Reaped process %d (exit code: %d)", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            PID1_DEBUG("Reaped process %d (signal: %d)", pid, WTERMSIG(status));
        }
    }

    if (pid < 0 && errno != ECHILD) {
        PID1_ERROR("waitpid() failed during reaping: %s", strerror(errno));
        return -1;
    }

    if (reaped > 0) {
        PID1_DEBUG("Reaped %d zombie processes", reaped);
    }

    g_sigchld_received = 0;
    return reaped;
}

int pid1_linux_cleanup(void)
{
    pid1_process_node_t* current;
    int remaining;

    if (!pid1_is_initialized()) {
        return 0;
    }

    PID1_INFO("Cleaning up Linux PID 1 service");

    // Send SIGTERM to all remaining processes
    current = g_process_list;
    remaining = 0;
    while (current != NULL) {
        PID1_INFO("Terminating process %d", current->pid);
        kill(current->pid, SIGTERM);
        remaining++;
        current = current->next;
    }

    // Give processes time to terminate gracefully
    if (remaining > 0) {
        PID1_INFO("Waiting for %d processes to terminate", remaining);
        sleep(2);

        // Reap any that have exited
        pid1_linux_reap_zombies();

        // Force kill any that remain
        current = g_process_list;
        while (current != NULL) {
            PID1_INFO("Force killing process %d", current->pid);
            kill(current->pid, SIGKILL);
            current = current->next;
        }

        // Final reap
        sleep(1);
        pid1_linux_reap_zombies();
    }

    // Free the process list
    while (g_process_list != NULL) {
        pid1_process_node_t* next = g_process_list->next;
        free(g_process_list);
        g_process_list = next;
    }
    g_process_count = 0;

    pid1_common_cleanup();
    return 0;
}

int pid1_linux_get_process_count(void)
{
    return g_process_count;
}

// Implement the common interface functions by delegating to Linux-specific implementations

int pid1_init(void)
{
    return pid1_linux_init();
}

int pid1_spawn_process(const pid1_process_options_t* options, pid1_process_handle_t* handle_out)
{
    return pid1_linux_spawn(options, handle_out);
}

int pid1_wait_process(pid1_process_handle_t handle, int* exit_code_out)
{
    return pid1_linux_wait(handle, exit_code_out);
}

int pid1_kill_process(pid1_process_handle_t handle)
{
    return pid1_linux_kill(handle);
}

int pid1_cleanup(void)
{
    return pid1_linux_cleanup();
}

int pid1_reap_zombies(void)
{
    return pid1_linux_reap_zombies();
}

int pid1_get_process_count(void)
{
    return pid1_linux_get_process_count();
}
