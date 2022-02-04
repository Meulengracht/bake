/**
 * Copyright 2022, Philip Meulengracht
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

const char* g_appYaml = 
    "# project information, packaging data\n"
    "project:\n"
    "  name: Simple Application Recipe\n"
    "  description: A simple application recipe\n"
    "  type: application\n"
    "  version: 0.1\n"
    "  platform: linux\n"
    "  architecture: amd64\n"
    "  license: MIT\n"
    "  homepage:\n"
    "\n\n"
    "ingredients:\n"
    "  - name: libc\n"
    "    version: 0.2\n"
    "    description: A library\n"
    "\n\n"
    "# There are a number of keywords that can be used in the recipe\n"
    "# steps during generate, build, install and package.\n"
    "recipes:\n"
    "  # The generate step is run first, and the folder generation is run\n"
    "  # from is denoted by ${BAKE_BUILD_DIR}.  Bake expects all artifacts that\n"
    "  # should be included in the package to be placed in ${BAKE_ARTIFACT_DIR}.\n"
    "  - type: generate\n"
    "    commands:\n"
    "      - configure ${BAKE_SOURCE_DIR} --prefix=${BAKE_ARTIFACT_DIR}\n"
    "  # The build step is executed from ${BAKE_BUILD_DIR}\n"
    "  - type: build\n"
    "    depends: generate\n"
    "    commands:\n"
    "      - make\n"
    "      - make install\n"
    "\n\n"
    "# commands are applications or services that should be available\n"
    "# to the system once the application is installed. These commands\n"
    "# can be registered to a binary or script inside the app package\n"
    "commands:\n"
    "  myapp:\n"
    "    path: /bin/myapp\n"
    "    arguments: [--arg1 --arg2]\n"
    "    type: executable\n"
    "    description: A simple application\n";
