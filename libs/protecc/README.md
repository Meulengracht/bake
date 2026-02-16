# Protecc - Path Pattern Matching Library for eBPF

Protecc is a C library designed to compile and optimize path patterns with wildcards and simple regex into a binary format optimized for fast evaluation in eBPF programs. The library provides efficient pattern matching suitable for security policies in containerized environments.

## Features

- **Wildcard Support**
  - `?` - matches any single character
  - `*` - matches any sequence of characters (does not cross directory boundaries)
  - `**` - matches any sequence including `/` (recursive directory match)

- **Character Sets and Ranges**
  - `[abc]` - matches any character in the set
  - `[a-z]` - matches any character in the range
  - `[0-9]` - matches any digit
  - Multiple ranges: `[a-zA-Z0-9]`

- **Modifiers**
  - `?` - zero or one (optional)
  - `+` - one or more
  - `*` - zero or more
  - Can be applied to character sets: `[0-9]+`, `[a-z]*`

- **Optimized for eBPF**
  - Trie-based compilation for fast matching
  - Binary export format for loading in constrained environments
  - Minimal memory footprint
  - O(n) matching complexity where n is path length

## Examples

```c
#include <protecc/protecc.h>

// Compile patterns
const char* patterns[] = {
    "/etc/passwd",           // Exact match
    "/tmp/*",                // Wildcard in directory
    "/home/**",              // Recursive match
    "/var/log/[a-z]*.log",  // Character range + wildcard
    "/dev/tty[0-9]+",       // Digit range with modifier
};

protecc_compiled_t* compiled;
protecc_error_t err = protecc_compile(
    patterns,
    5,
    PROTECC_FLAG_OPTIMIZE,
    &compiled
);

if (err != PROTECC_OK) {
    fprintf(stderr, "Compilation failed: %s\n", protecc_error_string(err));
    return 1;
}

// Match paths
bool match1 = protecc_match(compiled, "/etc/passwd", 0);           // true
bool match2 = protecc_match(compiled, "/tmp/test.txt", 0);         // true
bool match3 = protecc_match(compiled, "/home/user/doc.txt", 0);    // true
bool match4 = protecc_match(compiled, "/var/log/system.log", 0);   // true
bool match5 = protecc_match(compiled, "/dev/tty0", 0);             // true
bool match6 = protecc_match(compiled, "/usr/bin/ls", 0);           // false

// Export to binary format for eBPF
size_t size;
protecc_export(compiled, NULL, 0, &size);  // Get required size

void* buffer = malloc(size);
protecc_export(compiled, buffer, size, &size);

// ... load buffer into eBPF program ...

// Cleanup
protecc_free(compiled);
free(buffer);
```

## Pattern Syntax

### Exact Paths
```
/etc/passwd
/usr/bin/sh
```

### Single Character Wildcard
```
/tmp/file?        # matches /tmp/file1, /tmp/filea, etc.
/dev/tty?         # matches /dev/tty0, /dev/ttyS, etc.
```

### Multi Character Wildcard (no `/`)
```
/tmp/*.txt        # matches /tmp/file.txt but not /tmp/dir/file.txt
/var/log/*        # matches files in /var/log only
```

### Recursive Wildcard
```
/home/**          # matches everything under /home
/var/log/**/*.log # matches .log files anywhere under /var/log
```

### Character Ranges
```
/tmp/[a-z]        # matches /tmp/a through /tmp/z
/dev/tty[0-9]     # matches /dev/tty0 through /dev/tty9
/tmp/[A-Z]*       # matches /tmp/A*, /tmp/B*, etc.
```

### Character Sets
```
/tmp/[abc]        # matches /tmp/a, /tmp/b, /tmp/c
/dev/[xyz][0-9]   # matches /dev/x0, /dev/y5, etc.
```

### Modifiers
```
/dev/tty[0-9]?    # optional digit: /dev/tty, /dev/tty0
/dev/tty[0-9]+    # one or more digits: /dev/tty0, /dev/tty123
/tmp/[a-z]*       # zero or more letters: /tmp/, /tmp/abc
```

## API Reference

### Core Functions

#### `protecc_compile`
Compile an array of pattern strings into an optimized trie structure.

