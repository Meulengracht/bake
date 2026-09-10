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

#ifndef __LIBPLATFORM_H__
#define __LIBPLATFORM_H__

#include <stddef.h>
#include <stdint.h>
#include <chef/list.h>

// detect platform
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
   //define something for Windows (32-bit and 64-bit, this part is common)
   #define CHEF_ON_WINDOWS 1
   #define CHEF_PLATFORM_STR "windows"
   #ifdef _WIN64
      //define something for Windows (64-bit only)
   #else
      //define something for Windows (32-bit only)
   #endif
#elif __MOLLENOS__
    #define CHEF_ON_VALI 1
    #define CHEF_PLATFORM_STR "vali"
#elif __APPLE__
    #define CHEF_ON_MACOS 1
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
    #define CHEF_ON_LINUX 1
    #define CHEF_PLATFORM_STR "linux"
#elif __unix__ // all unices not caught above
    #define CHEF_ON_LINUX 1
    #define CHEF_PLATFORM_STR "unix"
#elif defined(_POSIX_VERSION)
    #define CHEF_ON_LINUX 1
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
#include <unistd.h>
#else
#include <limits.h>
#endif

#ifdef _MSC_VER
#include <io.h>
#define fileno _fileno
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

/**
 * @brief Copy the final path component without its extension.
 *
 * Copying starts after the final platform path separator and stops at the
 * first period or the end of the component. The result is always NUL-terminated
 * when @p bufferSize is nonzero. Invalid arguments set @c errno to @c EINVAL.
 *
 * @param[in] path Path whose base name is required.
 * @param[out] buffer Destination for the base name.
 * @param[in] bufferSize Capacity of @p buffer, including the NUL terminator.
 */
extern void strbasename(const char* path, char* buffer, size_t bufferSize);

/**
 * @brief Join a base path and a NULL-terminated list of path components.
 *
 * Leading separators are removed from every appended component and exactly one
 * separator is inserted when the accumulated path does not already end in one.
 * The resulting path is limited to 4095 characters.
 *
 * @param[in] base Initial path. Must not be NULL.
 * @param[in] ... Path components terminated by a NULL pointer.
 * @return A heap-allocated joined path, or NULL on allocation failure, when
 *         @p base is NULL, or when the result is too long. The caller must free
 *         the returned string.
 */
extern char* strpathjoin(const char* base, ...);

/**
 * @brief Combine two path components using the platform path separator.
 *
 * One leading separator is removed from @p path2, and a separator is added only
 * when @p path1 does not already end in one. If either input is NULL or empty,
 * a duplicate of the other input is returned.
 *
 * @param[in] path1 Left-hand path, or NULL.
 * @param[in] path2 Right-hand path, or NULL.
 * @return A heap-allocated combined path, or NULL when both paths are NULL or
 *         allocation fails. The caller must free the returned string.
 */
extern char* strpathcombine(const char* path1, const char* path2);

/**
 * @brief Split a string at every occurrence of a separator character.
 *
 * Empty fields, including leading and trailing empty fields, are preserved.
 * Each field and the pointer vector are allocated separately, and the vector
 * is terminated by a NULL pointer.
 *
 * @param[in] text String to split. Must not be NULL.
 * @param[in] sep Character that separates fields.
 * @return A newly allocated NULL-terminated string vector, or NULL on invalid
 *         input or allocation failure. Release it with strsplit_free().
 */
extern char** strsplit(const char* text, char sep);

/**
 * @brief Release a string vector returned by strsplit().
 *
 * Passing NULL has no effect.
 *
 * @param[in] strings String vector to release.
 */
extern void strsplit_free(char** strings);

/**
 * @brief Replace every non-overlapping occurrence of a substring.
 *
 * The input string is read but not modified. A NULL @p replaceWith removes all
 * matches. An empty @p find value is rejected because it cannot be advanced
 * through safely.
 *
 * @param[in] text Source string.
 * @param[in] find Non-empty substring to replace.
 * @param[in] replaceWith Replacement text, or NULL to use an empty string.
 * @return A heap-allocated result, or NULL on invalid input or allocation
 *         failure. The caller must free the returned string.
 */
