# Container Image System Analysis for Chef Containerv

## Executive Summary

The Container Image System will provide OCI-compatible image management with layered filesystems, caching, and multi-platform support for Chef's containerv library. This system needs to handle both Linux and Windows container images with their respective filesystem technologies.

## Requirements Analysis

### Core Functionality Required
1. **OCI Image Specification Compliance**
   - Image manifest parsing and creation
   - Layer management and extraction
   - Image configuration handling
   - Multi-architecture image support

2. **Layer Management**
   - Linux: OverlayFS, AUFS, Device Mapper
   - Windows: VHD differencing disks, WCIFS (Windows Container Image File System)
   - Layer caching and deduplication
   - Copy-on-write semantics

3. **Image Storage**
   - Local image cache management
   - Registry integration (pull/push)
   - Image verification and signing
   - Garbage collection

4. **Platform Integration**
   - Linux: Integration with existing namespace-based containers
   - Windows: Integration with HyperV VMs and VHD storage
   - Cross-platform image format translation

## OCI Image Format Overview

```json
{
  "schemaVersion": 2,
  "mediaType": "application/vnd.oci.image.manifest.v1+json",
  "config": {
    "mediaType": "application/vnd.oci.image.config.v1+json",
    "size": 7023,
    "digest": "sha256:b5b2b2c..."
  },
  "layers": [
    {
      "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip",
      "size": 32654,
      "digest": "sha256:e692418e4..."
    },
    {
      "mediaType": "application/vnd.oci.image.layer.v1.tar+gzip", 
      "size": 16724,
      "digest": "sha256:3c3a4604a..."
    }
  ]
}
```

## Architecture Design

### Image Storage Structure
```
/var/lib/chef/images/
├── blobs/
│   └── sha256/
│       ├── abc123... (layer data)
│       ├── def456... (config)
│       └── ghi789... (manifest)
├── repositories/
│   └── registry.example.com/
│       └── myapp/
│           └── latest -> ../../blobs/sha256/manifest_hash
├── cache/
│   ├── layers/
│   │   ├── extracted/
│   │   │   └── sha256_abc123/
│   │   └── mounted/
│   │       └── overlay_xyz/
│   └── rootfs/
│       └── container_id/
└── tmp/
    ├── pulls/
    └── builds/
```

### Windows Container Images
```
C:\ProgramData\chef\images\
├── blobs\
│   └── sha256\
│       ├── abc123... (layer VHD)
│       ├── def456... (config)
│       └── ghi789... (manifest)
├── repositories\
│   └── mcr.microsoft.com\
│       └── windows\
│           └── servercore -> ..\..\blobs\sha256\manifest_hash
├── cache\
│   ├── layers\
│   │   ├── vhds\         # Extracted VHD files
│   │   │   └── sha256_abc123.vhdx
│   │   └── mounted\      # Differencing VHDs
│   │       └── diff_xyz.vhdx
│   └── rootfs\
│       └── container_id\ # Final merged filesystem
└── temp\
    ├── pulls\
    └── builds\
```

## API Design

### Core Image Management API
```c
// Image reference structure
struct containerv_image_ref {
    char* registry;      // "docker.io", "mcr.microsoft.com"
    char* namespace;     // "library", "windows"  
    char* repository;    // "ubuntu", "servercore"
    char* tag;          // "22.04", "ltsc2022"
    char* digest;       // "sha256:abc123..." (optional)
};

// Image metadata
struct containerv_image {
    struct containerv_image_ref ref;
    char*    id;                    // Full image ID
    char*    parent_id;             // Parent image ID
    uint64_t size;                  // Total image size
    uint64_t virtual_size;          // Size including all layers
    time_t   created;               // Creation timestamp
    char**   tags;                  // Array of tags
    int      tag_count;             // Number of tags
    char*    os;                    // "linux", "windows"
    char*    architecture;          // "amd64", "arm64"
};

// Layer information
struct containerv_layer {
    char*    digest;                // sha256:...
    uint64_t size;                  // Compressed size
    uint64_t uncompressed_size;     // Uncompressed size
    char*    media_type;            // Layer media type
    char*    cache_path;            // Local cache path
    bool     available;             // Is layer cached locally
};

// Image manifest
struct containerv_manifest {
    int                       schema_version;
    char*                     media_type;
    struct containerv_layer*  config;
    struct containerv_layer*  layers;
    int                       layer_count;
    char*                     os;
    char*                     architecture;
};
```

