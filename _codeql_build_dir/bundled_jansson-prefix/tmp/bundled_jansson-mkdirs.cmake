# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/src/bundled_jansson")
  file(MAKE_DIRECTORY "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/src/bundled_jansson")
endif()
file(MAKE_DIRECTORY
  "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/src/bundled_jansson-build"
  "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix"
  "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/tmp"
  "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/src/bundled_jansson-stamp"
  "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/src"
  "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/src/bundled_jansson-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/src/bundled_jansson-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/runner/work/bake/bake/_codeql_build_dir/bundled_jansson-prefix/src/bundled_jansson-stamp${cfgdir}") # cfgdir has leading slash
endif()