extern char* strreplace(char* text, const char* find, const char* replaceWith);

/**
 * @brief Compare the end of a string with a suffix.
 *
 * @param[in] text String to inspect.
 * @param[in] suffix Suffix to compare.
 * @return Zero when @p text ends with @p suffix; nonzero otherwise. A suffix
 *         longer than @p text returns -1.
 */
extern int strendswith(const char* text, const char* suffix);

/**
 * @brief Parse a supported textual Boolean value.
 *
 * Accepted true values are @c y, @c yes, @c true, and @c on; accepted false
 * values are @c n, @c no, @c false, and @c off. The common all-lowercase,
 * title-case, and all-uppercase forms are recognized.
 *
 * @param[in] string Boolean text to parse.
 * @return One for a recognized true value and zero for a recognized false
 *         value. An unrecognized value also returns zero and sets @c errno to
 *         @c EINVAL.
 */
extern int strbool(const char* string);

/**
 * @brief Join a NULL-terminated string vector with a separator.
 *
 * The separator is inserted only between values. An empty vector produces an
 * empty string. The reported length includes the terminating NUL byte.
 *
 * @param[in] values NULL-terminated vector of strings to join.
 * @param[in] sep Separator inserted between adjacent values.
 * @param[out] lengthOut Receives the allocation size, including the NUL byte.
 * @return A heap-allocated NUL-terminated string, or NULL on allocation
 *         failure. The caller must free the returned string.
 */
extern char* strflatten(const char* const* values, char* sep, size_t* lengthOut);

/**
 * @brief Parse a mutable flat command line into an argument vector.
 *
 * Whitespace separates arguments except inside matching single or double
 * quotes. Quotes are removed, empty quoted arguments are preserved, and no
 * shell expansion is performed. Backslashes are literal except before double
 * quotes, where Windows CRT escaping rules apply. The function inserts NUL
 * terminators into @p arguments, so that buffer must remain alive while the
 * result is used. Only the pointer vector is allocated.
 *
 * @param[in,out] arguments Mutable command text, or NULL for no parsed
 *                          arguments.
 * @param[in] arg0 Optional caller-owned value inserted at index zero.
 * @param[out] argc Optional destination for the number of returned arguments,
 *                  including @p arg0 when supplied. It is set to zero before
 *                  parsing starts.
 * @return A newly allocated NULL-terminated pointer vector, or NULL on
 *         allocation failure or unmatched quotes. An unmatched quote sets
 *         @c errno to @c EINVAL. Release the vector with strargv_free().
 */
extern char** strargv(char* arguments, const char* arg0, int* argc);

/**
 * @brief Release the pointer vector returned by strargv().
 *
 * This does not release @p arguments, @p arg0, or any strings referenced by
 * the vector. Passing NULL has no effect.
 *
 * @param[in] argv Argument pointer vector to release.
 */
extern void strargv_free(char** argv);

/**
 * @brief Serialize literal arguments using Windows CRT quoting rules.
 *
 * Every argument is quoted so whitespace, embedded quotes, and trailing
 * backslashes survive parsing by a compatible Windows C runtime. No shell
 * expansion is performed.
 *
 * @param[in] arguments NULL-terminated vector to serialize, or NULL for an
 *                      empty command line.
 * @return A heap-allocated NUL-terminated command line, or NULL on allocation
 *         or size-overflow failure. The caller must free the returned string.
 */
extern char* strargv_windows(const char* const* arguments);

#define FILTER_FOLDCASE 0x1

