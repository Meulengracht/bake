<h1 align="center" style="margin-top: 0px;">Chef Package Management System</h1>

<div align="center" >

[![Get it from the Snap Store](https://snapcraft.io/static/images/badges/en/snap-store-black.svg)](https://snapcraft.io/vchef)

</div>

Chef is a cross-platform package management system, specifically built to support all kinds of platforms. It's built with cross-compilation in mind and is also built to work as an application format. It's written in C to make it as portable as possible. It is relatively lightweight alternative to other package management systems, and provides an online repository for all your packages as well.

<h1 align="center" style="margin-top: 0px;">Getting Started</h1>

The best way to get started is to install the latest version of Chef using the [snap store](https://snapcraft.io/vchef). However not everyone likes or uses snaps, and in this case it's recommended to build chef from source, as chef is not distributed as a debian package yet!

## Roamdmap

The most immediate actions that needs to be implemented for release 1.3.x

- Improved logging output (hooks, git output)
- Implicit recipe package discovery (linux)
- Updated recipe examples

Features that should be in the upcoming 1.4 release

- Cookd
- Waiterd
- TBA

Features that is expected in the upcoming 1.5 release

- Served initial feature completion
- TBA

Features that is expected in the upcoming 1.6 release

- Initial windows support
- TBA

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


```
#########################
# project
#
# This member is required, and specifies project information which can be
# viewed with 'order info'.
project:
  ###########################
  # summary - Required
  #
  # A short summary of the project, this will be shown in the first line
  # of the project info page.
  summary: Simple Application Recipe

  ###########################
  # description - Required
  #
  # A longer description of the project, detailing what the purpose is and how
  # to use it.
  description: A simple application recipe

  ###########################
  # author - Required
  #
  # The project author(s), this is just treated as a string value.
  author: who made it

  ###########################
  # email - Required
  #
  # The email of the project or the primary author/maintainer.
  # This will be visible to anyone who downloads the package.
  email: contact@me.com

  ##########################
  # version - Required
  #
  # A three part version number for the current project version. Chef
  # automatically adds an auto-incrementing revision number. This means
  # for every publish done the revision increments, no matter if the 
  # version number stays the same. 
  version: 0.1.0
  
  #########################
  # icon - Optional
  #
  # The project icon file. This is either a png, bmp or jpg file that will be
  # shown in the project info page.
  icon: /path/to/icon.png
  
  #########################
  # license - Optional
  #
  # Specify the project license, this can either be a short-form of know
  # licenses or a http link to the project license if a custom one is used.
  license: MIT
  
  #########################
  # eula - Optional
  #
  # If provided, the chef will open and require the user to sign an eula
  # in case one if required for installing the package. <Planned Feature>
  # The signing will be done either in the CLI or in the GUI when it arrives.
  eula: https://myorg.com/project-eula

  #########################
  # homepage - Optional
  #
  # The project website, it is expected for this to be an url if provided.
  homepage:

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
    - name: config

      ###########################
      # depends - Optional
      # 
      # List of steps that this step depends on. Steps are executed in sequential order
      # of how they are defined in the YAML file. But when requesting specific steps to run
      # then chef needs to know which steps will be invalidated once that step has rerun.
      depends: [config]

      ###########################
      # type - Required
      #    values: {generate, build, script}
      #
      # The step type, which must be specified. This determines which
      # kinds of 'system' is available for this step.
      type: generate
      
      ###########################
      # system - Required
      #    generate-values: {autotools, cmake}
      #    build-values:    {make}
      #    script-values:   <none>
      #
      # This determines which backend will be used for this step. Configure steps
      # will only be invoked when they change (todo!), but build/install steps are always
      # executed.
      system: autotools

      ###########################
      # script - Required for script
      # 
      # Shell script that should be executed. The working directory of the script
      # will be the build directory for this recipe. The project directory and install
      # directories can be refered to through $[[ PROJECT_PATH ]] and $[[ INSTALL_PREFIX ]].
      # On linux, this will be run as a shell script, while on windows it will run as a 
      # powershell script
      script: |
        valid=true
        count=1
        while [ $valid ]
        do
          echo $count
          if [ $count -eq 5 ];
          then
            break
          fi
          ((count++))
        done

      ###########################
      # arguments - Optional
      # 
      # List of arguments that should be passed to the spawn invocation.
      arguments: [--arg=value]

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

        ###########################
        # system-libs - Optional
        #    default: true
        #
        # Informs the library resolver whether it can also resolve libraries
        # the command is linked against from system paths. This means that
        # libraries not found in ingredients will be resolved in system
        # library paths.
        system-libs: true
```
