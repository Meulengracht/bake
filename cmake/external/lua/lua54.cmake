# original credits to the files in this folder
# https://gitea.osmocom.org/osmocom/wireshark/commit/687b24d5b3d3a386591040e5fda5f2e659f95e99

if(MSVC)
	# Prefer nmake in MSVC environments to avoid accidentally picking up MSYS/MinGW gmake.
	find_program(MAKE_EXE NAMES nmake)
else()
	find_program(MAKE_EXE NAMES gmake make nmake)
endif()
include(ExternalProject)

set(_staging_dir "${CMAKE_BINARY_DIR}/staging")

if(MINGW)
	set(_target mingw)
	set(_mycflags "-fPIC")
elseif(CMAKE_SYSTEM_NAME MATCHES Linux)
	set(_target linux)
	set(_mycflags "-fPIC")
elseif(UNIX)
	set(_target posix)
	set(_mycflags "-fPIC")
elseif(MSVC)
	set(_target generic)
	set(_mycflags "")
else()
	set(_target generic)
	set(_mycflags "")
endif()

if(MSVC)
	set(_lib_name "lua54.lib")
else()
	set(_lib_name "liblua.a")
endif()

set(HAVE_LUA TRUE)
set(LUA_INCLUDE_DIRS "${_staging_dir}/include")
set(LUA_LIBRARIES "${_staging_dir}/lib/${_lib_name}")
set(LUA_FOUND TRUE CACHE INTERNAL "")

#
# The install patch isn't strictly necessary for Lua but it's cleaner to install
# external projects to a staging directory first, and the normal install target
# does not work with MinGW.
#

if(MSVC)
	# MSVC must not build Lua via gmake/gcc, otherwise we get MinGW runtime symbols
	# like __mingw_fprintf inside liblua.a which MSVC cannot resolve.
	# Build Lua with cl/lib via a tiny CMake wrapper.
	set(_lua_msvc_build_dir "${CMAKE_BINARY_DIR}/lua54-msvc-build")
	ExternalProject_Add(lua54
		URL
		  https://www.lua.org/ftp/lua-5.4.7.tar.gz
		# No patching needed; wrapper compiles sources directly.
		PATCH_COMMAND
		  ""
		CONFIGURE_COMMAND
		  ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_LIST_DIR}/msvc-cmake -B ${_lua_msvc_build_dir}
		    -DLUA_SOURCE_DIR=<SOURCE_DIR>/src
		    -DCMAKE_INSTALL_PREFIX=${_staging_dir}
		    -DCMAKE_BUILD_TYPE=$<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},Release>
		BUILD_COMMAND
		  ${CMAKE_COMMAND} --build ${_lua_msvc_build_dir} --config $<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},Release>
		BUILD_BYPRODUCTS
		  ${LUA_LIBRARIES}
		INSTALL_COMMAND
		  ${CMAKE_COMMAND} --install ${_lua_msvc_build_dir} --config $<IF:$<BOOL:${CMAKE_BUILD_TYPE}>,${CMAKE_BUILD_TYPE},Release>
	)
else()
	ExternalProject_Add(lua54
		URL
		  https://www.lua.org/ftp/lua-5.4.7.tar.gz
		PATCH_COMMAND
		  patch -p1 < ${CMAKE_CURRENT_LIST_DIR}/0001-install-static-target.patch
		CONFIGURE_COMMAND
		  ""
		BUILD_COMMAND
		  ${MAKE_EXE} MYCFLAGS=-fPIC CC=${CMAKE_C_COMPILER} "AR=${CMAKE_C_COMPILER_AR} rcu" RANLIB=${CMAKE_C_COMPILER_RANLIB} ${_target}
		BUILD_IN_SOURCE
		  True
		BUILD_BYPRODUCTS
		  ${LUA_LIBRARIES}
		INSTALL_COMMAND
		  ${MAKE_EXE} INSTALL_TOP=${_staging_dir} install-static
	)
endif()