/**
 * @brief Match text against the platform glob-filter syntax.
 *
 * Supported operators include @c ? for one non-separator character, @c * for
 * characters within one path component, @c ** for recursive path matching,
 * bracket expressions, a leading @c ! for negation, and a backslash escape.
 *
 * @param[in] filter Pattern to evaluate.
 * @param[in] text Text to match against the pattern.
 * @param[in] flags Bitwise filter options; use @c FILTER_FOLDCASE for
 *                  case-insensitive matching.
 * @return Zero when the filter matches and -1 when it does not match or either
 *         required input is NULL.
 */
extern int strfilter(const char* filter, const char* text, int flags);

/**
 * @brief Create a directory and every missing parent directory.
 *
 * An existing directory is treated as success. On POSIX systems, newly
 * created directories receive mode 0755 regardless of the process umask.
 *
 * @param[in] path Directory path to create.
 * @return Zero on success and -1 on failure.
 */
extern int platform_mkdir(const char* path);

/**
 * @brief Recursively remove a directory and all of its contents.
 *
 * @param[in] path Directory tree to remove.
 * @return Zero on success and -1 on failure, including when @p path cannot be
 *         opened.
 */
extern int platform_rmdir(const char* path);

/**
 * @brief Check whether a path exists and identifies a directory.
 *
 * @param[in] path Filesystem path to inspect.
 * @return On POSIX, zero identifies a directory, a positive value identifies
 *         another file type, and -1 indicates a lookup failure. On Windows,
 *         one identifies a directory, zero identifies another file type, and
 *         -1 indicates a lookup failure.
 */
extern int platform_isdir(const char* path);

/**
 * @brief Read portable metadata for a filesystem entry without following a
 *        symbolic link on POSIX.
 *
 * Permissions use the low nine POSIX-style mode bits. Windows maps its limited
 * read, write, and execute flags onto those bits.
 *
 * @param[in] path Filesystem entry to inspect.
 * @param[out] stats Destination for the entry type, size, and permissions.
 * @return Zero on success and -1 on failure.
 */
extern int platform_stat(const char* path, struct platform_stat* stats);

/**
 * @brief Change the size of an open file.
 *
 * Enlarging the file extends it according to the host filesystem; shrinking it
 * discards data beyond the requested size.
 *
 * @param[in] fd Open file descriptor.
 * @param[in] size Requested size in bytes.
 * @return Zero on success and -1 on failure.
 */
extern int platform_chsize(int fd, long size);

/**
 * @brief Read the stored target of a symbolic link.
 *
 * @param[in] path Symbolic-link path to inspect.
 * @param[out] bufferOut Receives a heap-allocated NUL-terminated target path on
 *                       success. The caller must free it.
 * @return Zero on success and -1 on invalid input, allocation failure, or when
 *         the target cannot be read.
 */
extern int platform_readlink(const char* path, char** bufferOut);

/**
 * @brief Create or replace a symbolic link.
 *
 * If the target does not exist, the implementation creates a placeholder file
 * or directory before creating the link. An existing link at @p path is
 * removed and recreated.
 *
 * @param[in] path Path at which to create the symbolic link.
 * @param[in] target Stored target path. Relative targets are interpreted from
 *                   the link's parent directory when creating a placeholder.
 * @param[in] directory Nonzero when the target is a directory; zero for a file.
 * @return Zero on success and -1 on failure.
 */
extern int platform_symlink(const char* path, const char* target, int directory);

/**
 * @brief Remove a filesystem entry using the host unlink operation.
 *
 * @param[in] path Entry to remove.
 * @return Zero on success and -1 on failure.
 */
extern int platform_unlink(const char* path);

/**
 * @brief Resolve a path to an allocated absolute path.
 *
 * POSIX resolution follows symbolic links and requires the path to be
 * resolvable. Windows expands the path through @c GetFullPathNameA.
 *
 * @param[in] path Path to resolve.
 * @return A heap-allocated absolute path, or NULL on failure. The caller must
 *         free the returned string.
 */
extern char* platform_abspath(const char* path);

/**
 * @brief Read the process's current working directory.
 *
 * @param[out] buffer Destination buffer for the NUL-terminated path.
 * @param[in] length Capacity of @p buffer in bytes.
 * @return Zero on success and -1 when the directory cannot be read or the
 *         buffer is too small.
 */
