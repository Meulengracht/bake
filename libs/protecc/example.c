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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 * This example demonstrates how to use protecc to compile and match
 * path patterns suitable for eBPF-based security policies.
 */

#include <protecc/protecc.h>
#include <stdio.h>
#include <stdlib.h>

void print_match_result(const protecc_compiled_t* compiled, const char* path) {
    bool match = protecc_match(compiled, path, 0);
    printf("  %s: %s\n", path, match ? "ALLOWED" : "DENIED");
}

int main(void) {
    protecc_compiled_t* compiled = NULL;
    protecc_error_t err;
    
    printf("=== Protecc Library Example ===\n\n");
    
    // Example 1: Simple file access control
    printf("1. Basic file access patterns:\n");
    {
        const char* patterns[] = {
            "/etc/passwd",
            "/etc/group",
            "/tmp/*",
        };
        
        err = protecc_compile(patterns, 3, PROTECC_FLAG_NONE, &compiled);
        if (err != PROTECC_OK) {
            fprintf(stderr, "Compilation failed: %s\n", protecc_error_string(err));
            return 1;
        }
        
        print_match_result(compiled, "/etc/passwd");  // ALLOWED
        print_match_result(compiled, "/etc/shadow");  // DENIED
        print_match_result(compiled, "/tmp/test.txt"); // ALLOWED
        print_match_result(compiled, "/var/log/syslog"); // DENIED
        
        protecc_free(compiled);
        printf("\n");
    }
    
    // Example 2: Wildcard patterns
    printf("2. Wildcard patterns:\n");
    {
        const char* patterns[] = {
            "/home/**",           // Recursive match
            "/var/log/*.log",     // Single directory wildcard
            "/dev/tty?",          // Single character wildcard
        };
        
        err = protecc_compile(patterns, 3, PROTECC_FLAG_NONE, &compiled);
        if (err != PROTECC_OK) {
            fprintf(stderr, "Compilation failed: %s\n", protecc_error_string(err));
            return 1;
        }
        
        print_match_result(compiled, "/home/user/document.txt");  // ALLOWED
        print_match_result(compiled, "/home/user/deep/file.txt"); // ALLOWED
        print_match_result(compiled, "/var/log/system.log");      // ALLOWED
        print_match_result(compiled, "/var/log/sub/app.log");     // DENIED
        print_match_result(compiled, "/dev/tty0");                // ALLOWED
        print_match_result(compiled, "/dev/tty10");               // DENIED
        
        protecc_free(compiled);
        printf("\n");
    }
    
    // Example 3: Character ranges and sets
    printf("3. Character ranges and sets:\n");
    {
        const char* patterns[] = {
            "/dev/tty[0-9]+",         // TTY devices with numbers
            "/tmp/[a-z]*",            // Files starting with lowercase
            "/var/log/app[0-9].log",  // Numbered log files
        };
        
        err = protecc_compile(patterns, 3, PROTECC_FLAG_NONE, &compiled);
        if (err != PROTECC_OK) {
            fprintf(stderr, "Compilation failed: %s\n", protecc_error_string(err));
            return 1;
        }
        
        print_match_result(compiled, "/dev/tty0");        // ALLOWED
        print_match_result(compiled, "/dev/tty123");      // ALLOWED
        print_match_result(compiled, "/dev/ttyS0");       // DENIED
        print_match_result(compiled, "/tmp/myfile");      // ALLOWED
        print_match_result(compiled, "/tmp/MyFile");      // DENIED
        print_match_result(compiled, "/var/log/app5.log"); // ALLOWED
        
        protecc_free(compiled);
        printf("\n");
    }
    
    // Example 4: Case-insensitive matching
    printf("4. Case-insensitive matching:\n");
    {
        const char* patterns[] = {
            "/Windows/*",
            "/Program Files/**",
        };
        
        err = protecc_compile(patterns, 2, PROTECC_FLAG_CASE_INSENSITIVE, &compiled);
        if (err != PROTECC_OK) {
            fprintf(stderr, "Compilation failed: %s\n", protecc_error_string(err));
            return 1;
        }
        
        print_match_result(compiled, "/Windows/system32");        // ALLOWED
        print_match_result(compiled, "/windows/System32");        // ALLOWED
        print_match_result(compiled, "/WINDOWS/notepad.exe");     // ALLOWED
        print_match_result(compiled, "/Program Files/app/bin/tool.exe"); // ALLOWED
        
        protecc_free(compiled);
        printf("\n");
    }
    
    // Example 5: Statistics
    printf("5. Pattern statistics:\n");
    {
        const char* patterns[] = {
            "/etc/*",
            "/var/**",
            "/tmp/[a-z]*",
            "/home/user/*",
        };
        
        err = protecc_compile(patterns, 4, PROTECC_FLAG_OPTIMIZE, &compiled);
        if (err != PROTECC_OK) {
            fprintf(stderr, "Compilation failed: %s\n", protecc_error_string(err));
            return 1;
        }
        
        protecc_stats_t stats;
        protecc_get_stats(compiled, &stats);
        
        printf("  Number of patterns: %zu\n", stats.num_patterns);
        printf("  Binary size: %zu bytes\n", stats.binary_size);
        printf("  Max depth: %zu\n", stats.max_depth);
        printf("  Number of nodes: %zu\n", stats.num_nodes);
        
        protecc_free(compiled);
        printf("\n");
    }
    
    // Example 6: Binary export/import
    printf("6. Binary export (for eBPF):\n");
    {
        const char* patterns[] = {
            "/etc/passwd",
            "/tmp/*",
        };
        
        err = protecc_compile(patterns, 2, PROTECC_FLAG_NONE, &compiled);
        if (err != PROTECC_OK) {
            fprintf(stderr, "Compilation failed: %s\n", protecc_error_string(err));
            return 1;
        }
        
        // Query export size
        size_t export_size;
        err = protecc_export(compiled, NULL, 0, &export_size);
        if (err != PROTECC_OK) {
            fprintf(stderr, "Export size query failed: %s\n", protecc_error_string(err));
            protecc_free(compiled);
            return 1;
        }
        
        printf("  Export size: %zu bytes\n", export_size);
        printf("  This binary format can be loaded into eBPF programs\n");
        printf("  for fast path matching in kernel space.\n");
        
        protecc_free(compiled);
        printf("\n");
    }
    
    printf("=== Example Complete ===\n");
    return 0;
}
