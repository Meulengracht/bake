# GitHub Copilot Instructions for Chef/Bake

## Project Overview

Chef is a cross-platform package management system and application format designed for portability and ease of use. The project is written in C to maximize portability and consists of:

- **bake**: Recipe building and package creation tool
- **order**: Package publishing and account management tool
- **serve**: Package installation and management client
- **served**: Application backend daemon for package management
- **bakectl**: Build control utilities
- **cvctl**: Container control utilities
- **mkcdk**: Disk image utility

The project emphasizes cross-compilation support and lightweight package management as an alternative to existing systems.

## Technology Stack

- **Languages**: C (primary), with some CMake configuration
- **Build System**: CMake 3.14.3 or higher
- **External Dependencies**:
  - libcurl (HTTP/HTTPS operations)
  - OpenSSL (cryptography)
  - libfuse3 (Linux filesystem operations)
  - libcap (Linux capabilities)
  - Lua 5.4 (embedded scripting)
  - zstd (compression)
  - jansson (JSON parsing)
- **Platforms**: Linux, Windows (in progress)
- **Version Control**: Git with submodules

## Repository Structure

```
/
├── tools/              # Command-line tools (bake, order, serve, etc.)
│   ├── bake/          # Recipe building tool
│   ├── order/         # Publishing and account management
│   ├── serve/         # Package installation client
│   ├── bakectl/       # Build control utilities
│   ├── cvctl/         # Container control
│   ├── mkcdk/         # Disk image utility
│   └── serve-exec/    # Package execution helper
├── libs/              # Core libraries
│   ├── chefclient/    # Chef API client library
│   ├── common/        # Shared utilities
│   ├── containerv/    # Container management
│   ├── dirconf/       # Directory configuration
│   ├── disk/          # Disk and filesystem operations
│   ├── fridge/        # Ingredient cache management
│   ├── oven/          # Build backend implementations
│   ├── package/       # Package file format handling
│   ├── platform/      # Platform abstraction layer
│   ├── remote/        # Remote build operations
│   ├── vlog/          # Logging library
│   └── yaml/          # YAML recipe parsing
├── daemons/           # Background services
│   ├── served/        # Package management daemon
│   ├── cookd/         # Build server daemon
│   ├── waiterd/       # Build server manager
│   └── cvd/           # Container daemon
├── protocols/         # IPC protocol definitions (.gr files)
├── examples/          # Example recipes and configurations
│   ├── recipes/       # Sample recipes
│   └── images/        # Disk image examples
├── cmake/             # CMake modules and external dependencies
└── dist/              # Distribution packaging files
```

## Key Concepts

### Recipes
YAML files that describe how to build packages. Key components:
- **project**: Metadata (name, version, author, description, license)
- **ingredients**: Dependencies needed for building
- **recipes**: Build steps (configure, build, install)
- **packs**: Output package definitions

### Packs
Output packages with types:
- **ingredient**: Build-time dependency (headers, libraries)
- **application**: Installable applications
- **toolchain**: Cross-compilation toolchains

### Ingredients
Dependencies that can be:
- Other Chef packages (from Chef repository)
- Toolchains for cross-compilation
- Build and runtime libraries

### Steps
Build operations with types:
- **generate**: Configuration steps (cmake, autotools)
- **build**: Compilation steps (make, msbuild)
- **script**: Custom shell/PowerShell scripts

## Build Instructions

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt-get install -y libfuse3-3 libfuse3-dev libcap2 libcap-dev \
                        libcurl4-openssl-dev libssl-dev cmake build-essential

# Clone with submodules
git clone --recursive https://github.com/Meulengracht/bake.git
cd bake
```

### Building
```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Test
cd build && ctest -C Release
```

### Build Options
- `CHEF_BUILD_TOOLS`: Build bake toolset (default: ON)
- `CHEF_BUILD_RUNTIME`: Build serve daemon (default: ON)
- `CHEF_BUILD_CONTAINER`: Build container manager (default: ON)
- `CHEF_BUILD_SERVER`: Build cook build server (default: ON)
- `CHEF_BUILD_MANAGER`: Build waiter build server manager (default: ON)
- `CHEF_BUILD_AS_SNAP`: Build without setuid on Unix (default: OFF)

## Development Workflow

### Environment Variables
During recipe execution, Chef exposes:
- `PROJECT_PATH`: Project root directory
- `INSTALL_PREFIX`: Installation directory for built files
- `CHEF_HOST_PLATFORM`: Build platform
- `CHEF_HOST_ARCHITECTURE`: Build architecture
- `TOOLCHAIN_PREFIX`: Toolchain location (cross-compile)
- `BUILD_INGREDIENTS_PREFIX`: Ingredients location (cross-compile)
- `CHEF_TARGET_PLATFORM`: Target platform (cross-compile)
- `CHEF_TARGET_ARCHITECTURE`: Target architecture (cross-compile)

### Common Commands

#### Bake (Building)
```bash
bake init                           # Initialize new recipe
bake build [recipe.yaml]           # Build recipe (defaults to chef.yaml or recipe.yaml)
bake clean                         # Clean build artifacts
bake build --platform=linux --archs=amd64  # Cross-compile
bake remote init                   # Setup remote build
bake remote build --archs=amd64,arm64      # Remote parallel builds
bake fridge list                   # List cached ingredients
bake fridge clean                  # Clear ingredient cache
```

#### Order (Publishing)
```bash
order account whoami               # View/setup account
order publish package.pack         # Publish package
order info publisher/package       # View package info
order find <query>                 # Search packages
order package list                 # List your packages
order config                       # View configuration
```

#### Serve (Installation)
```bash
serve install publisher/package    # Install package
serve list                        # List installed packages
serve update [package]            # Update package(s)
serve remove package              # Remove package
```

## Coding Standards

### General Guidelines
- **Language**: Pure C for maximum portability
- **Headers**: Use include guards or `#pragma once`
- **Formatting**: Follow existing style (4-space indentation)
- **Error Handling**: Always check return values
- **Memory Management**: No leaks - free all allocated memory
- **Cross-platform**: Use platform abstraction layer (libs/platform)
- **Logging**: Use vlog library for consistent logging