extern int platform_getcwd(char* buffer, size_t length);

/**
 * @brief Read the platform-specific per-user data directory.
 *
 * POSIX returns the current user's home directory. Windows returns the local
 * application-data directory.
 *
 * @param[out] buffer Destination buffer for the path.
 * @param[in] length Capacity of @p buffer in bytes. Callers should provide
 *                   enough space for the complete path and its NUL terminator.
 * @return Zero on success and -1 on failure.
 */
extern int platform_getuserdir(char* buffer, size_t length);

/**
 * @brief Change the process's current working directory.
 *
 * @param[in] path Directory that becomes the process working directory.
 * @return Zero on success and -1 on failure.
 */
extern int platform_chdir(const char* path);

/**
 * @brief Change the permission bits of a filesystem entry.
 *
 * Windows supports only the subset representable by its C runtime.
 *
 * @param[in] path Filesystem entry to update.
 * @param[in] permissions POSIX-style permission mask.
 * @return Zero on success and -1 on invalid input or host-operation failure.
 */
extern int platform_chmod(const char* path, uint32_t permissions);

/**
 * @brief Enumerate files beneath a directory into a caller-owned list.
 *
 * The caller must initialize @p files with list_init() before calling. Each
 * appended item is a platform_file_entry. A recursive enumeration descends
 * into directories and reports their contents rather than the directories
 * themselves. A missing root directory produces an empty successful result.
 *
 * @param[in] path Root directory to enumerate.
 * @param[in] recursive Nonzero to descend into subdirectories; zero to list
 *                      only direct children.
 * @param[in,out] files Initialized list that receives allocated entries.
 * @return Zero on success and -1 on invalid input or enumeration failure.
 *         Release all appended entries with platform_getfiles_destroy().
 */
extern int platform_getfiles(const char* path, int recursive, struct list* files);

/**
 * @brief Release every entry produced by platform_getfiles().
 *
 * The list is reinitialized after its entries and their strings are freed.
 * Passing NULL has no effect.
 *
 * @param[in,out] files List whose platform_file_entry items are released.
 */
extern void platform_getfiles_destroy(struct list* files);

/**
 * @brief Return the number of logical processors available to the system.
 *
 * @return A positive processor count on success, or -1 when the host query
 *         fails.
 */
extern int platform_cpucount(void);

/**
 * @brief Copy one file's contents to a destination path.
 *
 * The destination is created or truncated. File metadata is not preserved.
 *
 * @param[in] source Existing source file.
 * @param[in] destination Destination file to create or replace.
 * @return Zero on success and -1 on open, allocation, read, or write failure.
 */
extern int platform_copyfile(const char* source, const char* destination);

/**
 * @brief Recursively copy a directory tree.
 *
 * Missing destination directories are created and regular files are copied or
 * replaced. Symbolic links and unsupported entry types are skipped.
 *
 * @param[in] source Existing source directory.
 * @param[in] destination Destination directory.
 * @return Zero on success and -1 on invalid input or copy failure.
 */
extern int platform_copydir(const char* source, const char* destination);

/**
 * @brief Return the native error from the most recent Windows directory copy.
 *
 * @return The saved Windows error code, or zero when no Windows copy error is
 *         available. POSIX builds always return zero.
 */
extern unsigned long platform_copydir_lasterror(void);

/**
 * @brief Describe the operation that failed during the most recent Windows
 *        directory copy.
 *
 * @return A borrowed static operation description, or NULL when unavailable.
 *         The caller must not free or modify the returned string. POSIX builds
 *         always return NULL.
 */
extern const char* platform_copydir_lasterror_operation(void);

/**
 * @brief Read an entire binary file into memory.
 *
 * The returned bytes are not followed by an additional NUL terminator.
 *
 * @param[in] path File to read.
 * @param[out] bufferOut Receives the allocated file contents on success. The
 *                       caller must free this buffer.
 * @param[out] lengthOut Receives the exact number of bytes read.
 * @return Zero on success and -1 on invalid input, open, allocation, seek, or
 *         read failure.
 */
