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

#ifndef __LIBPLATFORM_H__
#define __LIBPLATFORM_H__

#include <stddef.h>
#include <stdint.h>
#include <chef/list.h>

// detect platform
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
   //define something for Windows (32-bit and 64-bit, this part is common)
   #define CHEF_PLATFORM_STR "windows"
   #ifdef _WIN64
      //define something for Windows (64-bit only)
   #else
      //define something for Windows (32-bit only)
   #endif
#elif __MOLLENOS__
    #define CHEF_PLATFORM_STR "vali"
#elif __APPLE__
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR
         // iOS, tvOS, or watchOS Simulator
         #define CHEF_PLATFORM_STR "ios-simulator"
    #elif TARGET_OS_MACCATALYST
         // Mac's Catalyst (ports iOS API into Mac, like UIKit).
         #define CHEF_PLATFORM_STR "ios-catalyst"
    #elif TARGET_OS_IPHONE
        // iOS, tvOS, or watchOS device
         #define CHEF_PLATFORM_STR "ios"
    #elif TARGET_OS_MAC
        // Other kinds of Apple platforms
         #define CHEF_PLATFORM_STR "mac"
    #else
    #   error "Unknown Apple platform"
    #endif
#elif __linux__
    #define CHEF_PLATFORM_STR "linux"
#elif __unix__ // all unices not caught above
    #define CHEF_PLATFORM_STR "unix"
#elif defined(_POSIX_VERSION)
    #define CHEF_PLATFORM_STR "posix"
#else
#   error "Unknown compiler"
#endif

// detect architecture
#if defined(__x86_64__) || defined(_M_X64)
#define CHEF_ARCHITECTURE_STR "amd64"
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
#define CHEF_ARCHITECTURE_STR "i386"
#elif defined(__ARM_ARCH_2__)
#define CHEF_ARCHITECTURE_STR "arm2"
#elif defined(__ARM_ARCH_3__) || defined(__ARM_ARCH_3M__)
#define CHEF_ARCHITECTURE_STR "arm3"
#elif defined(__ARM_ARCH_4T__) || defined(__TARGET_ARM_4T)
#define CHEF_ARCHITECTURE_STR "arm4t"
#elif defined(__ARM_ARCH_5_) || defined(__ARM_ARCH_5E_)
#define CHEF_ARCHITECTURE_STR "arm5"
#elif defined(__ARM_ARCH_6T2_) || defined(__ARM_ARCH_6T2_)
#define CHEF_ARCHITECTURE_STR "arm6t2"
#elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__)
#define CHEF_ARCHITECTURE_STR "arm6"
#elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#define CHEF_ARCHITECTURE_STR "arm7"
#elif defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#define CHEF_ARCHITECTURE_STR "arm7a"
#elif defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__)
#define CHEF_ARCHITECTURE_STR "arm7r"
#elif defined(__ARM_ARCH_7M__)
#define CHEF_ARCHITECTURE_STR "arm7m"
#elif defined(__ARM_ARCH_7S__)
#define CHEF_ARCHITECTURE_STR "arm7s"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CHEF_ARCHITECTURE_STR "arm64"
#elif defined(mips) || defined(__mips__) || defined(__mips)
#define CHEF_ARCHITECTURE_STR "mips"
#elif defined(__sh__)
#define CHEF_ARCHITECTURE_STR "superh"
#elif defined(__powerpc) || defined(__powerpc__) || defined(__powerpc64__) || defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC)
#define CHEF_ARCHITECTURE_STR "powerpc"
#elif defined(__PPC64__) || defined(__ppc64__) || defined(_ARCH_PPC64)
#define CHEF_ARCHITECTURE_STR "powerpc64"
#elif defined(__sparc__) || defined(__sparc)
#define CHEF_ARCHITECTURE_STR "sparc"
#elif defined(__m68k__)
#define CHEF_ARCHITECTURE_STR "m68k"
#elif defined(__riscv32)
#define CHEF_ARCHITECTURE_STR "riscv32"
#elif defined(__riscv64)
#define CHEF_ARCHITECTURE_STR "riscv64"
#else
#define CHEF_ARCHITECTURE_STR "unknown"
#endif

#if defined(__linux__) || defined(__APPLE__) || \
    defined(__MOLLENOS__) || defined(__unix__) || \
    defined(__posix__)
#define CHEF_PATH_SEPARATOR   '/'
#define CHEF_PATH_SEPARATOR_S "/"
#else
#define CHEF_PATH_SEPARATOR   '\\'
#define CHEF_PATH_SEPARATOR_S "\\"
#endif

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#if defined(_WIN32) || defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef PATH_MAX
#define PATH_MAX MAX_PATH
#elif __linux__
#include <linux/limits.h>
#else
#include <limits.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct chef_keypair_item {
    struct list_item list_header;
    const char*      key;
    const char*      value;
};

