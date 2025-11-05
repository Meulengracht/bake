/**
 * Copyright 2024, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include <chef/containerv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <threads.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <curl/curl.h>
#endif

// Health monitoring state
struct health_monitor_entry {
    char service_name[256];
    char instance_id[64];
    struct containerv_healthcheck config;
    enum containerv_health_status status;
    int consecutive_failures;
    time_t last_check;
    time_t started_at;
    bool enabled;
};

struct health_monitoring_state {
    bool initialized;
    bool monitoring_active;
    mtx_t lock;
    thrd_t monitor_thread;
    
    struct health_monitor_entry* monitors;
    int monitor_count;
    int monitor_capacity;
    
    containerv_orchestration_callback event_callback;
    void* callback_user_data;
} g_health_monitor = {0};

// Forward declarations
static int health_monitoring_thread(void* arg);
static int execute_health_check(struct health_monitor_entry* monitor);
static int execute_command_health_check(char** command, int timeout_seconds);
static int execute_http_health_check(const char* url, int timeout_seconds);
static int parse_health_check_command(char** command, char** url_out);
static int find_monitor_entry(const char* service_name, const char* instance_id);
static int create_monitor_entry(const char* service_name, const char* instance_id);

/**
 * @brief Start health monitoring for an application
 */
int containerv_start_health_monitoring(struct containerv_application* app,
                                      containerv_orchestration_callback callback,
                                      void* user_data) {
    if (!app || g_health_monitor.monitoring_active) {
        return -1;
    }
    
    if (!g_health_monitor.initialized) {
        if (mtx_init(&g_health_monitor.lock, mtx_plain) != thrd_success) {
            return -1;
        }
        
        g_health_monitor.monitor_capacity = 128;
        g_health_monitor.monitors = calloc(g_health_monitor.monitor_capacity,
                                          sizeof(struct health_monitor_entry));
        if (!g_health_monitor.monitors) {
            mtx_destroy(&g_health_monitor.lock);
            return -1;
        }
        
        g_health_monitor.initialized = true;
    }
    
    mtx_lock(&g_health_monitor.lock);
    
    g_health_monitor.event_callback = callback;
    g_health_monitor.callback_user_data = user_data;
    
    // Register health checks for all services with health config
    for (int i = 0; i < app->service_count; i++) {
        struct containerv_service* service = &app->services[i];
        
        if (!service->healthcheck) {
            continue; // No health check configured
        }
        
        // Register health monitors for all instances
        for (int j = 0; j < app->instance_counts[i]; j++) {
            struct containerv_service_instance* instance = &app->instances[i][j];
            
            int monitor_index = create_monitor_entry(service->name, instance->id);
            if (monitor_index == -1) {
                continue; // Skip on allocation failure
            }
            
            struct health_monitor_entry* monitor = &g_health_monitor.monitors[monitor_index];
            
            // Copy health check configuration
            monitor->config = *service->healthcheck;
            
            // Duplicate command array
            if (service->healthcheck->test_command) {
                int cmd_count = 0;
                while (service->healthcheck->test_command[cmd_count]) cmd_count++;
                
                monitor->config.test_command = malloc((cmd_count + 1) * sizeof(char*));
                for (int k = 0; k < cmd_count; k++) {
                    monitor->config.test_command[k] = strdup(service->healthcheck->test_command[k]);
                }
                monitor->config.test_command[cmd_count] = NULL;
            }
            
            monitor->status = CV_HEALTH_STARTING;
            monitor->consecutive_failures = 0;
            monitor->last_check = 0;
            monitor->started_at = time(NULL);
            monitor->enabled = true;
        }
    }
    
    // Start monitoring thread
    g_health_monitor.monitoring_active = true;
    if (thrd_create(&g_health_monitor.monitor_thread, 
                      health_monitoring_thread, NULL) != thrd_success) {
        g_health_monitor.monitoring_active = false;
        mtx_unlock(&g_health_monitor.lock);
        return -1;
    }
    
    mtx_unlock(&g_health_monitor.lock);
    return 0;
}

/**
 * @brief Stop health monitoring for an application
 */