### Image Operations API
```c
/**
 * @brief Initialize the container image system
 * @param cache_dir Directory for image cache (NULL for default)
 * @return 0 on success, -1 on failure
 */
extern int containerv_images_init(const char* cache_dir);

/**
 * @brief Cleanup and shutdown the image system
 */
extern void containerv_images_cleanup(void);

/**
 * @brief Pull an image from a registry
 * @param image_ref Image reference to pull
 * @param progress_callback Optional progress callback
 * @param callback_data User data for progress callback
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_pull(
    const struct containerv_image_ref* image_ref,
    void (*progress_callback)(const char* status, int percent, void* data),
    void* callback_data
);

/**
 * @brief Push an image to a registry  
 * @param image_ref Image reference to push
 * @param progress_callback Optional progress callback
 * @param callback_data User data for progress callback
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_push(
    const struct containerv_image_ref* image_ref,
    void (*progress_callback)(const char* status, int percent, void* data),
    void* callback_data
);

/**
 * @brief List locally cached images
 * @param images Output array of images
 * @param max_images Maximum number of images to return
 * @return Number of images returned, or -1 on error
 */
extern int containerv_image_list(
    struct containerv_image* images,
    int max_images
);

/**
 * @brief Get detailed information about an image
 * @param image_ref Image reference
 * @param image Output image information
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_inspect(
    const struct containerv_image_ref* image_ref,
    struct containerv_image* image
);

/**
 * @brief Remove an image from local cache
 * @param image_ref Image reference to remove
 * @param force Force removal even if containers are using it
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_remove(
    const struct containerv_image_ref* image_ref,
    bool force
);

/**
 * @brief Build an image from a Dockerfile/Containerfile
 * @param build_context Path to build context directory
 * @param dockerfile Path to Dockerfile (relative to context)
 * @param image_ref Image reference for the built image
 * @param build_args Array of build arguments (KEY=VALUE)
 * @param arg_count Number of build arguments
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_build(
    const char* build_context,
    const char* dockerfile,
    const struct containerv_image_ref* image_ref,
    const char** build_args,
    int arg_count
);
```

### Layer Management API
```c
/**
 * @brief Extract and mount image layers for container creation
 * @param image_ref Image to mount
 * @param mount_path Output path where image is mounted
 * @param mount_path_size Size of mount_path buffer
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_mount(
    const struct containerv_image_ref* image_ref,
    char* mount_path,
    size_t mount_path_size
);

/**
 * @brief Unmount and cleanup image layers
 * @param mount_path Path returned by containerv_image_mount
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_unmount(const char* mount_path);

/**
 * @brief Create a writable layer on top of an image
 * @param image_ref Base image reference
 * @param container_id Container identifier
 * @param rw_path Output path to writable layer
 * @param rw_path_size Size of rw_path buffer
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_create_rw_layer(
    const struct containerv_image_ref* image_ref,
    const char* container_id,
    char* rw_path,
    size_t rw_path_size
);

/**
 * @brief Remove container's writable layer
 * @param container_id Container identifier
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_remove_rw_layer(const char* container_id);
```

## Platform-Specific Implementation

### Linux Layer Management (OverlayFS)
```c
// Linux-specific layer mounting
struct linux_layer_mount {
    char* lower_dirs;       // Colon-separated list of lower directories
    char* upper_dir;        // Writable upper directory
    char* work_dir;         // OverlayFS work directory
    char* merged_dir;       // Final merged directory
};

static int linux_mount_overlay_layers(
    const struct containerv_layer* layers,
    int layer_count,
    const char* container_id,
    struct linux_layer_mount* mount_info
) {
    // 1. Extract all layers to cache/layers/extracted/
    // 2. Create upper and work directories for container
    // 3. Mount overlayfs with all layers as lowerdir
    // 4. Return merged directory path
}
```

### Windows Layer Management (VHD Differencing)
```c
// Windows-specific layer mounting  
struct windows_layer_mount {
    HANDLE* layer_handles;      // Array of VHD handles
    int     layer_count;        // Number of layers
    HANDLE  rw_handle;          // Read-write differencing VHD
    char*   mount_path;         // Final mounted path
};

static int windows_mount_vhd_layers(
    const struct containerv_layer* layers,
    int layer_count,
    const char* container_id,
    struct windows_layer_mount* mount_info
) {
    // 1. Extract layer VHD files to cache/layers/vhds/
    // 2. Create differencing VHD for container
    // 3. Chain VHDs: base -> layer1 -> layer2 -> rw_layer
    // 4. Attach final VHD to HyperV VM
    // 5. Return VM mount path
}
```

## Registry Integration