```c
protecc_error_t protecc_compile(
    const char** patterns,      // Array of pattern strings
    size_t count,               // Number of patterns
    uint32_t flags,             // Compilation flags
    protecc_compiled_t** compiled  // Output compiled pattern set
);
```

Flags:
- `PROTECC_FLAG_NONE` - No special options
- `PROTECC_FLAG_CASE_INSENSITIVE` - Case-insensitive matching
- `PROTECC_FLAG_OPTIMIZE` - Enable optimizations (default)

#### `protecc_match`
Match a path against compiled patterns.

```c
bool protecc_match(
    const protecc_compiled_t* compiled,  // Compiled pattern set
    const char* path,                    // Path to match
    size_t path_len                      // Length (0 = use strlen)
);
```

#### `protecc_compile_with_permissions`
Compile patterns with explicit read/write/execute permissions.

```c
protecc_pattern_t patterns[] = {
    {"/etc/**", PROTECC_PERM_READ},
    {"/tmp/**", PROTECC_PERM_READ | PROTECC_PERM_WRITE},
};
protecc_compile_with_permissions(patterns, 2, PROTECC_FLAG_NONE, &compiled);
```

#### `protecc_match_with_permissions`
Match a path and require specific permissions.

```c
bool allowed = protecc_match_with_permissions(
    compiled,
    "/tmp/file.txt",
    0,
    PROTECC_PERM_WRITE
);
```

#### `protecc_export`
Export compiled patterns to a flat binary profile for eBPF:
- header (`magic`, `version`, `flags`, `pattern_count`)
- repeated entries (`permissions`, `pattern_len`, raw pattern bytes)

```c
protecc_error_t protecc_export(
    const protecc_compiled_t* compiled,
    void* buffer,           // Output buffer (NULL to query size)
    size_t buffer_size,     // Size of buffer
    size_t* bytes_written   // Actual bytes written
);
```

#### `protecc_import`
Import patterns from binary format.

```c
protecc_error_t protecc_import(
    const void* buffer,
    size_t buffer_size,
    protecc_compiled_t** compiled
);
```

#### `protecc_free`
Free compiled pattern set.

```c
void protecc_free(protecc_compiled_t* compiled);
```

### Utility Functions

#### `protecc_validate_pattern`
Validate a pattern string before compilation.

```c
protecc_error_t protecc_validate_pattern(const char* pattern);
```

#### `protecc_get_stats`
Get statistics about compiled patterns.

```c
protecc_error_t protecc_get_stats(
    const protecc_compiled_t* compiled,
    protecc_stats_t* stats
);
```

#### `protecc_error_string`
Get human-readable error message.

```c
const char* protecc_error_string(protecc_error_t error);
```

## Building

The library is built as part of the bake project using CMake:

```bash
cd /path/to/bake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To run tests:

```bash
cmake --build build --target protecc_test
./build/libs/protecc/protecc_test
```

## Integration with eBPF

The library is designed to work with containerv's eBPF security policies:

1. **Compilation Phase** (user space)
   - Compile patterns using `protecc_compile()`
   - Export to binary format using `protecc_export()`

2. **Loading Phase** (eBPF initialization)
   - Load binary format into eBPF maps
   - Use optimized trie structure for fast lookups

3. **Evaluation Phase** (eBPF program)
   - Match paths in O(n) time using pre-compiled trie
   - Minimal memory access patterns
   - Suitable for LSM hooks and syscall filters

## Performance

The library is optimized for:
- **Fast compilation**: Patterns are compiled once, matched many times
- **Efficient matching**: O(n) complexity where n is the path length
- **Low memory**: Trie structure shares common prefixes
- **eBPF-friendly**: Binary format suitable for constrained environments

Typical performance:
- Compilation: ~1-10ms for 100-1000 patterns
- Matching: ~100-500ns per path (depending on pattern complexity)
- Memory: ~50-200 bytes per pattern (depending on sharing)

## Use Cases

- **Container Security**: Path-based access control in containerv
- **Syscall Filtering**: Restrict file access in seccomp/eBPF policies
- **Audit Systems**: Fast path matching for security auditing
- **File Monitoring**: Efficient filtering of filesystem events

## License

GNU General Public License v3.0 - See LICENSE file for details

## Contributing

When contributing:
1. Maintain backwards compatibility
2. Add tests for new features
3. Update documentation
4. Ensure eBPF compatibility