void containerv_stop_health_monitoring(struct containerv_application* app) {
    if (!g_health_monitor.monitoring_active) {
        return;
    }
    
    mtx_lock(&g_health_monitor.lock);
    
    g_health_monitor.monitoring_active = false;
    
    mtx_unlock(&g_health_monitor.lock);
    thrd_join(g_health_monitor.monitor_thread, NULL);
    
    mtx_lock(&g_health_monitor.lock);
    
    // Disable monitoring for this application's services
    if (app) {
        for (int i = 0; i < app->service_count; i++) {
            for (int j = 0; j < app->instance_counts[i]; j++) {
                struct containerv_service_instance* instance = &app->instances[i][j];
                int monitor_index = find_monitor_entry(app->services[i].name, instance->id);
                
                if (monitor_index != -1) {
                    g_health_monitor.monitors[monitor_index].enabled = false;
                }
            }
        }
    }
    
    mtx_unlock(&g_health_monitor.lock);
}

/**
 * @brief Health monitoring thread
 */
static int health_monitoring_thread(void* arg) {
    (void)arg;
    
    while (g_health_monitor.monitoring_active) {
        time_t now = time(NULL);
        
        mtx_lock(&g_health_monitor.lock);
        
        for (int i = 0; i < g_health_monitor.monitor_count; i++) {
            struct health_monitor_entry* monitor = &g_health_monitor.monitors[i];
            
            if (!monitor->enabled) {
                continue;
            }
            
            // Check if it's time for a health check
            time_t next_check_time = monitor->last_check + monitor->config.interval_seconds;
            
            // For starting status, wait for start_period before first check
            if (monitor->status == CV_HEALTH_STARTING) {
                time_t start_period_end = monitor->started_at + monitor->config.start_period_seconds;
                if (now < start_period_end) {
                    continue; // Still in start period
                }
                next_check_time = monitor->last_check + monitor->config.interval_seconds;
            }
            
            if (now >= next_check_time) {
                monitor->last_check = now;
                
                // Execute health check
                int check_result = execute_health_check(monitor);
                enum containerv_health_status old_status = monitor->status;
                
                if (check_result == 0) {
                    // Health check passed
                    monitor->consecutive_failures = 0;
                    
                    if (monitor->status != CV_HEALTH_HEALTHY) {
                        monitor->status = CV_HEALTH_HEALTHY;
                        
                        // Update service discovery
                        extern int containerv_update_endpoint_health(const char*, const char*, bool);
                        containerv_update_endpoint_health(monitor->service_name, 
                                                        monitor->instance_id, true);
                        
                        // Fire health event
                        if (g_health_monitor.event_callback && old_status != CV_HEALTH_HEALTHY) {
                            g_health_monitor.event_callback(CV_ORCH_SERVICE_HEALTHY,
                                                          monitor->service_name,
                                                          "Service became healthy",
                                                          g_health_monitor.callback_user_data);
                        }
                    }
                } else {
                    // Health check failed
                    monitor->consecutive_failures++;
                    
                    if (monitor->consecutive_failures >= monitor->config.retries) {
                        if (monitor->status != CV_HEALTH_UNHEALTHY) {
                            monitor->status = CV_HEALTH_UNHEALTHY;
                            
                            // Update service discovery
                            extern int containerv_update_endpoint_health(const char*, const char*, bool);
                            containerv_update_endpoint_health(monitor->service_name,
                                                            monitor->instance_id, false);
                            
                            // Fire unhealthy event
                            if (g_health_monitor.event_callback) {
                                char message[256];
                                snprintf(message, sizeof(message), 
                                        "Service failed %d consecutive health checks", 
                                        monitor->consecutive_failures);
                                g_health_monitor.event_callback(CV_ORCH_SERVICE_UNHEALTHY,
                                                              monitor->service_name, message,
                                                              g_health_monitor.callback_user_data);
                            }
                        }
                    }
                }
            }
        }
        
        mtx_unlock(&g_health_monitor.lock);
        
        // Sleep for 1 second before next iteration
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
    }
    
    return 0;
}

/**
 * @brief Execute a health check
 */
static int execute_health_check(struct health_monitor_entry* monitor) {
    if (!monitor->config.test_command || !monitor->config.test_command[0]) {
        return -1; // No command configured
    }
    
    // Check if this is an HTTP health check
    char* url = NULL;
    if (parse_health_check_command(monitor->config.test_command, &url) == 0) {
        int result = execute_http_health_check(url, monitor->config.timeout_seconds);
        free(url);
        return result;
    }
    
    // Execute as shell command
    return execute_command_health_check(monitor->config.test_command, 
                                       monitor->config.timeout_seconds);
}