### Registry Client Implementation
```c
struct containerv_registry_client {
    char* base_url;             // Registry base URL
    char* username;             // Authentication username  
    char* password;             // Authentication password/token
    char* user_agent;           // HTTP user agent
    bool  verify_ssl;           // SSL certificate verification
};

struct containerv_pull_context {
    struct containerv_registry_client* client;
    struct containerv_image_ref*       image_ref;
    char*                              manifest_json;
    struct containerv_manifest*        manifest;
    int                                layers_downloaded;
    int                                total_layers;
    uint64_t                           bytes_downloaded;
    uint64_t                           total_bytes;
};

// Registry operations
static int registry_get_manifest(
    struct containerv_registry_client* client,
    const struct containerv_image_ref* image_ref,
    struct containerv_manifest* manifest
);

static int registry_download_layer(
    struct containerv_registry_client* client,
    const struct containerv_layer* layer,
    const char* output_path,
    void (*progress_callback)(int percent, void* data),
    void* callback_data
);

static int registry_upload_layer(
    struct containerv_registry_client* client,
    const struct containerv_layer* layer,
    const char* layer_path
);
```

## Integration with Containerv

### Enhanced Container Creation
```c
// Extended containerv_create to support images
extern int containerv_create_from_image(
    const struct containerv_image_ref* image_ref,
    struct containerv_options*         options,
    struct containerv_container**      container_out
);

// Modified container options to include image
extern void containerv_options_set_image(
    struct containerv_options*         options,
    const struct containerv_image_ref* image_ref
);

// Get image information from container
extern int containerv_get_image(
    struct containerv_container*       container,
    struct containerv_image_ref*       image_ref
);
```

### Container Creation Flow with Images
```c
int containerv_create_from_image(
    const struct containerv_image_ref* image_ref,
    struct containerv_options*         options,
    struct containerv_container**      container_out
) {
    // 1. Check if image exists locally, pull if needed
    if (!image_exists_locally(image_ref)) {
        int ret = containerv_image_pull(image_ref, NULL, NULL);
        if (ret != 0) return ret;
    }
    
    // 2. Mount image layers and create writable layer
    char rootfs_path[PATH_MAX];
    ret = containerv_image_mount(image_ref, rootfs_path, sizeof(rootfs_path));
    if (ret != 0) return ret;
    
    // 3. Create container with mounted rootfs
    ret = containerv_create(rootfs_path, options, container_out);
    if (ret != 0) {
        containerv_image_unmount(rootfs_path);
        return ret;
    }
    
    // 4. Store image reference in container metadata
    // ... implementation details ...
    
    return 0;
}
```

## Image Cache Management

### Cache Structure and Policies
```c
struct containerv_cache_config {
    char*    cache_dir;             // Base cache directory
    uint64_t max_size;              // Maximum cache size in bytes
    int      max_age_days;          // Maximum age for cached items
    bool     auto_gc;               // Automatic garbage collection
    int      gc_interval_hours;     // GC run interval
};

struct containerv_cache_stats {
    uint64_t total_size;            // Current cache size
    uint64_t available_space;       // Available disk space
    int      image_count;           // Number of cached images
    int      layer_count;           // Number of cached layers
    time_t   last_gc;               // Last garbage collection time
};

/**
 * @brief Configure image cache settings
 * @param config Cache configuration
 * @return 0 on success, -1 on failure
 */
extern int containerv_cache_configure(const struct containerv_cache_config* config);

/**
 * @brief Get cache statistics
 * @param stats Output cache statistics
 * @return 0 on success, -1 on failure
 */
extern int containerv_cache_get_stats(struct containerv_cache_stats* stats);

/**
 * @brief Run garbage collection on image cache
 * @param force Force cleanup even if not needed
 * @return Number of items cleaned up, or -1 on error
 */
extern int containerv_cache_gc(bool force);

/**
 * @brief Prune unused images and layers
 * @param max_age_days Remove items older than this (0 for all unused)
 * @return Number of items pruned, or -1 on error
 */
extern int containerv_cache_prune(int max_age_days);
```

## Build System Integration