extern int platform_readfile(const char* path, void** bufferOut, size_t* lengthOut);
extern int platform_readtext(const char* path, char** bufferOut, size_t* lengthOut);

/**
 * @brief Replace a text file with a NUL-terminated string's contents.
 *
 * The terminating NUL byte is not written.
 *
 * @param[in] path File to create or truncate.
 * @param[in] text Text to write.
 * @return Zero on success and -1 on invalid input, open, write, or close
 *         failure.
 */
extern int platform_writetextfile(const char* path, const char* text);

/**
 * @brief Create a unique temporary directory for Chef.
 *
 * @return A heap-allocated path to the newly created directory, or NULL on
 *         failure. The caller must remove the directory when finished and free
 *         the returned string.
 */
extern char* platform_tmpdir(void);

/**
 * @brief Duplicate a NUL-terminated string using the platform allocator.
 *
 * @param[in] string String to duplicate.
 * @return A heap-allocated duplicate, or NULL on failure. The caller must free
 *         the returned string.
 */
extern char* platform_strdup(const char* string);

/**
 * @brief Duplicate at most a specified number of string characters.
 *
 * The returned copy is always NUL-terminated.
 *
 * @param[in] string String to duplicate.
 * @param[in] maxlen Maximum number of characters to copy, excluding the NUL
 *                   terminator.
 * @return A heap-allocated duplicate, or NULL on failure. The caller must free
 *         the returned string.
 */
extern char* platform_strndup(const char* string, size_t maxlen);

/**
 * @brief Suspend the calling thread for approximately the requested duration.
 *
 * Signals may interrupt the sleep on POSIX systems.
 *
 * @param[in] milliseconds Minimum requested delay in milliseconds.
 * @return Zero after the full delay, or -1 when a POSIX sleep is interrupted or
 *         rejected. Windows returns zero after sleeping.
 */
extern int platform_sleep(unsigned int milliseconds);

enum platform_spawn_output_type {
    PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT,
    PLATFORM_SPAWN_OUTPUT_TYPE_STDERR
};

/**
 * @brief Receive output captured from a spawned process.
 *
 * The provided text is temporary and is valid only for the duration of the
 * callback. Complete lines include their trailing newline; a final partial line
 * may not. The callback executes synchronously inside platform_spawn() or
 * platform_spawn_argv().
 *
 * @param[in] line NUL-terminated output fragment.
 * @param[in] type Stream from which the fragment was read.
 */
typedef void (*platform_spawn_output_handler)(const char* line,
    enum platform_spawn_output_type type);

/** @brief Optional controls for process spawning. */
struct platform_spawn_options {
    /** Child working directory, or NULL to use the default. */
    const char* cwd;

    /** POSIX argv[0] override, or NULL to use the executable path. */
    const char* argv0;

    /** Optional synchronous callback for captured stdout and stderr. */
    platform_spawn_output_handler output_handler;
};

/**
 * @brief Spawn a process from a legacy flat argument string and wait for it.
 *
 * No command shell is invoked. POSIX builds parse @p arguments with strargv();
 * Windows builds pass it as a pre-serialized CRT command line. Prefer
 * platform_spawn_argv() when individual arguments must remain literal. When an
 * output handler is supplied, stdout and stderr are captured and delivered
 * before this function returns.
 *
 * @param[in] path Executable name or path. Must not be NULL.
 * @param[in] arguments Flat argument text excluding argv[0], or NULL for no
 *                      arguments.
 * @param[in] envp Optional NULL-terminated @c NAME=VALUE environment vector.
 * @param[in] options Optional spawn controls. Inputs remain caller-owned.
 * @return Zero when the child exits successfully, -1 when the child cannot be
 *         launched or managed, and another nonzero platform-specific status
 *         when the child fails.
 */