### File Organization
- Header files in `include/` directories or alongside implementation
- Implementation in `*.c` files
- Platform-specific code in separate files (e.g., `*-linux.c`, `*-windows.c`)

### Naming Conventions
- Functions: lowercase with underscores (`parse_recipe`, `build_package`)
- Types: lowercase with underscores, may use typedefs
- Constants/Macros: UPPERCASE with underscores
- Global variables: prefix with `g_`

### Common Patterns
```c
// Error handling pattern
int result = function_call();
if (result != 0) {
    VLOG_ERROR("operation", "failed: %d\n", result);
    return result;
}

// Platform abstraction
#if defined(_WIN32)
    // Windows implementation
#else
    // Unix/Linux implementation
#endif
```

## Testing

The project uses CTest for testing:
```bash
cd build
ctest -C Release          # Run all tests
ctest -V                  # Verbose output
ctest -R <pattern>        # Run specific tests
```

Currently, the test infrastructure is minimal. When adding tests:
- Place test files near the code they test
- Register tests in CMakeLists.txt using `add_test()`
- Ensure tests are platform-agnostic or properly guarded

## Integration Points

### Chef API
- Base URL: `https://api.chef.io` (v2)
- Authentication: OAuth2 with device flow
- Operations: Package publishing, downloading, metadata queries

### Protocols
Protocol definitions in `protocols/*.gr` files (Gracht RPC format):
- `served.gr`: Package management daemon protocol
- `waiterd.gr`: Build manager protocol (includes waiterd service for clients and waiterd_cook service for cookd build servers)
- `cvd.gr`: Container daemon protocol

## Common Tasks

### Adding a New Tool
1. Create directory in `tools/<toolname>/`
2. Add `CMakeLists.txt` with executable target
3. Create `main.c` with command handlers
4. Add commands in separate files in `commands/` directory
5. Update root CMakeLists.txt if needed

### Adding a New Library
1. Create directory in `libs/<libname>/`
2. Add `CMakeLists.txt` with library target
3. Place public headers in `include/<libname>/`
4. Implement in `.c` files
5. Update dependent projects' CMakeLists.txt

### Supporting a New Build System
1. Add backend in `libs/oven/backends/<system>/`
2. Implement configuration, build, and install operations
3. Register backend in oven's step dispatcher
4. Update recipe specification documentation

### Adding Cross-Platform Support
1. Identify platform-specific code
2. Create separate implementation files (`*-linux.c`, `*-windows.c`)
3. Use preprocessor guards for compile-time selection
4. Update platform abstraction layer if needed
5. Test on target platform

## Dependencies Management

### Adding External Dependencies
1. Prefer external CMake modules in `cmake/external/`
2. Use FetchContent for small dependencies
3. Document system dependencies in README.md
4. Update CI workflows (.github/workflows/build.yml)

### Submodules
- `libs/gracht/`: RPC framework
- `libs/vafs/`: Virtual archive filesystem
Update with: `git submodule update --recursive`

## CI/CD

GitHub Actions workflows in `.github/workflows/`:
- **build.yml**: Builds on Ubuntu and Windows
- **codeql.yml**: Security analysis

When making changes:
- Ensure builds pass on both Linux and Windows
- Fix any security warnings from CodeQL
- Test cross-compilation if touching build system

## Architecture Notes

### Package Format
- Extension: `.pack`
- Format: VaFS (Virtual Archive FileSystem)
- Compression: zstd
- Metadata: Embedded JSON manifest

### Build Process Flow
1. Parse recipe YAML
2. Resolve and download ingredients
3. Unpack ingredients to build cache
4. Execute recipe steps in order
5. Install files to temporary prefix
6. Package files into .pack format
7. Generate package manifest

### Container Support
- Linux: Uses namespaces, cgroups, overlayfs
- Windows: HCI layer (planned)
- Provides isolated build environments

## Tips for Contributors

1. **Start Small**: Familiarize yourself with the codebase by reading tool entry points (`main.c` files)
2. **Check Examples**: Review `examples/recipes/` for recipe patterns
3. **Use Existing Patterns**: Follow patterns from similar existing code
4. **Platform Testing**: Test on Linux first, then validate Windows behavior
5. **Documentation**: Update README.md recipe specification if changing recipe format
6. **Build Locally**: Always test full builds before committing
7. **Dependencies**: Minimize external dependencies; prefer vendored or fetch-on-build

## Troubleshooting

### Build Issues
- Ensure all submodules are initialized: `git submodule update --init --recursive`
- Check for missing system dependencies (libcurl, openssl, etc.)
- Clear build directory and reconfigure if CMake cache is stale

### Runtime Issues
- Check `~/.config/chef/` for configuration and logs
- Enable verbose logging with environment variables (check vlog implementation)
- Verify ingredient cache in `~/.cache/chef/fridge/`

## Additional Resources

- `README.md`: Comprehensive recipe specification
- `daemons/served/README.md`: Package daemon protocol specification
- `examples/recipes/`: Real-world recipe examples
- Chef API: https://api.chef.io (package repository API)
