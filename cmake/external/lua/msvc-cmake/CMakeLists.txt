cmake_minimum_required(VERSION 3.18)
project(lua54 C)

# This wrapper exists solely to build Lua with MSVC (cl/lib) in a predictable way.
# It is used by cmake/external/lua/lua54.cmake when MSVC is active.

set(LUA_SOURCE_DIR "" CACHE PATH "Path to Lua's src/ directory")

if(NOT LUA_SOURCE_DIR)
  message(FATAL_ERROR "LUA_SOURCE_DIR is required and must point to the extracted lua-*/src directory")
endif()

set(_lua_sources
  lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c ldblib.c ldebug.c ldo.c ldump.c
  lfunc.c lgc.c linit.c liolib.c llex.c lmathlib.c lmem.c loadlib.c lobject.c lopcodes.c
  loslib.c lparser.c lstate.c lstring.c lstrlib.c ltable.c ltablib.c ltm.c lundump.c lutf8lib.c
  lvm.c lzio.c
)

list(TRANSFORM _lua_sources PREPEND "${LUA_SOURCE_DIR}/")

add_library(lua54 STATIC ${_lua_sources})

target_compile_definitions(lua54 PRIVATE LUA_COMPAT_5_3)
if(MSVC)
  target_compile_definitions(lua54 PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

target_include_directories(lua54 PUBLIC "${LUA_SOURCE_DIR}")

# Install headers + library into the staging prefix expected by the top-level build.
install(TARGETS lua54 ARCHIVE DESTINATION lib)
install(FILES
  "${LUA_SOURCE_DIR}/lua.h"
  "${LUA_SOURCE_DIR}/luaconf.h"
  "${LUA_SOURCE_DIR}/lualib.h"
  "${LUA_SOURCE_DIR}/lauxlib.h"
  DESTINATION include)
