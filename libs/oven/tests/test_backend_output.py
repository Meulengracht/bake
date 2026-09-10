"""Verify backend output is logged as literal text.

Run with python3 libs/oven/tests/test_backend_output.py [C compiler].
The linker discards unrelated backend functions, so this test does not need the
full oven runtime.
"""

from pathlib import Path
import subprocess
import sys
import tempfile


HARNESS = r"""
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "BACKEND_SOURCE"

static const char* expected;
static enum vlog_level expected_level;
static int calls;

void vlog_output(enum vlog_level level, const char* tag, const char* format, ...)
{
    char actual[8192];
    va_list args;

    (void)tag;
    va_start(args, format);
    vsnprintf(actual, sizeof(actual), format, args);
    va_end(args);

    // Fail on either altered text or an unexpected severity level.
    if (strcmp(actual, expected) != 0 || level != expected_level) {
        fprintf(stderr, "Compiler output changed: %s\n", actual);
        exit(1);
    }

    calls++;
}

int main(void)
{
    const char* samples[] = {
        "[ 89%] Building CXX object OptionValueEnumeration.cpp.o\n",
        "warning: format '%s' expects char*, got '%d'; %n %p %%\n",
        "trailing percent %",
        "ordinary output\n"
    };

    // Send every sample through both channels to cover both logging levels.
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        expected = samples[i];

        expected_level = VLOG_LEVEL_DEBUG;
        OUTPUT_HANDLER(expected, PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT);

        expected_level = VLOG_LEVEL_ERROR;
        OUTPUT_HANDLER(expected, PLATFORM_SPAWN_OUTPUT_TYPE_STDERR);
    }

    return calls == 8 ? 0 : 1;
}
"""


def main():
    """Compile the backend output harness and execute it."""
    root = Path(__file__).resolve().parents[3]
    compiler = sys.argv[1] if len(sys.argv) > 1 else "cc"

    with tempfile.TemporaryDirectory(prefix="chef-output-test-") as directory:
        directory = Path(directory)
        source = directory / "common.c"
        executable = directory / "common"
        source.write_text(
            HARNESS.replace(
                "BACKEND_SOURCE",
                str(root / "libs/oven/backends/common.c"),
            ).replace("OUTPUT_HANDLER", "__output")
        )

        includes = [
            root / "libs" / path
            for path in (
                "oven/backends/include",
                "oven/include",
                "platform/include",
                "common/include",
                "vlog/include",
            )
        ]
        command = [
            compiler,
            "-ffunction-sections",
            "-fdata-sections",
            *[f"-I{path}" for path in includes],
            str(source),
            "-Wl,--gc-sections",
            "-o",
            str(executable),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True, timeout=10)

    print("common: stdout/stderr preserved literally")


if __name__ == "__main__":
    main()
