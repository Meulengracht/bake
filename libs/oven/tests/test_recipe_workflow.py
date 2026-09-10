"""Verify recipe phase semantics with recording tools and a real CMake install."""
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile

DRIVER, CMAKE = sys.argv[1:]


def write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    if executable:
        path.chmod(0o755)


def run(
    root,
    steps,
    *,
    fake=True,
    fail=False,
    configure_fails=False,
    build_fails=False,
    install_fails=False,
    extra_env=None,
):
    """Run the recipe driver and return the recorded process calls."""
    # Create every directory that the recipe driver and fake tools will use.
    for folder in ("source/app", "build", "install", "ingredients", "bin"):
        (root / folder).mkdir(parents=True, exist_ok=True)
    recipe = root / "recipe.yaml"
    write(recipe, "name: test\nauthor: Test\nemail: test@test.com\nversion: 1.0.0\n"
          "recipes:\n- name: app\n  steps:\n" + steps +
          "packs:\n- name: test\n  summary: Test\n  type: ingredient\n")
    env = dict(
        os.environ,
        CHEF_TEST_LOG=str(root / "calls.jsonl"),
        CHEF_TEST_CONFIGURE_FAIL=str(int(configure_fails)),
        CHEF_TEST_BUILD_FAIL=str(int(build_fails)),
        CHEF_TEST_INSTALL_FAIL=str(int(install_fails)),
    )
    env.update(extra_env or {})
    if fake:
        # Record the actual argv/environment received by the spawned programs.
        recorder = f"#!{sys.executable}\n" + '''import json, os, sys
from pathlib import Path
name = Path(sys.argv[0]).name
with open(os.environ['CHEF_TEST_LOG'], 'a') as out:
    out.write(json.dumps({
        'tool': name,
        'args': sys.argv[1:],
        'cwd': os.getcwd(),
        'mode': os.environ.get('MODE'),
        'shared': os.environ.get('SHARED'),
        'only': os.environ.get('CONFIG_ONLY'),
        'site': os.environ.get('CONFIG_SITE'),
    }) + '\\n')
# Simulate configuration failure for every backend's generate command.
if os.environ['CHEF_TEST_CONFIGURE_FAIL'] == '1' and (
    name == 'configure' or '-S' in sys.argv or 'setup' in sys.argv
):
    sys.exit(7)
# Simulate compilation failure while allowing install-only calls to be distinguished.
if os.environ['CHEF_TEST_BUILD_FAIL'] == '1' and (
    '--build' in sys.argv or 'compile' in sys.argv or
    (name in ('make', 'ninja') and 'install' not in sys.argv)
):
    sys.exit(8)
# Simulate installation failure after a successful compile.
if os.environ['CHEF_TEST_INSTALL_FAIL'] == '1' and (
    '--install' in sys.argv or 'install' in sys.argv
):
    sys.exit(9)
'''
        # Install one recorder under each backend name used by the scenarios.
        for tool in ("cmake", "make", "ninja", "meson"):
            write(root / "bin" / tool, recorder, True)
        write(root / "source/app/subproject/configure", recorder, True)
        env["PATH"] = str(root / "bin") + os.pathsep + env["PATH"]
    else:
        env["PATH"] = str(Path(CMAKE).parent) + os.pathsep + env["PATH"]
    result = subprocess.run(
        [
            DRIVER,
            str(recipe),
            str(root),
            str(root / "source"),
            str(root / "build"),
            str(root / "install"),
            str(root / "ingredients"),
        ],
        env=env,
        capture_output=True,
        text=True,
        timeout=60,
    )
    diagnostics = result.stdout + result.stderr
    # Add backend diagnostics only when the observed result disagrees with the expectation.
    if (result.returncode != 0) != fail:
        # These logs explain failures that happen after the child process starts.
        for log in (root / "build/app/config.log", root / "build/app/meson-logs/meson-log.txt"):
            if log.exists():
                diagnostics += log.read_text()
    assert (result.returncode != 0) == fail, diagnostics
    log = root / "calls.jsonl"
    # A failed setup may not create a call log at all.
    if not log.exists():
        return []

    return [json.loads(line) for line in log.read_text().splitlines()]