### Dockerfile Processing
```c
struct containerv_build_context {
    char*    context_dir;           // Build context directory
    char*    dockerfile_path;       // Path to Dockerfile
    char**   ignore_patterns;       // .dockerignore patterns
    int      ignore_count;          // Number of ignore patterns
};

struct containerv_build_step {
    char*    instruction;           // FROM, RUN, COPY, etc.
    char*    arguments;             // Instruction arguments
    char*    comment;               // Comments from Dockerfile
    int      line_number;           // Line number in Dockerfile
};

/**
 * @brief Parse Dockerfile into build steps
 * @param dockerfile_path Path to Dockerfile
 * @param steps Output array of build steps
 * @param max_steps Maximum number of steps
 * @return Number of steps parsed, or -1 on error
 */
extern int containerv_dockerfile_parse(
    const char*                   dockerfile_path,
    struct containerv_build_step* steps,
    int                           max_steps
);

/**
 * @brief Execute a build step
 * @param step Build step to execute
 * @param build_context Build context
 * @param temp_container Temporary container for step execution
 * @return 0 on success, -1 on failure
 */
extern int containerv_build_step_execute(
    const struct containerv_build_step* step,
    struct containerv_build_context*    build_context,
    struct containerv_container*        temp_container
);
```

## Security and Verification

### Image Verification
```c
struct containerv_signature {
    char* algorithm;                // Signature algorithm
    char* signature;                // Base64 signature
    char* keyid;                   // Signing key ID
};

struct containerv_trust_policy {
    char*  registry_pattern;        // Registry pattern to match
    bool   require_signature;       // Require valid signature
    char** trusted_keys;            // Array of trusted key IDs
    int    key_count;              // Number of trusted keys
};

/**
 * @brief Verify image signature
 * @param image_ref Image to verify
 * @param signature Signature to check
 * @param trust_policy Trust policy to apply
 * @return 0 if valid, -1 if invalid or error
 */
extern int containerv_image_verify_signature(
    const struct containerv_image_ref* image_ref,
    const struct containerv_signature* signature,
    const struct containerv_trust_policy* trust_policy
);

/**
 * @brief Scan image for vulnerabilities
 * @param image_ref Image to scan
 * @param scan_results Output scan results
 * @return 0 on success, -1 on failure
 */
extern int containerv_image_security_scan(
    const struct containerv_image_ref* image_ref,
    struct containerv_scan_results*    scan_results
);
```

## Performance Considerations

### Caching Strategy
1. **Layer Deduplication**: Share common layers between images
2. **Lazy Pulling**: Only pull layers when needed
3. **Parallel Downloads**: Download multiple layers concurrently  
4. **Compression**: Use efficient compression for layer storage
5. **Memory Mapping**: Use mmap for large layer files

### Platform Optimizations

#### Linux Optimizations
```c
// Use OverlayFS native features for best performance
static int optimize_overlayfs_mount(const char* merged_dir) {
    // 1. Use index=on for faster lookups
    // 2. Use metacopy=on for metadata-only copies
    // 3. Use redirect_dir=on for efficient renames
    // 4. Use xino=auto for inode number consistency
}

// Pre-warm page cache for frequently used layers
static int prewarm_layer_cache(const struct containerv_layer* layers, int count) {
    // Use readahead() syscall to load layer data into page cache
}
```

#### Windows Optimizations  
```c
// Optimize VHD performance for container workloads
static int optimize_vhd_performance(HANDLE vhd_handle) {
    // 1. Set appropriate VHD cache policy
    // 2. Configure optimal block size
    // 3. Use sparse VHDs where possible
    // 4. Enable compression for space efficiency
}

// Use HyperV VM memory optimization
static int optimize_vm_memory(struct hcs_container* container) {
    // 1. Configure dynamic memory allocation
    // 2. Set appropriate memory weight
    // 3. Enable memory hot-add if supported
}
```

## Implementation Plan

### Phase 1: Core Infrastructure
1. **Basic Image Management**
   - Image metadata structures
   - Local image storage
   - Basic pull/push operations
   - Simple layer extraction

2. **Platform Abstraction**
   - Cross-platform layer mounting
   - Filesystem abstraction layer
   - Platform-specific optimizations

### Phase 2: Registry Integration
1. **Registry Client**
   - HTTP client implementation
   - Authentication support
   - Manifest handling
   - Layer download/upload

2. **OCI Compliance**
   - Full OCI image spec support
   - Multi-architecture images
   - Image signing/verification

### Phase 3: Advanced Features
1. **Build System**
   - Dockerfile parsing
   - Build step execution
   - Layer caching during builds
   - Multi-stage builds

2. **Performance Optimization**
   - Layer deduplication
   - Parallel operations
   - Smart caching
   - Background garbage collection

### Phase 4: Enterprise Features
1. **Security Integration**
   - Image vulnerability scanning
   - Signature verification
   - Trust policies
   - Access control

2. **Monitoring and Management**
   - Usage analytics
   - Performance metrics
   - Health monitoring
   - Administrative tools

This comprehensive Container Image System will provide Chef's containerv with production-ready image management capabilities that are fully compatible with OCI standards while leveraging platform-specific optimizations for both Linux and Windows environments.