/**
 * @brief Parse health check command to detect HTTP checks
 */
static int parse_health_check_command(char** command, char** url_out) {
    if (!command || !command[0] || !url_out) {
        return -1;
    }
    
    // Check for common HTTP health check patterns
    if (strcmp(command[0], "CMD") == 0 && command[1]) {
        // Docker-style health check
        if (strstr(command[1], "curl") && command[2]) {
            // Look for URL in curl command
            for (int i = 2; command[i]; i++) {
                if (strstr(command[i], "http://") || strstr(command[i], "https://")) {
                    *url_out = strdup(command[i]);
                    return 0;
                }
            }
        } else if (strstr(command[1], "wget") && command[2]) {
            // Look for URL in wget command
            for (int i = 2; command[i]; i++) {
                if (strstr(command[i], "http://") || strstr(command[i], "https://")) {
                    *url_out = strdup(command[i]);
                    return 0;
                }
            }
        }
    }
    
    return -1; // Not an HTTP health check
}

/**
 * @brief Execute HTTP health check
 */
static int execute_http_health_check(const char* url, int timeout_seconds) {
    if (!url) {
        return -1;
    }
    
#ifdef _WIN32
    // Windows HTTP client implementation
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    DWORD dwSize = 0, dwDownloaded = 0, dwStatusCode = 0, dwStatusSize = sizeof(dwStatusCode);
    BOOL bResults = FALSE;
    
    hSession = WinHttpOpen(L"Chef Health Monitor/1.0",
                          WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (hSession) {
        // Set timeout
        WinHttpSetTimeouts(hSession, timeout_seconds * 1000, timeout_seconds * 1000,
                          timeout_seconds * 1000, timeout_seconds * 1000);
        
        // Parse URL (simplified - would need full URL parsing)
        wchar_t wurl[1024];
        MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, sizeof(wurl) / sizeof(wchar_t));
        
        URL_COMPONENTS urlComp = {0};
        urlComp.dwStructSize = sizeof(urlComp);
        urlComp.dwHostNameLength = -1;
        urlComp.dwUrlPathLength = -1;
        
        if (WinHttpCrackUrl(wurl, 0, 0, &urlComp)) {
            hConnect = WinHttpConnect(hSession, urlComp.lpszHostName, urlComp.nPort, 0);
            
            if (hConnect) {
                hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
                
                if (hRequest) {
                    bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS,
                                                0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                    
                    if (bResults) {
                        bResults = WinHttpReceiveResponse(hRequest, NULL);
                        
                        if (bResults) {
                            bResults = WinHttpQueryHeaders(hRequest,
                                                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                                         NULL, &dwStatusCode, &dwStatusSize, NULL);
                        }
                    }
                }
            }
        }
    }
    
    // Cleanup
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    
    // Check if status code indicates success (200-299)
    return (bResults && dwStatusCode >= 200 && dwStatusCode < 300) ? 0 : -1;
    
#else
    // Linux HTTP client implementation using libcurl
    CURL* curl;
    CURLcode res;
    long response_code = 0;
    
    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL); // Discard response body
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); // HEAD request
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    res = curl_easy_perform(curl);
    
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    }
    
    curl_easy_cleanup(curl);
    
    // Check if status code indicates success (200-299)
    return (res == CURLE_OK && response_code >= 200 && response_code < 300) ? 0 : -1;
#endif
}

/**
 * @brief Execute command-based health check
 */
static int execute_command_health_check(char** command, int timeout_seconds) {
    if (!command || !command[0]) {
        return -1;
    }
    
#ifdef _WIN32
    // Windows process execution
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    
    // Build command line
    char cmdline[1024] = {0};
    for (int i = 0; command[i] && strlen(cmdline) < sizeof(cmdline) - 100; i++) {
        if (i > 0) strcat(cmdline, " ");
        strcat(cmdline, command[i]);
    }
    
    if (CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_seconds * 1000);
        
        DWORD exit_code = 1;
        if (wait_result == WAIT_OBJECT_0) {
            GetExitCodeProcess(pi.hProcess, &exit_code);
        } else {
            TerminateProcess(pi.hProcess, 1);
        }
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        return exit_code == 0 ? 0 : -1;
    }
    
    return -1;
    
