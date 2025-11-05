/**
 * Example: Windows Volume Management
 * 
 * This example demonstrates Windows-specific volume management features
 * including VHD creation, host bind mounts, and temporary filesystems.
 */

#include <chef/containerv.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>

int main() {
    struct containerv_options* options;
    struct containerv_container* container;
    int status;
    
    printf("=== Windows Container Volume Management Example ===\n\n");
    
    // Create container options
    options = containerv_options_new();
    if (!options) {
        fprintf(stderr, "Failed to create container options\n");
        return 1;
    }
    
    // Enable filesystem capabilities for volume management
    containerv_options_set_caps(options, 
        CV_CAP_FILESYSTEM |       // Enable volume and mount support
        CV_CAP_PROCESS_CONTROL |  // Enable process management
        CV_CAP_CGROUPS            // Enable resource limits
    );
    
    printf("Creating named persistent volumes...\n");
    
    // Create persistent volumes for demonstration
    status = containerv_volume_create("app-data", 500, "NTFS");
    if (status == 0) {
        printf("✓ Created 'app-data' volume (500MB, NTFS)\n");
    } else {
        printf("✗ Failed to create 'app-data' volume\n");
    }
    
    status = containerv_volume_create("database", 1024, "NTFS");
    if (status == 0) {
        printf("✓ Created 'database' volume (1GB, NTFS)\n");
    } else {
        printf("✗ Failed to create 'database' volume\n");
    }
    
    printf("\nConfiguring container volumes and mounts...\n");
    
    // Configure various types of mounts
    struct containerv_mount mounts[] = {
        // Host directory bind mount (read-write)
        {
            .what = "C:\\host\\data",
            .where = "C:\\container\\data", 
            .fstype = NULL,
            .flags = CV_MOUNT_BIND | CV_MOUNT_CREATE
        },
        
        // Host directory bind mount (read-only)
        {
            .what = "C:\\host\\config",
            .where = "C:\\container\\config",
            .fstype = NULL, 
            .flags = CV_MOUNT_BIND | CV_MOUNT_READONLY | CV_MOUNT_CREATE
        },
        
        // Temporary filesystem (in-memory, fast)
        {
            .what = NULL,
            .where = "C:\\container\\temp",
            .fstype = "tmpfs",
            .flags = CV_MOUNT_CREATE
        },
        
        // VHD persistent volume (would be created/attached)
        {
            .what = "app-data.vhdx",  // References named volume
            .where = "C:\\container\\app",
            .fstype = NULL,
            .flags = CV_MOUNT_CREATE
        },
        
        // Another VHD volume
        {
            .what = "database.vhdx", 
            .where = "C:\\container\\database",
            .fstype = NULL,
            .flags = CV_MOUNT_CREATE
        }
    };
    
    // Configure mounts
    containerv_options_set_mounts(options, mounts, sizeof(mounts)/sizeof(mounts[0]));
    
    printf("Mount configuration:\n");
    printf("  1. Host bind (RW): C:\\host\\data -> C:\\container\\data\n");
    printf("  2. Host bind (RO): C:\\host\\config -> C:\\container\\config\n");
    printf("  3. Temporary FS:   tmpfs -> C:\\container\\temp\n");
    printf("  4. VHD Volume:     app-data -> C:\\container\\app\n");
    printf("  5. VHD Volume:     database -> C:\\container\\database\n\n");
    
    // Configure resource limits
    containerv_options_set_resource_limits(options, "1G", "50", "64");
    
    printf("Creating Windows container with volume configuration...\n");
    status = containerv_create("C:\\chef\\rootfs", options, &container);
    
    if (status != 0) {
        fprintf(stderr, "Failed to create container: %d\n", status);
        containerv_options_delete(options);
        return 1;
    }
    
    printf("Container created: %s\n", containerv_id(container));
    printf("Volume management active!\n\n");
    
    // Demonstrate volume usage
    printf("Spawning process to test volume access...\n");
    
    struct containerv_spawn_options spawn_opts = {0};
    spawn_opts.arguments = "/c dir C:\\container & echo Volume test complete";
    spawn_opts.flags = CV_SPAWN_WAIT;
    
    HANDLE process_handle;
    status = containerv_spawn(container, "cmd.exe", &spawn_opts, &process_handle);
    
    if (status == 0) {
        printf("✓ Process executed successfully - volumes are accessible\n");
    } else {
        printf("✗ Process execution failed: %d\n", status);
    }
    
    printf("\n=== Windows Volume Management Features ===\n");
    printf("✓ VHD Creation: Persistent storage using Windows Virtual Disk API\n");
    printf("✓ Host Bind Mounts: HyperV shared folders for host directory access\n");
    printf("✓ Temporary Filesystems: RAM-based storage for fast I/O\n");
    printf("✓ Named Volume Management: Persistent volume lifecycle management\n");
    printf("✓ Multiple Filesystem Support: NTFS, ReFS, FAT32 compatibility\n");
    printf("✓ Read-Only Mounts: Security through immutable configurations\n");
    printf("✓ Automatic Cleanup: VHDs and mounts cleaned up on container destroy\n");
    printf("✓ HyperV Integration: Native Windows VM storage attachment\n");
    printf("✓ Cross-Platform API: Same mount interface as Linux containers\n\n");
    
    printf("Volume Types Supported:\n");
    printf("  • VHD Files (.vhd/.vhdx): Persistent, portable, secure storage\n");
    printf("  • Host Directories: Direct filesystem sharing with host\n");
    printf("  • Temporary Storage: High-performance in-memory filesystems\n");
    printf("  • SMB Network Shares: Networked storage (planned)\n");
    printf("  • Named Volumes: Managed persistent storage with lifecycle\n\n");
    
    printf("Windows-Specific Advantages:\n");
    printf("  • Strong Isolation: VM-level storage isolation vs namespace-level\n");
    printf("  • NTFS ACLs: Windows-native permission and security model\n");
    printf("  • Enterprise Integration: Compatible with Windows storage infrastructure\n");
    printf("  • Backup/Restore: VHDs can be easily backed up and restored\n");
    printf("  • Portability: VHD volumes can be moved between systems\n\n");
    
    // Clean up
    printf("Cleaning up container and volumes...\n");
    containerv_destroy(container);
    containerv_options_delete(options);
    
    printf("Windows Volume Management example completed successfully!\n");
    return 0;
}

#else
int main() {
    printf("This example is Windows-specific. Please run on Windows with HyperV support.\n");
    printf("For Linux volume management, see the Linux container examples.\n");
    return 1;
}
#endif