extern int platform_spawn(const char* path, const char* arguments,
    const char* const* envp, struct platform_spawn_options* options);

/**
 * @brief Spawn a process with literal arguments and wait for it.
 *
 * The argument vector is converted to the host process format without shell
 * parsing or expansion. argv[0] comes from @p options when a POSIX override is
 * supplied; otherwise the executable path is used.
 *
 * @param[in] path Executable name or path. Must not be NULL.
 * @param[in] arguments NULL-terminated argument vector excluding argv[0], or
 *                      NULL for no arguments.
 * @param[in] envp Optional NULL-terminated @c NAME=VALUE environment vector.
 * @param[in] options Optional spawn controls. Inputs remain caller-owned.
 * @return Zero when the child exits successfully, -1 when the child cannot be
 *         launched or managed, and another nonzero platform-specific status
 *         when the child fails.
 */
extern int platform_spawn_argv(const char* path, const char* const* arguments,
    const char* const* envp, struct platform_spawn_options* options);

/**
 * @brief Execute a command and capture its standard output.
 *
 * The POSIX implementation captures at most 4096 bytes. Output handling and
 * command interpretation otherwise follow the host implementation.
 *
 * @param[in] cmd Command text to execute.
 * @return A heap-allocated output buffer, or NULL when execution fails or no
 *         output is read. The caller must free the returned buffer.
 */
extern char* platform_exec(const char* cmd);

/**
 * @brief Generate a canonical uppercase GUID string using rand().
 *
 * This function is not cryptographically secure. The caller is responsible for
 * seeding rand() when nondeterministic output is required.
 *
 * @param[out] strbuffer Destination with room for the 36-character GUID and
 *                       its NUL terminator.
 */
extern void platform_guid_new_string(char strbuffer[40]);

/**
 * @brief Generate a 16-byte GUID value using rand().
 *
 * This function is not cryptographically secure. The caller is responsible for
 * seeding rand() when nondeterministic output is required.
 *
 * @param[out] guid Destination for the 16-byte GUID.
 */
extern void platform_guid_new(unsigned char guid[16]);

/**
 * @brief Parse a canonical textual GUID into its 16-byte representation.
 *
 * Both uppercase and lowercase hexadecimal digits are accepted. The input must
 * contain 36 characters with hyphens at the canonical offsets. Invalid input
 * leaves @p guid unchanged.
 *
 * @param[out] guid Destination for the parsed 16-byte GUID.
 * @param[in] str NUL-terminated canonical GUID string.
 */
extern void platform_guid_parse(unsigned char guid[16], const char* str);

/**
 * @brief Fills the buffer with cryptographically strong random bytes.
 *
 * This is implemented using OS-provided RNG facilities (Linux: getrandom(2)
 * with /dev/urandom fallback; Windows: BCryptGenRandom).
 *
 * @param[out] buffer Destination buffer.
 * @param[in] length Number of bytes to write. Zero is accepted when @p buffer
 *                   is non-NULL.
 * @return Zero on success and -1 on failure, with @c errno set.
 */
extern int platform_secure_random_bytes(void* buffer, size_t length);

/**
 * @brief Generates a cryptographically strong random string consisting only
 * of characters in the set [0-9A-Z].
 *
 * The output is NUL-terminated. The caller must provide a buffer of at least
 * (length + 1) bytes.
 *
 * @param[out] out Destination with capacity for at least @p length + 1 bytes.
 * @param[in] length Number of random characters to generate.
 * @return Zero on success and -1 on failure, with @c errno set.
 */
extern int platform_secure_random_string(char* out, size_t length);

/**
 * @brief Allocates and returns a new NUL-terminated random [0-9A-Z] string.
 *
 * @param[in] length Number of random characters to generate.
 * @return A heap-allocated string on success, or NULL on failure. The caller
 *         must free the returned string.
 */
extern char* platform_secure_random_string_new(size_t length);

#ifdef __cplusplus
}
#endif

#endif //!__LIBPLATFORM_H__