with tempfile.TemporaryDirectory(prefix="chef-oven-test-") as directory:
    base = Path(directory)
    custom = """  - name: build
    type: build
    system: cmake
    source-dir: subproject
    arguments: [--parallel, '2']
    env: {MODE: build, SHARED: inherited}
    configure:
      arguments: [-DFEATURE=ON]
      env: {MODE: configure, CONFIG_ONLY: "$[[ INSTALL_PREFIX ]]"}
"""
    calls = run(base / "custom", custom)
    assert len(calls) == 3, calls
    configure, build, install = calls
    assert configure["args"][0] == "-S" and configure["args"][1].endswith("/app/subproject"), calls
    assert "-DFEATURE=ON" in configure["args"] and "--parallel" not in configure["args"], calls
    assert configure["mode"] == "configure" and configure["shared"] == "inherited", calls
    assert configure["only"] == str(base / "custom/install"), calls
    assert build["args"] == ["--build", ".", "--config", "Release", "--parallel", "2"], calls
    assert install["args"] == ["--install", ".", "--config", "Release"], calls
    assert all(c["mode"] == "build" and c["only"] is None for c in (build, install)), calls
    calls = run(base / "failed", custom, fail=True, configure_fails=True)
    assert len(calls) == 1, calls
    calls = run(base / "build-failed", custom, fail=True, build_fails=True)
    assert len(calls) == 2, calls
    calls = run(base / "install-failed", custom, fail=True, install_fails=True)
    assert len(calls) == 3, calls
    calls = run(
        base / "skip",
        "  - name: build\n"
        "    type: build\n"
        "    system: cmake\n"
        "    configure: false\n",
    )
    assert len(calls) == 2 and calls[0]["args"][0] == "--build", calls
    # Verify the two direct build backends receive both build and install calls.
    for backend in ("make", "ninja"):
        calls = run(base / backend, f"  - name: build\n    type: build\n    system: {backend}\n")
        assert len(calls) == 2 and all(c["tool"] == backend for c in calls), calls
    # Verify Autotools aliases preserve configure arguments and build variables.
    for backend in ("autotools", "autoconf"):
        calls = run(base / backend, f"""  - name: build
    type: build
    system: {backend}
    source-dir: subproject
    arguments: [V=1]
    configure:
      arguments: [--enable-shared]
""")
        assert [c["tool"] for c in calls] == ["configure", "make", "make"], calls
        assert "--enable-shared" in calls[0]["args"] and "V=1" not in calls[0]["args"], calls
        assert "V=1" in calls[1]["args"] and calls[2]["args"] == ["install"], calls
    calls = run(base / "explicit", """  - name: config
    type: generate
    system: cmake
  - name: build
    type: build
    system: make
    depends: [config]
""")
    assert [c["tool"] for c in calls] == ["cmake", "make", "make"], calls
    # A real project proves the generated build tree and staging prefix work together.
    root = base / "real"
    write(root / "source/app/CMakeLists.txt", '''cmake_minimum_required(VERSION 3.14)
project(recipe_test C)
add_executable(hello main.c)
install(TARGETS hello DESTINATION bin)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/result.txt" "combined-build-ok")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/result.txt" DESTINATION share)
''')
    write(
        root / "source/app/main.c",
        "#include <stdio.h>\n"
        "int main(void) {\n"
        '    puts("hello");\n'
        "    return 0;\n"
        "}\n",
    )
    run(root, "  - name: build\n    type: build\n    system: cmake\n", fake=False)
    assert (
        subprocess.check_output(
            [str(root / "install/usr/bin/hello")],
            text=True,
        ).strip()
        == "hello"
    )
    assert (root / "install/usr/share/result.txt").read_text() == "combined-build-ok"
    # Reconfiguration and explicit skipping both work against an existing build tree.
    run(root, "  - name: build\n    type: build\n    system: cmake\n", fake=False)
    run(
        root,
        "  - name: build\n"
        "    type: build\n"
        "    system: cmake\n"
        "    configure: false\n",
        fake=False,
    )
print("Recipe workflows passed")


def generate(system, arguments=(), extra=""):
    """Build a YAML generate step for a backend test."""
    return (
        f"  - name: configure\n    type: generate\n    system: {system}\n"
        f"    arguments: {json.dumps(list(arguments))}\n"
        + extra
    )

