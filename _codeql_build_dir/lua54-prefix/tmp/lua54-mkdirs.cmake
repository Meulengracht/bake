# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/src/lua54")
  file(MAKE_DIRECTORY "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/src/lua54")
endif()
file(MAKE_DIRECTORY
  "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/src/lua54-build"
  "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix"
  "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/tmp"
  "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/src/lua54-stamp"
  "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/src"
  "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/src/lua54-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/src/lua54-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/runner/work/bake/bake/_codeql_build_dir/lua54-prefix/src/lua54-stamp${cfgdir}") # cfgdir has leading slash
endif()
