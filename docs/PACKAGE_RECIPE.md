<h1 align="center" style="margin-top: 0px;">Recipe Specification</h1>

## Combined build steps

CMake and Autotools projects can configure, build, and install into Chef's staging
directory with a single step:

```yaml
steps:
  - name: build
    type: build
    system: cmake
```

Use `system: autotools` (or its `autoconf` alias) for configure + Make projects.
CMake builds use `cmake --build` and `cmake --install`, so the selected CMake
generator is also used for compilation. The CMake executable must support
`--install` (CMake 3.15 or later).

Optional nested `configure` settings customize configuration without adding a step:

```yaml
steps:
  - name: build
    type: build
    system: cmake
    arguments: [--parallel, '2']
    env:
      SHARED_SETTING: value
    configure:
      source-dir: src
      arguments: [-DBUILD_SHARED_LIBS=ON]
      env:
        CONFIGURE_SETTING: value
```

- `configure.arguments` goes only to configuration. Top-level `arguments` goes
  only to compilation, as it does for existing build steps. For CMake, these are
  `cmake --build` options; put native build-tool options after `--`.
- Top-level `env` applies to configuration, compilation, and installation.
  `configure.env` overrides matching keys for configuration only.
- `configure.source-dir` selects a source subdirectory relative to the recipe
  part's source root. It falls back to the step's `source-dir` when omitted.
- Omitted `configure`, `configure: true`, and `configure: {}` all enable generation.
  `configure: false` skips it and requires an already configured build directory.
- Each execution of a combined step runs configuration before building unless
  configuration is disabled. A configuration failure stops the step immediately.
  Dependencies refer to the named build step as a whole.

Explicit generation remains supported, for example when inserting a script
between configuration and compilation:

```yaml
steps:
  - name: config
    type: generate
    system: cmake
    arguments: [-G, '"Unix Makefiles"']
  - name: build
    type: build
    system: make
    depends: [config]
```

A subsequent `build/cmake` step can instead use `configure: false` and
`depends: [config]`. Chef does not infer this from an earlier generate step.
Direct `build/make` and `build/ninja` steps retain their existing behavior.
Meson retains its existing separate-step behavior; nested `configure` settings
are currently accepted only on CMake and Autotools build steps. Script steps
are unchanged.

### Backend arguments and staging

Backend-generated paths are passed as individual process arguments. Recipe argument
lists retain their existing command-string syntax: use quotes inside a YAML string
when a value contains spaces, for example:

```yaml
configure:
  arguments:
    - '-DCMAKE_INSTALL_PREFIX:PATH="/custom prefix"'
    - '-DCMAKE_PREFIX_PATH="/dependency one;/dependency two"'
```

CMake definitions are matched by exact name, including optional types such as
`:PATH`. `CMAKE_PREFIX_PATH` uses semicolon-separated entries on all platforms.
Chef appends the ingredient root, plus `usr` and `usr/local` for Linux, or
`Program Files` for Windows.

CMake, Autotools, and Meson place their installation prefix under `INSTALL_PREFIX`.
An already staged prefix is preserved only when it matches the staging root or a
child path; a sibling with a similar name is staged as a new path. Parent-directory
components (`..`) are rejected. This is lexical path handling, not a sandbox for
build scripts or absolute install destinations defined by the project itself.

Autotools checks both platform and architecture when deciding whether to generate
cross-compilation settings. Its generated `chef-config.site` lives in the build
folder and is selected through `CONFIG_SITE`. It appends ingredient include and
library flags while preserving existing `CPPFLAGS`, `LDFLAGS`, and `CFLAGS`.
An explicit `CONFIG_SITE` environment value takes precedence. Compiler selection
and Autoconf `--host`/`--build` settings remain recipe responsibilities. Autoconf's
compiler flag expansion still requires ingredient paths without whitespace.

### Meson steps

Meson uses an explicit generate step followed by a build step:

```yaml
steps:
  - name: configure
    type: generate
    system: meson
    source-dir: src
    # Optional template, resolved relative to the recipe project directory:
    meson-cross-file: toolchains/cross.txt
    arguments: [--buildtype=release]
  - name: build
    type: build
    system: meson
    depends: [configure]
    arguments: [-j, '2']
```

Generation runs `meson setup` with both source and build directories; subsequent
runs use `--reconfigure`. The cross-file template is expanded into the build
folder, and a read, expansion, or write failure stops generation. Build runs
`meson compile` followed by `meson install --no-rebuild`; clean runs
`meson compile --clean`. Build arguments apply to compilation only, while step
environment settings also apply to installation. Meson must support `compile`
(version 0.54 or later).

