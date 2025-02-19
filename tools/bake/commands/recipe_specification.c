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
 */

const char* g_baseYaml = 
"#########################\n"
"# project\n"
"#\n"
"# This member is required, and specifies project information which can be\n"
"# viewed with 'order info'.\n"
"project:\n"
"  ###########################\n"
"  # summary - Required\n"
"  #\n"
"  # A short summary of the project, this will be shown in the first line\n"
"  # of the project info page.\n"
"  summary: Simple Application Recipe\n"
"\n"
"  ###########################\n"
"  # description - Required\n"
"  #\n"
"  # A longer description of the project, detailing what the purpose is and how\n"
"  # to use it.\n"
"  description: A simple application recipe\n"
"\n"
"  ###########################\n"
"  # author - Required\n"
"  #\n"
"  # The project author(s), this is just treated as a string value.\n"
"  author: who made it\n"
"\n"
"  ###########################\n"
"  # email - Required\n"
"  #\n"
"  # The email of the project or the primary author/maintainer.\n"
"  # This will be visible to anyone who downloads the package.\n"
"  email: contact@me.com\n"
"\n"
"  ##########################\n"
"  # version - Required\n"
"  #\n"
"  # A three part version number for the current project version. Chef\n"
"  # automatically adds an auto-incrementing revision number. This means\n"
"  # for every publish done the revision increments, no matter if the \n"
"  # version number stays the same. \n"
"  version: 0.1.0\n"
"  \n"
"  #########################\n"
"  # icon - Optional\n"
"  #\n"
"  # The project icon file. This is either a png, bmp or jpg file that will be\n"
"  # shown in the project info page.\n"
"  icon: /path/to/icon.png\n"
"  \n"
"  #########################\n"
"  # license - Optional\n"
"  #\n"
"  # Specify the project license, this can either be a short-form of know\n"
"  # licenses or a http link to the project license if a custom one is used.\n"
"  license: MIT\n"
"  \n"
"  #########################\n"
"  # eula - Optional\n"
"  #\n"
"  # If provided, the chef will open and require the user to sign an eula\n"
"  # in case one if required for installing the package. <Planned Feature>\n"
"  # The signing will be done either in the CLI or in the GUI when it arrives.\n"
"  eula: https://myorg.com/project-eula\n"
"\n"
"  #########################\n"
"  # homepage - Optional\n"
"  #\n"
"  # The project website, it is expected for this to be an url if provided.\n"
"  homepage:\n"
"\n"
"###########################\n"
"# ingredients - Optional\n"
"#\n"
"# Ingredients are the same as dependencies. They are either\n"
"# libraries or toolchains the project needs to build correctly.\n"
"ingredients:\n"
"    ###########################\n"
"    # name - Required\n"
"    # \n"
"    # Name of the ingredient required. How the name is given depends on the source\n"
"    # the package comes from. If the ingredient is a chef-package, then it must be\n"
"    # given in the format publisher/package.\n"
"  - name: vali/package\n"
"    \n"
"    ###########################\n"
"    # version - Optional\n"
"    #\n"
"    # A specific version can be given, this will attempt to resolve the package\n"
"    # with the wanted version, if no version is provided, then the latest will be\n"
"    # fetched.\n"
"    # Supported version formats:\n"
"    #  - <major>.<minor>.<patch>\n"
"    #  - <revision>\n"
"    version: 1.0.1\n"
"\n"
"    ###########################\n"
"    # include-filters - Optional\n"
"    #\n"
"    # Array of filters that should be used to filter files from this ingredient.\n"
"    # This can only be used in conjungtion with 'include: true', and exclusion\n"
"    # filters can be set by prefixing with '!'\n"
"    include-filters:\n"
"      - bin/*.dll\n"
"      - lib/*.lib\n"
"      - !share\n"
"\n"
"    ###########################\n"
"    # channel - Optional\n"
"    #\n"
"    # The channel to retrieve the package from. The default channel to retrieve\n"
"    # packages from is 'stable'.\n"
"    channel: stable\n"
"\n"
"###########################\n"
"# recipes - Required\n"
"#\n"
"# Recipes describe how to build up all components of this project. A project\n"
"# can consist of multiple recipes, that all make up the final product.\n"
"recipes:\n"
"    ###########################\n"
"    # name - Required\n"
"    # \n"
"    # Name of the recipe. This should be a very short name as it will\n"
"    # be used to scope the build files while building.\n"
"  - name: my-app\n"
"    \n"
"    ###########################\n"
"    # path - Optional\n"
"    # \n"
"    # If the source code is not in the root directory, but in a project subfolder\n"
"    # then path can be used to specify where the root of source code of this recipe\n"
"    # is in relative terms from project root.\n"
"    path: source/\n"
"\n"
"    ###########################\n"
"    # toolchain - Optional\n"
"    # \n"
"    # If the recipe needs to be built using a specific toolchain this can be\n"
"    # specified here, this must refer to a package in 'ingredients'\n"
"    toolchain: vali/package\n"
"\n"
"    ###########################\n"
"    # steps - Required\n"
"    #\n"
"    # Steps required to build the project. This usually involves\n"
"    # configuring, building and installing the project. Each generator backend\n"
"    # will automatically set the correct installation prefix when invoking the\n"
"    # generator.\n"
"    steps:\n"
"      ###########################\n"
"      # name - Required\n"
"      #\n"
"      # Name of the step, this can also be used to refer to this step when\n"
"      # setting up step dependencies.\n"
"    - name: config\n"
"\n"
"      ###########################\n"
"      # depends - Optional\n"
"      # \n"
"      # List of steps that this step depends on. Steps are executed in sequential order\n"
"      # of how they are defined in the YAML file. But when requesting specific steps to run\n"
"      # then chef needs to know which steps will be invalidated once that step has rerun.\n"
"      depends: [config]\n"
"\n"
"      ###########################\n"
"      # type - Required\n"
"      #    values: {generate, build, script}\n"
"      #\n"
"      # The step type, which must be specified. This determines which\n"
"      # kinds of 'system' is available for this step.\n"
"      type: generate\n"
"      \n"
"      ###########################\n"
"      # system - Required\n"
"      #    generate-values: {autotools, cmake}\n"
"      #    build-values:    {make}\n"
"      #    script-values:   <none>\n"
"      #\n"
"      # This determines which backend will be used for this step. Configure steps\n"
"      # will only be invoked when they change (todo!), but build/install steps are always\n"
"      # executed.\n"
"      system: autotools\n"
"\n"
"      ###########################\n"
"      # script - Required for script\n"
"      # \n"
"      # Shell script that should be executed. The working directory of the script\n"
"      # will be the build directory for this recipe. The project directory and install\n"
"      # directories can be refered to through $[[ PROJECT_PATH ]] and $[[ INSTALL_PREFIX ]].\n"
"      # On linux, this will be run as a shell script, while on windows it will run as a \n"
"      # powershell script\n"
"      script: |\n"
"        valid=true\n"
"        count=1\n"
"        while [ $valid ]\n"
"        do\n"
"          echo $count\n"
"          if [ $count -eq 5 ];\n"
"          then\n"
"            break\n"
"          fi\n"
"          ((count++))\n"
"        done\n"
"\n"
"      ###########################\n"
"      # arguments - Optional\n"
"      # \n"
"      # List of arguments that should be passed to the spawn invocation.\n"
"      arguments: [--arg=value]\n"
"\n"
"      ###########################\n"
"      # env - Optional\n"
"      #\n"
"      # List of environment variables that should be passed to the spawn\n"
"      # invocations. This will override the inherited host variables if a\n"
"      # variable with the same key is specified on the host. \n"
"      env:\n"
"        VAR: VALUE\n"
"\n"
"packs:\n"
"    ###########################\n"
"    # name - Required\n"
"    # \n"
"    # Name of the pack. This will be used for the filename and also the\n"
"    # name that will be used for publishing. The published name will be\n"
"    # publisher/name of this pack.\n"
"  - name: mypack\n"
"\n"
"    ###########################\n"
"    # type - Required\n"
"    #    values: {ingredient, application, toolchain}\n"
"    #\n"
"    # The project type, this defines how the pack is being used by the backend\n"
"    # when building projects that rely on this package. Toolchains will be unpacked\n"
"    # and treated differently than ingredients would. Only applications can be installed\n"
"    # by the application system, and should only contain the neccessary files to be installed,\n"
"    # while ingredients might contains headers, build files etc.\n"
"    type: application\n"
"\n"
"    ###########################\n"
"    # filters - Optional\n"
"    #\n"
"    # Array of filters that should be used to filter files from the install path\n"
"    # exclusion filters can be set by prefixing with '!'\n"
"    filters:\n"
"      - bin/app\n"
"      - bin/*.dll\n"
"      - share\n"
"    \n"
"    ###########################\n"
"    # commands - Required for applications\n"
"    # \n"
"    # commands are applications or services that should be available\n"
"    # to the system once the application is installed. These commands\n"
"    # can be registered to a binary or script inside the app package\n"
"    commands:\n"
"        ###########################\n"
"        # name - Required\n"
"        # \n"
"        # Name of the command. This is the command that will be exposed\n"
"        # to the system. The name should be unique, and should not contain\n"
"        # spaces.\n"
"      - name: myapp\n"
"        \n"
"        ###########################\n"
"        # name - Required\n"
"        # \n"
"        # Path to the command. This is the relative path from the root\n"
"        # of the pack. So if the application is installed at bin/app then\n"
"        # thats the path that should be used.\n"
"        path: /bin/myapp\n"
"\n"
"        ###########################\n"
"        # arguments - Optional\n"
"        #\n"
"        # Arguments that should be passed to the command when run.\n"
"        arguments: [--arg1, --arg2]\n"
"\n"
"        ###########################\n"
"        # type - Required\n"
"        #    values: {executable, daemon}\n"
"        #\n"
"        # The type of command, this determines how the command is run.\n"
"        type: executable\n"
"\n"
"        ###########################\n"
"        # description - Optional\n"
"        #\n"
"        # Description of the command, will be shown to user if the user decides\n"
"        # to expect the command.\n"
"        description: A simple application\n"
"\n"
"        ###########################\n"
"        # icon - Optional\n"
"        #\n"
"        # Icon that should be shown for this command. This is only used in \n"
"        # combination with the window manager. Every command registered can\n"
"        # also register a seperate icon.\n"
"        icon: /my/app/icon\n"
"\n"
"        ###########################\n"
"        # system-libs - Optional\n"
"        #    default: false\n"
"        #\n"
"        # Informs the library resolver that it can also resolve libraries\n"
"        # the command is linked against from system paths. This means that\n"
"        # libraries not found in ingredients will be resolved in system\n"
"        # library paths. Use with caution.\n"
"        system-libs: true\n";