with tempfile.TemporaryDirectory(prefix="chef-backends space-") as directory:
    base = Path(directory)
    # Exact and typed CMake definitions, lookalikes, spaces, repeated prefixes and long lists.
    root = base / "cmake"
    calls = run(root, generate("cmake", [
        '-D', 'CMAKE_INSTALL_PREFIX:PATH="/custom dir"',
        '-DCMAKE_PREFIX_PATH="/one;/two dirs"',
        '-DNOTE=CMAKE_INSTALL_PREFIX', '-DCMAKE_INSTALL_PREFIX_EXTRA=unchanged']))
    args = calls[0]["args"]
    assert "CMAKE_INSTALL_PREFIX:PATH=" + str(root / "install/custom dir") in args, args
    assert "-DNOTE=CMAKE_INSTALL_PREFIX" in args
    assert "-DCMAKE_INSTALL_PREFIX_EXTRA=unchanged" in args
    expected_prefixes = ";".join(
        str(root / path)
        for path in ("ingredients", "ingredients/usr", "ingredients/usr/local")
    )
    assert (
        "-DCMAKE_PREFIX_PATH=/one;/two dirs;" + expected_prefixes in args
    ), args
    # Check staged prefixes that are already inside, outside, or equal to the root.
    for n, value, expected in (
        ("existing", '"$[[ INSTALL_PREFIX ]]/usr"', "install/usr"),
        ("boundary", '"$[[ INSTALL_PREFIX ]]-other"', None),
        ("root", '"$[[ INSTALL_PREFIX ]]"', "install")):
        root = base / n
        args = run(root, generate("cmake", ["-DCMAKE_INSTALL_PREFIX=" + value]))[0]["args"]
        actual = next(
            argument.split("=", 1)[1]
            for argument in args
            if argument.startswith("-DCMAKE_INSTALL_PREFIX=")
        )
        # A known relative result is exact; the sibling case needs boundary checks.
        if expected:
            assert actual == str(root / expected), args
        else:
            assert actual.startswith(str(root / "install") + "/"), args
            assert actual.endswith("install-other"), args

    invalid_values = (
        "-DCMAKE_INSTALL_PREFIX",
        "-DCMAKE_INSTALL_PREFIX=../outside",
        '-DKEY="unterminated',
    )
    # Every malformed prefix must stop before a backend process is launched.
    for n, value in enumerate(invalid_values):
        assert run(
            base / f"invalid{n}",
            generate("cmake", [value]),
            fail=True,
        ) == []

    long_paths = ";".join("/prefix" + str(i) for i in range(1000))
    args = run(
        base / "long",
        generate("cmake", ["-DCMAKE_PREFIX_PATH=" + long_paths]),
    )[0]["args"]
    assert next(
        argument for argument in args if argument.startswith("-DCMAKE_PREFIX_PATH=")
    ).startswith("-DCMAKE_PREFIX_PATH=" + long_paths + ";")

    root = base / "windows"
    args = run(
        root,
        generate("cmake"),
        extra_env={"CHEF_TEST_PLATFORM": "windows"},
    )[0]["args"]
    assert "-DCMAKE_INSTALL_PREFIX=" + str(root / "install/Program Files") in args, args
    assert (
        "-DCMAKE_PREFIX_PATH="
        + str(root / "ingredients")
        + ";"
        + str(root / "ingredients/Program Files")
        in args
    )

    # Autotools prefixes at the start/middle/end and both accepted syntaxes.
    autotools_arguments = (
        ["--prefix=/usr", "--enable-shared"],
        ["--enable-shared", '--prefix="/custom dir"'],
        ["--prefix", '"$[[ INSTALL_PREFIX ]]/usr"'],
    )
    # Exercise prefix options at different positions and in both spellings.
    for n, arguments in enumerate(autotools_arguments):
        root = base / f"auto{n}"
        calls = run(
            root,
            generate("autotools", arguments, "    source-dir: subproject\n"),
        )
        args = calls[0]["args"]
        # Separate and equals-form options store their value at different indexes.
        if "--prefix" in args:
            value = args[args.index("--prefix") + 1]
        else:
            value = next(
                argument[9:]
                for argument in args
                if argument.startswith("--prefix=")
            )

        expected = "install/custom dir" if n == 1 else "install/usr"
        assert value == str(root / expected), args

    root = base / "make-in-tree-build"
    calls = run(
        root,
        "  - name: build\n"
        "    type: build\n"
        "    system: make\n"
        "    source-dir: subproject\n"
        "    make-in-tree: true\n",
    )
    assert len(calls) == 2, calls
    assert all(
        call["cwd"] == str(root / "source/app/subproject")
        for call in calls
    ), calls

    root = base / "cross"
    calls = run(
        root,
        generate("autotools", extra="    source-dir: subproject\n"),
        extra_env={"CHEF_TEST_ARCH": "other-arch"},
    )
    site = Path(calls[0]["site"])
    assert site == root / "build/app/chef-config.site"
    assert site.exists()
    assert not list((root / "install").rglob("config.site"))
    flags = subprocess.check_output(
        [
            "sh",
            "-c",
            '. "$1"; printf "%s\n%s\n%s" "$CFLAGS" "$CPPFLAGS" "$LDFLAGS"',
            "sh",
            str(site),
        ],
        env=dict(
            os.environ,
            CFLAGS="-O2",
            CPPFLAGS="-DKEEP",
            LDFLAGS="-pthread",
        ),
        text=True,
    )
    assert flags.startswith("-O2\n-DKEEP "), flags
    assert "-pthread " in flags, flags
    assert str(root / "ingredients/usr/include") in flags, flags
    assert "data->paths" not in flags, flags

    root = base / "custom-site"
    calls = run(
        root,
        generate(
            "autotools",
            extra="    source-dir: subproject\n"
            "    env: {CONFIG_SITE: custom.site}\n",
        ),
        extra_env={"CHEF_TEST_ARCH": "other-arch"},
    )
    assert calls[0]["site"] == "custom.site"
    assert not (root / "build/app/chef-config.site").exists()

    root = base / "site-write-failure"
    (root / "build/app/chef-config.site").mkdir(parents=True)
    assert run(
        root,
        generate("autotools", extra="    source-dir: subproject\n"),
        extra_env={"CHEF_TEST_ARCH": "other-arch"},
        fail=True,
    ) == []

    meson_steps = generate(
        "meson",
        extra="    source-dir: subproject\n",
    ) + (
        "  - name: build\n"
        "    type: build\n"
        "    system: meson\n"
        "    arguments: [-j, '2']\n"
    )
    root = base / "meson"
    calls = run(root, meson_steps)
    assert [call["args"][0] for call in calls] == [
        "setup",
        "compile",
        "install",
    ], calls
    assert calls[0]["args"][1:3] == [
        str(root / "build/app"),
        str(root / "source/app/subproject"),
    ]
    assert calls[1]["args"] == [
        "compile",
        "-C",
        str(root / "build/app"),
        "-j",
        "2",
    ], calls
    assert calls[2]["args"] == [
        "install",
        "-C",
        str(root / "build/app"),
        "--no-rebuild",
    ], calls

    root = base / "meson-cross"
    write(root / "config/cross.txt", "[properties]\nroot = '$[[ BUILD_INGREDIENTS_PREFIX ]]'\n")
    calls = run(
        root,
        generate("meson", extra="    meson-cross-file: config/cross.txt\n"),
    )
    assert "--cross-file" in calls[0]["args"]
    assert str(root / "ingredients") in (root / "build/app/cross-file.txt").read_text()

    root = base / "meson-invalid-template"
    write(root / "cross.txt", "root = '$[[ UNKNOWN ]]'\n")
    assert run(
        root,
        generate("meson", extra="    meson-cross-file: cross.txt\n"),
        fail=True,
    ) == []

    # Missing input and an unwriteable destination must both fail generation.
    for name in ("missing", "write-failure"):
        root = base / f"meson-{name}"
        # Only the write-failure case needs a directory that blocks file creation.
        if name == "write-failure":
            write(root / "cross.txt", "[properties]\n")
            (root / "build/app/cross-file.txt").mkdir(parents=True)
        assert run(
            root,
            generate("meson", extra="    meson-cross-file: cross.txt\n"),
            fail=True,
        ) == []

    meson_failures = (
        ("setup", "configure_fails", 1),
        ("compile", "build_fails", 2),
        ("install", "install_fails", 3),
    )
    # Confirm each Meson phase stops the workflow at the failing command.
    for name, flag, count in meson_failures:
        calls = run(base / f"meson-fail-{name}", meson_steps, fail=True, **{flag: True})
        assert len(calls) == count, calls

    # Run clean and failure scenarios for every supported build backend.
    for system in ("make", "ninja", "meson", "cmake"):
        extra = "    configure: false\n" if system == "cmake" else ""
        steps = (
            f"  - name: build\n"
            f"    type: build\n"
            f"    system: {system}\n"
            + extra
        )
        calls = run(
            base / f"clean-{system}",
            steps,
            extra_env={"CHEF_TEST_CLEAN": "1"},
        )
        assert len(calls) == 1, calls
        assert "clean" in calls[0]["args"] or "--clean" in calls[0]["args"], calls

        # Build failures skip install; install failures occur after both calls.
        for phase in ("build_fails", "install_fails"):
            calls = run(
                base / f"fail-{system}-{phase}",
                steps,
                fail=True,
                **{phase: True},
            )
            expected_calls = 1 if phase == "build_fails" else 2
            assert len(calls) == expected_calls, calls

    root = base / "make-in-tree"
    calls = run(
        root,
        "  - name: build\n"
        "    type: build\n"
        "    system: make\n"
        "    make-in-tree: true\n"
        "    source-dir: subproject\n",
        extra_env={"CHEF_TEST_CLEAN": "1"},
    )
    assert calls[0]["cwd"] == str(root / "source/app/subproject"), calls

    # Real CMake dependency discovery and installation with spaces in all roots.
    root = base / "real-cmake"
    write(
        root / "ingredients/usr/lib/cmake/ChefDependency/ChefDependencyConfig.cmake",
        "set(CHEF_DEP_FOUND YES)\n",
    )
    write(root / "source/app/CMakeLists.txt", '''cmake_minimum_required(VERSION 3.15)
project(test NONE)
find_package(ChefDependency REQUIRED CONFIG)
if(NOT CHEF_DEP_FOUND)
  message(FATAL_ERROR "Ingredient was not found")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/result.txt" "staged")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/result.txt" DESTINATION share)
''')
    run(
        root,
        "  - name: build\n"
        "    type: build\n"
        "    system: cmake\n",
        fake=False,
    )
    assert (root / "install/usr/share/result.txt").read_text() == "staged"

    # Run the real Meson scenario only when both required executables are present.
    if shutil.which("meson") and shutil.which("ninja"):
        root = base / "real-meson"
        write(
            root / "source/app/subproject/meson.build",
            "project('backend-test', 'c')\n"
            "executable('hello', 'main.c', install: true)\n",
        )
        write(
            root / "source/app/subproject/main.c",
            '#include <stdio.h>\n'
            'int main(void) {\n'
            '    puts("meson-ok");\n'
            '    return 0;\n'
            '}\n',
        )
        run(root, meson_steps, fake=False)
        executable = root / "install/usr/local/bin/hello"
        assert subprocess.check_output([executable], text=True).strip() == "meson-ok"
        run(root, meson_steps, fake=False)  # Existing tree uses setup --reconfigure.
        run(
            root,
            meson_steps,
            fake=False,
            extra_env={"CHEF_TEST_CLEAN": "1"},
        )
        assert not (root / "build/app/hello").exists()
    else:
        print("SKIP real Meson: meson/ninja not installed")