```
#########################
# Project metadata (root fields)
#
# name, author, email, and version are required. These fields are written at
# the recipe root; there is no project: wrapper.
name: my-project
author: who made it
email: contact@me.com
version: 0.1.0

#########################
# Optional project metadata
license: MIT
eula: https://myorg.com/project-eula
homepage: https://example.com

# Pack presentation metadata belongs on each pack below. A pack can define
# summary, description, and icon independently; these are not project-root
# fields.

###########################
# ingredients - Optional
#
# Ingredients are the same as dependencies. They are either
# libraries or toolchains the project needs to build correctly.
ingredients:
    ###########################
    # name - Required
    # 
    # Name of the ingredient required. How the name is given depends on the source
    # the package comes from. If the ingredient is a chef-package, then it must be
    # given in the format publisher/package.
  - name: vali/package
    
    ###########################
    # version - Optional
    #
    # A specific version can be given, this will attempt to resolve the package
    # with the wanted version, if no version is provided, then the latest will be
    # fetched.
    # Supported version formats:
    #  - <major>.<minor>.<patch>
    #  - <revision>
    version: 1.0.1

    ###########################
    # include - Optional
    #    values: {false, true}
    #
    # Specifies the ingredient should be bundled into the output
    # of this package build. This is used to include runtime dependencies for
    # applications, or to build aggregate packages. The default value for this
    # is false.
    include: false
    
    ###########################
    # include-filters - Optional
    #
    # Array of filters that should be used to filter files from this ingredient.
    # This can only be used in conjungtion with 'include: true', and exclusion
    # filters can be set by prefixing with '!'
    include-filters:
      - bin/*.dll
      - lib/*.lib
      - !share

    ###########################
    # platform - Optional
    #
    # The platform configuration of the package to retrieve. This is usefull
    # if cross-compiling for another platform. The default value for this is
    # the host platform. The value 'host' is also supported, which can be usefull
    # for toolchains
    platform: linux

    ###########################
    # channel - Optional
    #
    # The channel to retrieve the package from. The default channel to retrieve
    # packages from is 'stable'.
    channel: stable

    ###########################
    # arch - Optional
    #    values: {host, i386, amd64, arm, arm64, rv32, rv64}
    #
    # The architecture configuration of the package to retrieve. This is also usefull
    # for cross-compiling for other architectures. This value defaults to host architecture.
    arch: amd64

    ###########################
    # description - Optional
    #
    # Provides a description for why this ingredient is included in the project.
    description: A library

###########################
# recipes - Required
#
# Recipes describe how to build up all components of this project. A project
# can consist of multiple recipes, that all make up the final product.
recipes:
    ###########################
    # name - Required
    # 
    # Name of the recipe. This should be a very short name as it will
    # be used to scope the build files while building.
  - name: my-app
    
    ###########################
    # path - Optional
    # 
    # If the source code is not in the root directory, but in a project subfolder
    # then path can be used to specify where the root of source code of this recipe
    # is in relative terms from project root.
    path: source/

    ###########################
    # toolchain - Optional
    # 
    # If the recipe needs to be built using a specific toolchain this can be
    # specified here, this must refer to a package in 'ingredients'
    toolchain: vali/package

    ###########################
    # steps - Required
    #
    # Steps required to build the project. This usually involves
    # configuring, building and installing the project. Each generator backend
    # will automatically set the correct installation prefix when invoking the
    # generator.
    steps:
      ###########################
      # name - Required
      #
      # Name of the step, this can also be used to refer to this step when
      # setting up step dependencies.
    - name: build

      ###########################
      # depends - Optional
      # 
      # List of steps that this step depends on. Steps are executed in sequential order
      # of how they are defined in the YAML file. But when requesting specific steps to run
      # then chef needs to know which steps will be invalidated once that step has rerun.
      # depends: [earlier-step]

      ###########################
      # type - Required
      #    values: {generate, build, script}
      #
      # The step type, which must be specified. This determines which
      # kinds of 'system' is available for this step.
      type: build
      
      ###########################
      # system - Required
      #    generate-values: {autotools, autoconf, cmake, meson}
      #    build-values:    {cmake, autotools, autoconf, make, ninja, meson}
      #    script-values:   <none>
      #
      # CMake and Autotools build steps configure, build, and install.
      system: cmake

      # Optional configuration-only settings (or configure: false to skip).
      configure:
        arguments: [-DBUILD_SHARED_LIBS=ON]

      ###########################
      # script - Required for script
      # 
      # A Lua script snippet that will be executed by the oven runtime.
      # The working directory of the script will be the build directory for this recipe.
      # Use the `build.*` APIs to run external tools and resolve paths:
      # - `build.shell(<exe>, <args>)`
      # - `build.paths.project()` (project root)
      # - `build.paths.install()` (install/output root)
      # The snippet can call platform-native tools (e.g. `sh`/`bash` on Linux, `powershell.exe` on Windows).
      script: |
        local project_root = build.paths.project()
        local install_root = build.paths.install()

        -- Example: run a host tool and write into the install root.
        build.shell('powershell.exe', '-NoProfile -NonInteractive -Command "' ..
          "Write-Host 'project='" .. project_root .. "'; " ..
          "Write-Host 'install='" .. install_root .. "'" ..
        '"')
        done

      ###########################
      # arguments - Optional
      # 
      # List of arguments that should be passed to the spawn invocation.
      arguments: [--parallel, '2']

      ###########################
      # env - Optional
      #
      # List of environment variables that should be passed to the spawn
      # invocations. This will override the inherited host variables if a
      # variable with the same key is specified on the host. 
      env:
        VAR: VALUE

packs:
    ###########################
    # name - Required
    # 
    # Name of the pack. This will be used for the filename and also the
    # name that will be used for publishing. The published name will be
    # publisher/name of this pack.
  - name: mypack

    ###########################
    # summary - Required
    #
    # Short text shown when the pack is listed.
    summary: My package

    ###########################
    # description - Optional
    description: A package built with Chef

    ###########################
    # type - Required
    #    values: {ingredient, application, toolchain}
    #
    # The project type, this defines how the pack is being used by the backend
    # when building projects that rely on this package. Toolchains will be unpacked
    # and treated differently than ingredients would. Only applications can be installed
    # by the application system, and should only contain the neccessary files to be installed,
    # while ingredients might contains headers, build files etc.
    type: application

    ###########################
    # ingredient options - Optional
    # 
    # Options provided by this ingredient pack. This can be additional include paths
    # or library paths, or specific compiler/links options that must be added when using
    # this ingredient. All the below options are lists.
    ingredient-options:
      bin-paths: [/bin]
      include-paths: [/include]
      lib-paths: [/libs]
      compiler-args: [--arg1]
      linker-args: [--arg1]

    ###########################
    # filters - Optional
    #
    # Array of filters that should be used to filter files from the install path
    # exclusion filters can be set by prefixing with '!'
    filters:
      - bin/app
      - bin/*.dll
      - share
    
    ###########################
    # capabilities - Optional
    #
    # Capabilities declare what system access this pack needs at runtime.
    # The runtime daemon (served) translates each capability into the
    # appropriate containerv policy plugins and security settings.
    # See docs/capabilities/ for full reference documentation.
    #
    # Supported system capabilities:
    #   network-client     — outbound IP networking (HTTP, databases, APIs, …)
    #   file-control       — file creation/deletion/rename beyond own rootfs
    #   process-control    — fork/exec/clone
    #   package-management — package-management related syscalls
    capabilities:
        ###########################
        # name - Required
        #
        # The capability name.
      - name: network-client

        ###########################
        # config - Optional
        #
        # Key-value pairs that configure the capability. Supported keys
        # depend on the capability; see individual docs for details.
        # For network-client: allow (list of proto/ports rules).
        config:
          allow:
            - tcp: 80,443
            - udp: 53

    ###########################
    # commands - Required for applications
    # 
    # commands are applications or services that should be available
    # to the system once the application is installed. These commands
    # can be registered to a binary or script inside the app package
    commands:
        ###########################
        # name - Required
        # 
        # Name of the command. This is the command that will be exposed
        # to the system. The name should be unique, and should not contain
        # spaces.
      - name: myapp
        
        ###########################
        # name - Required
        # 
        # Path to the command. This is the relative path from the root
        # of the pack. So if the application is installed at bin/app then
        # thats the path that should be used.
        path: /bin/myapp

        ###########################
        # arguments - Optional
        #
        # Arguments that should be passed to the command when run.
        arguments: [--arg1, --arg2]

        ###########################
        # type - Required
        #    values: {executable, daemon}
        #
        # The type of command, this determines how the command is run.
        type: executable

        ###########################
        # description - Optional
        #
        # Description of the command, will be shown to user if the user decides
        # to expect the command.
        description: A simple application

        ###########################
        # icon - Optional
        #
        # Icon that should be shown for this command. This is only used in 
        # combination with the window manager. Every command registered can
        # also register a seperate icon.
        icon: /my/app/icon
```
