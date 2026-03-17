#!/usr/bin/env bash
# assert.sh — lightweight assertion helpers
#
# Each function prints a descriptive error on failure and returns 1.
# On success, the function returns 0 (no output).

# Assert that a previous command's exit code was 0.
# Usage:  assert_success EXIT_CODE DESCRIPTION
assert_success() {
    local rc="$1"
    local desc="${2:-command}"
    if [[ "$rc" -ne 0 ]]; then
        echo "ASSERT FAILED: '$desc' exited with code $rc (expected 0)" >&2
        return 1
    fi
    return 0
}

# Assert that a previous command's exit code was non-zero.
# Usage:  assert_failure EXIT_CODE DESCRIPTION
assert_failure() {
    local rc="$1"
    local desc="${2:-command}"
    if [[ "$rc" -eq 0 ]]; then
        echo "ASSERT FAILED: '$desc' exited with code 0 (expected non-zero)" >&2
        return 1
    fi
    return 0
}

# Assert that a file exists and is a regular file.
# Usage:  assert_file_exists PATH DESCRIPTION
assert_file_exists() {
    local path="$1"
    local desc="${2:-$path}"
    if [[ ! -f "$path" ]]; then
        echo "ASSERT FAILED: file does not exist: $desc ($path)" >&2
        return 1
    fi
    return 0
}

# Assert that a file exists and is non-empty.
# Usage:  assert_file_nonempty PATH DESCRIPTION
assert_file_nonempty() {
    local path="$1"
    local desc="${2:-$path}"
    if [[ ! -s "$path" ]]; then
        echo "ASSERT FAILED: file is missing or empty: $desc ($path)" >&2
        return 1
    fi
    return 0
}

# Assert that a directory exists.
# Usage:  assert_dir_exists PATH DESCRIPTION
assert_dir_exists() {
    local path="$1"
    local desc="${2:-$path}"
    if [[ ! -d "$path" ]]; then
        echo "ASSERT FAILED: directory does not exist: $desc ($path)" >&2
        return 1
    fi
    return 0
}

# Assert that a string contains a substring.
# Usage:  assert_contains HAYSTACK NEEDLE DESCRIPTION
assert_contains() {
    local haystack="$1"
    local needle="$2"
    local desc="${3:-output}"
    if [[ "$haystack" != *"$needle"* ]]; then
        echo "ASSERT FAILED: '$desc' does not contain expected substring" >&2
        echo "  expected: $needle" >&2
        echo "  actual:   $haystack" >&2
        return 1
    fi
    return 0
}

# Assert that the output of 'serve list' contains a package name.
# Usage:  assert_package_listed OUTPUT PACKAGE_NAME
assert_package_listed() {
    local output="$1"
    local package="$2"
    if ! echo "$output" | grep -qF "$package"; then
        echo "ASSERT FAILED: package '$package' not found in 'serve list' output" >&2
        echo "  output was:" >&2
        echo "$output" | sed 's/^/    /' >&2
        return 1
    fi
    return 0
}

# Assert that at least one file matching a glob pattern exists and is non-empty.
# Usage:  assert_glob_nonempty PATTERN DESCRIPTION
assert_glob_nonempty() {
    local pattern="$1"
    local desc="${2:-$pattern}"

    shopt -s nullglob
    # shellcheck disable=SC2206
    local matches=( $pattern )
    shopt -u nullglob

    if [[ ${#matches[@]} -eq 0 ]]; then
        echo "ASSERT FAILED: no files matched pattern: $desc ($pattern)" >&2
        return 1
    fi

    for f in "${matches[@]}"; do
        if [[ ! -s "$f" ]]; then
            echo "ASSERT FAILED: matched file is empty: $f" >&2
            return 1
        fi
    done
    return 0
}
