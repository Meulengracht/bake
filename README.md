<h1 align="center" style="margin-top: 0px;">Chef Package Management System</h1>

<div align="center" >

[![Get it from the Snap Store](https://snapcraft.io/static/images/badges/en/snap-store-black.svg)](https://snapcraft.io/vchef)

</div>

Chef is a cross-platform package management system, specifically built to support all kinds of platforms. It's built with cross-compilation in mind and is also built to work as an application format. It's written in C to make it as portable as possible. It is relatively lightweight alternative to other package management systems, and provides an online repository for all your packages as well.

<h1 align="center" style="margin-top: 0px;">Getting Started</h1>

The best way to get started is to install the latest version of Chef using the [snap store](https://snapcraft.io/vchef). However not everyone likes or uses snaps, and in this case it's recommended to build chef from source, as chef is not distributed as a debian package yet!

## Roadmap

Features that is expected in the upcoming 1.5 release

- [x] Served initial feature completion
- [x] Remote management commands
  * `bake remote list --arch=...`
  * `bake remote info [agent]`
- [x] Disk image utility based on yaml descriptions
  * Support files/directories/chef packages as data sources
  * Support mfs/fat initially
  * Support .img outputs
- [x] Chef Store V2
  * Move away from Azure, use more generic deployment
  * New sign-on process, move away from external SSO
  * Support multi-publisher per account
  * Support per publisher signing
  * Support api-keys
- [x] Improved windows support
  * Finish the platform layer for windows
  * Fix the configure process on windows

Features that is expected in the planned 1.6 release

- Complete windows support
  * Fix the build issues
  * Proper support for windows bases
  * Extend containerv to support the windows HCI layer

## Account Setup

To get started with your account setup, you will need to activate one of the privileged commands provided by 'order'

```
$ order account whoami
```

This will initialize the account setup by asking you to login/register an account using OAUTH2 authentation methods. It will provide you with a link and a code you need to paste in to the browser.

Once authentitacted, you will be prompted to provide your publisher name (the name from which your packages will be published under), and an email used to notify you once your publisher name is approved (or rejected, in which case you may choose another).

Publishing names will be reviewed quickly (usually within 24 hours), and then you will be able to publish packages to the official Chef repository. While waiting for
the approval, you can also see the current account status using

```
$ order account whoami
```

## Building Chef from Source

### Linux

#### Prerequisites
```bash
# Ubuntu/Debian
sudo apt-get install -y libfuse3-3 libfuse3-dev libcap2 libcap-dev \
                        libcurl4-openssl-dev libssl-dev libseccomp-dev \
                        cmake build-essential

# Clone with submodules
git clone --recursive https://github.com/Meulengracht/bake.git
cd bake
```

#### Build
```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release -j$(nproc)

# Install (optional)
sudo cmake --install build
```

### Windows

#### Prerequisites
- Visual Studio 2019 or later (with C/C++ development tools), or MinGW-w64
- CMake 3.14.3 or later
- Git for Windows

#### Build with Visual Studio
```powershell
# Clone with submodules
git clone --recursive https://github.com/Meulengracht/bake.git
cd bake

# Configure for Visual Studio
cmake -B build -G "Visual Studio 16 2019" -A x64

# Build
cmake --build build --config Release

# Install (optional, requires admin)
cmake --install build
```

#### Build with MinGW
```bash
# Clone with submodules
git clone --recursive https://github.com/Meulengracht/bake.git
cd bake

# Configure for MinGW
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build -j%NUMBER_OF_PROCESSORS%

# Install (optional)
cmake --install build
```

**Note:** On Windows, symbolic link creation may require administrator privileges or Windows 10+ Developer Mode to be enabled.

## Your first recipe

The easiest way to get started is to use the init helper provided by the command 'bake'. This will create a new .yaml recipe in the current directory, where you can
customize the recipe to your liking.

```
$ bake init
```

For examples on recipes, please see the examples/ directory, the chef.yaml, or refer
to the Recipe Specification in the bottom of this page.

Once the recipe is created, you can start baking!

## Building

```
$ bake my-recipe.yaml
```

This should create a new (or multiple based on your recipe) *.pack file in the current directory, which are now ready for publishing! During execution of steps,
chef will expose the following variables to help control the build process:

  * `PROJECT_PATH`: path to the root of the project (where bake was invoked)
  * `INSTALL_PREFIX`: path to where the steps will install files to be packed
  * `CHEF_HOST_PLATFORM`: the platform for which the package is being built
  * `CHEF_HOST_ARCHITECTURE`: the architecture for which the package is being built

## Cross-compiling

Chef is specifically built to work easily with cross-compilation scenarios. This allows users to build packages for other platforms or architecture and publish them as well.

```
$ bake my-recipe.yaml --cross-compile=linux/i386
```

The above will trigger chef to download ingredients for the linux/i386 platform, and then build the package for that platform. During execution of the different steps, chef will expose the following additional environment variables:

  * `TOOLCHAIN_PREFIX`: path to where the toolchain ingredient is unpacked
  * `BUILD_INGREDIENTS_PREFIX`: path to where the build ingredients are unpacked
  * `CHEF_TARGET_PLATFORM`: the platform which was provided on the commandline
  * `CHEF_TARGET_ARCHITECTURE`: the architecture which was provided on the commandline

## Remote Building

Chef supports remote building through the `bake remote` commands, which allow you to execute builds on remote build servers (agents). This is useful for building packages for architectures that you don't have local access to, or for offloading compute-intensive builds.

### Listing Available Agents

To see what remote build agents are available:

```
$ bake remote list
```

You can filter agents by architecture:

```
$ bake remote list --arch=arm64
```

This will display available agents with their status, supported architectures, and current load.

### Getting Agent Information

To get detailed information about a specific agent:

```
$ bake remote info agent-01
```

This shows the agent's status, supported architectures, and current workload.

### Building Remotely

To execute a build remotely:

```
$ bake remote build my-recipe.yaml
```

For more information on remote build commands, see `bake remote --help`.

## Publishing your first package

Once the packages are built, they are in essence ready for publishing. To publish them, you can use the publish command:

```
$ order publish my-something.pack
```

## Installing packages

Chef packages are designed to be installable, not just used for building. To support this the 'serve' utility
is provided, and you will also need a 'served' daemon for your platform. 

Currently no platforms have a served daemon implemented, however this is currently planned for linux and vali.

```
$ serve install publisher/package
```

The 'serve' utility communicates with the 'served' daemon through a network protocol, to support network control
of a computer. The serve protocol provides the ability to install, update and remove packages from the system. For
more information about the 'served' daemon, see the README in directory daemons/served.

<h1 align="center" style="margin-top: 0px;">Recipe Specification</h1>

See [RECIPE.md](RECIPE.md) for the specification of Chef Recipes.