#else
    // Linux process execution
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        execvp(command[0], command);
        _exit(1); // execvp failed
    } else if (pid > 0) {
        // Parent process
        int status;
        
        // Set up alarm for timeout
        alarm(timeout_seconds);
        
        pid_t result = waitpid(pid, &status, 0);
        
        alarm(0); // Cancel alarm
        
        if (result == pid && WIFEXITED(status)) {
            return WEXITSTATUS(status) == 0 ? 0 : -1;
        } else {
            // Timeout or other error - kill child
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            return -1;
        }
    }
    
    return -1;
#endif
}

/**
 * @brief Find monitor entry by service and instance
 */
static int find_monitor_entry(const char* service_name, const char* instance_id) {
    for (int i = 0; i < g_health_monitor.monitor_count; i++) {
        if (strcmp(g_health_monitor.monitors[i].service_name, service_name) == 0 &&
            strcmp(g_health_monitor.monitors[i].instance_id, instance_id) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Create new monitor entry
 */
static int create_monitor_entry(const char* service_name, const char* instance_id) {
    if (g_health_monitor.monitor_count >= g_health_monitor.monitor_capacity) {
        g_health_monitor.monitor_capacity *= 2;
        g_health_monitor.monitors = realloc(g_health_monitor.monitors,
            g_health_monitor.monitor_capacity * sizeof(struct health_monitor_entry));
        if (!g_health_monitor.monitors) {
            return -1;
        }
    }
    
    struct health_monitor_entry* monitor = &g_health_monitor.monitors[g_health_monitor.monitor_count];
    memset(monitor, 0, sizeof(*monitor));
    
    strncpy(monitor->service_name, service_name, sizeof(monitor->service_name) - 1);
    strncpy(monitor->instance_id, instance_id, sizeof(monitor->instance_id) - 1);
    
    return g_health_monitor.monitor_count++;
}

/**
 * @brief Get health status for a specific service
 */
int containerv_get_service_health(const char* service_name,
                                 enum containerv_health_status* health_status) {
    if (!service_name || !health_status || !g_health_monitor.initialized) {
        return -1;
    }
    
    mtx_lock(&g_health_monitor.lock);
    
    // Find any monitor for this service
    bool found = false;
    enum containerv_health_status overall_status = CV_HEALTH_HEALTHY;
    
    for (int i = 0; i < g_health_monitor.monitor_count; i++) {
        if (strcmp(g_health_monitor.monitors[i].service_name, service_name) == 0 &&
            g_health_monitor.monitors[i].enabled) {
            found = true;
            
            // Overall service is unhealthy if any instance is unhealthy
            if (g_health_monitor.monitors[i].status == CV_HEALTH_UNHEALTHY) {
                overall_status = CV_HEALTH_UNHEALTHY;
                break;
            } else if (g_health_monitor.monitors[i].status == CV_HEALTH_STARTING &&
                      overall_status == CV_HEALTH_HEALTHY) {
                overall_status = CV_HEALTH_STARTING;
            }
        }
    }
    
    mtx_unlock(&g_health_monitor.lock);
    
    if (found) {
        *health_status = overall_status;
        return 0;
    } else {
        *health_status = CV_HEALTH_NONE;
        return -1;
    }
}

/**
 * @brief Manually trigger health check for service
 */
int containerv_trigger_health_check(const char* service_name, const char* instance_id) {
    if (!service_name || !g_health_monitor.initialized) {
        return -1;
    }
    
    mtx_lock(&g_health_monitor.lock);
    
    int checks_triggered = 0;
    
    for (int i = 0; i < g_health_monitor.monitor_count; i++) {
        struct health_monitor_entry* monitor = &g_health_monitor.monitors[i];
        
        if (strcmp(monitor->service_name, service_name) == 0 &&
            (instance_id == NULL || strcmp(monitor->instance_id, instance_id) == 0) &&
            monitor->enabled) {
            
            // Force immediate health check by resetting last_check time
            monitor->last_check = 0;
            checks_triggered++;
        }
    }
    
    mtx_unlock(&g_health_monitor.lock);
    return checks_triggered > 0 ? 0 : -1;
}