print("Backend regressions passed")

# Run the real Autotools scenario only when generation and compilation tools exist.
if shutil.which("autoconf") and shutil.which("make"):
    with tempfile.TemporaryDirectory(prefix="chef-real-autotools-") as directory:
        root = Path(directory)
        source = root / "source/app"
        write(root / "ingredients/usr/include/chef_ingredient.h", "#define CHEF_INGREDIENT 42\n")
        write(source / "configure.ac", '''AC_INIT([backend-test], [1.0])
AC_CONFIG_SRCDIR([main.c])
AC_PROG_CC
AC_CHECK_HEADER([chef_ingredient.h], [], [AC_MSG_ERROR([ingredient header missing])])
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
''')
        write(
            source / "main.c",
            "#include <chef_ingredient.h>\n"
            "int main(void) {\n"
            "    return CHEF_INGREDIENT == 42 ? 0 : 1;\n"
            "}\n",
        )
        write(source / "Makefile.in", '''CC = @CC@
CPPFLAGS = @CPPFLAGS@
CFLAGS = @CFLAGS@
LDFLAGS = @LDFLAGS@
prefix = @prefix@
all: hello
hello: @srcdir@/main.c
\t$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o $@
install: hello
\tmkdir -p "$(prefix)/bin"
\tcp hello "$(prefix)/bin/hello"
clean:
\trm -f hello
''')
        subprocess.run(["autoconf"], cwd=source, check=True, capture_output=True)
        steps = "  - name: build\n    type: build\n    system: autotools\n"
        run(root, steps, fake=False, extra_env={"CHEF_TEST_ARCH": "other-arch"})
        subprocess.run([root / "install/usr/local/bin/hello"], check=True)
        run(root, steps, fake=False, extra_env={"CHEF_TEST_ARCH": "other-arch"})
        run(root, steps, fake=False, extra_env={"CHEF_TEST_CLEAN": "1"})
        assert not (root / "build/app/hello").exists()
        print("Real Autotools build/install/clean passed")
else:
    print("SKIP real Autotools: autoconf/make not installed")