enum platform_filetype {
    PLATFORM_FILETYPE_DIRECTORY,
    PLATFORM_FILETYPE_FILE,
    PLATFORM_FILETYPE_SYMLINK,
    PLATFORM_FILETYPE_UNKNOWN
};

struct platform_stat {
    enum platform_filetype type;
    uint64_t               size;
    uint32_t               permissions;
};

struct platform_file_entry {
    struct list_item       list_header;
    char*                  name;
    enum platform_filetype type;
    char*                  path;
    char*                  sub_path;
};

extern void   strbasename(const char* path, char* buffer, size_t bufferSize);
extern char*  strpathjoin(const char* base, ...);
extern char*  strpathcombine(const char* path1, const char* path2);
extern char** strsplit(const char* text, char sep);
extern void   strsplit_free(char** strings);
extern char*  strreplace(char* text, const char* find, const char* replaceWith);
extern int    strendswith(const char* text, const char* suffix);
extern int    strbool(const char* string);

#define FILTER_FOLDCASE 0x1

/**
 * @brief Supports most of the glob patterns from the POSIX standard. The
 * following wildcards are supported: ?, *,  \\, !
 * 
 * @param filter 
 * @param text 
 * @param flags 
 * @return int returns 0 if filter was a match, -1 if not
 */
extern int strfilter(const char* filter, const char* text, int flags);

/**
 * @brief Recursively creates the provided directory path, if
 * the directory already exists nothing happens.
 * 
 * @param[In] path The path to create 
 * @return int 0 on success, -1 on error
 */
extern int platform_mkdir(const char* path);

/**
 * @brief Recursively deletes the provided directory path, if the directory
 * does not exist nothing happens, but it returns with -1.
 * 
 * @param[In] path The path to delete
 * @return int 0 on success, -1 on error
 */
extern int platform_rmdir(const char* path);

/**
 * @brief Check whether the path exists and is a directory
 * 
 * @param[In] path The path to check
 * @#define CHEF_ARCHITECTURE_STR int 0 if the path exists and is a directory, -1 otherwise
 */
extern int platform_isdir(const char* path);
extern int platform_stat(const char* path, struct platform_stat* stats);
extern int platform_chsize(int fd, long size);
extern int platform_readlink(const char* path, char** bufferOut);
extern int platform_symlink(const char* path, const char* target, int directory);
extern int platform_unlink(const char* path);
extern char* platform_abspath(const char* path);
extern int platform_getcwd(char* buffer, size_t length);
extern int platform_getuserdir(char* buffer, size_t length);
extern int platform_chmod(const char* path, uint32_t permissions);
extern int platform_getfiles(const char* path, int recursive, struct list* files);
extern int platform_getfiles_destroy(struct list* files);
extern int platform_cpucount(void);
extern int platform_copyfile(const char* source, const char* destination);
extern int platform_lockfile(int fd);
extern int platform_unlockfile(int fd);

/**
 * @brief 
 * 
 * @param milliseconds 
 * @#define CHEF_ARCHITECTURE_STR int 
 */
extern int platform_sleep(unsigned int milliseconds);

enum platform_spawn_output_type {
    PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT,
    PLATFORM_SPAWN_OUTPUT_TYPE_STDERR
};

typedef void (*platform_spawn_output_handler)(const char* line, enum platform_spawn_output_type type);

struct platform_spawn_options {
    // cwd allows the possibility of spawning the process with
    // a new working directory instead of the one of the host.
    const char* cwd;
    // argv0 allows the caller to override the argv[0] argument used
    // for spawning the process.
    const char* argv0;
    // output_handler if provided will allow the spawner to handle
    // line output by the child process.
    platform_spawn_output_handler output_handler;
};

/**
 * @brief Spawns a new process, and waits for the process to complete. 
 * 
 * @param[In] path      The path to the executable 
 * @param[In] arguments The arguments to pass to the executable
 * @param[In] envp      The environment variables to pass to the executable
 * @param[In] options   Options to customize how the spawn must be handled.
 * @#define CHEF_ARCHITECTURE_STR int 0 on success, -1 on error
 */
extern int platform_spawn(const char* path, const char* arguments, const char* const* envp, struct platform_spawn_options* options);

/**
 * @brief Spawns a child process and returns the stdout as a string.
 */
extern char* platform_exec(const char* cmd);

/**
 * @brief Execute the provided shell script.
 * 
 * @param[In] script The shell script to execute 
 * @return int 
 */
extern int platform_script(const char* script);

#ifdef __cplusplus
}
#endif

#endif //!__LIBPLATFORM_H__
