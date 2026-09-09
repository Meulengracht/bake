"""Verify recipe phase semantics with recording tools and a real CMake install."""
import json
import os
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


def run(root, steps, *, fake=True, fail=False, configure_fails=False, build_fails=False, install_fails=False):
    for folder in ("source/app", "build", "install", "ingredients", "bin"):
        (root / folder).mkdir(parents=True, exist_ok=True)
    recipe = root / "recipe.yaml"
    write(recipe, "name: test\nauthor: Test\nemail: test@test.com\nversion: 1.0.0\n"
          "recipes:\n- name: app\n  steps:\n" + steps +
          "packs:\n- name: test\n  summary: Test\n  type: ingredient\n")
    env = dict(os.environ, CHEF_TEST_LOG=str(root / "calls.jsonl"),
               CHEF_TEST_CONFIGURE_FAIL=str(int(configure_fails)),
               CHEF_TEST_BUILD_FAIL=str(int(build_fails)), CHEF_TEST_INSTALL_FAIL=str(int(install_fails)))
    if fake:
        # Record the actual argv/environment received by the spawned programs.
        recorder = f"#!{sys.executable}\n" + '''import json, os, sys
from pathlib import Path
name = Path(sys.argv[0]).name
with open(os.environ['CHEF_TEST_LOG'], 'a') as out:
    out.write(json.dumps({'tool': name, 'args': sys.argv[1:], 'cwd': os.getcwd(),
                          'mode': os.environ.get('MODE'), 'shared': os.environ.get('SHARED'),
                          'only': os.environ.get('CONFIG_ONLY')}) + '\\n')
if os.environ['CHEF_TEST_CONFIGURE_FAIL'] == '1' and (name == 'configure' or '-S' in sys.argv):
    sys.exit(7)
if os.environ['CHEF_TEST_BUILD_FAIL'] == '1' and '--build' in sys.argv:
    sys.exit(8)
if os.environ['CHEF_TEST_INSTALL_FAIL'] == '1' and '--install' in sys.argv:
    sys.exit(9)
'''
        for tool in ("cmake", "make", "ninja"):
            write(root / "bin" / tool, recorder, True)
        write(root / "source/app/subproject/configure", recorder, True)
        env["PATH"] = str(root / "bin") + os.pathsep + env["PATH"]
    else:
        env["PATH"] = str(Path(CMAKE).parent) + os.pathsep + env["PATH"]
    result = subprocess.run([DRIVER, str(recipe), str(root), str(root / "source"),
                             str(root / "build"), str(root / "install"), str(root / "ingredients")],
                            env=env, capture_output=True, text=True, timeout=60)
    assert (result.returncode != 0) == fail, result.stdout + result.stderr
    log = root / "calls.jsonl"
    return [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []


with tempfile.TemporaryDirectory(prefix="chef-oven-test-") as directory:
    base = Path(directory)
    custom = """  - name: build
    type: build
    system: cmake
    arguments: [--parallel, '2']
    env: {MODE: build, SHARED: inherited}
    configure:
      source-dir: subproject
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
    calls = run(base / "skip", "  - name: build\n    type: build\n    system: cmake\n    configure: false\n")
    assert len(calls) == 2 and calls[0]["args"][0] == "--build", calls
    for backend in ("make", "ninja"):
        calls = run(base / backend, f"  - name: build\n    type: build\n    system: {backend}\n")
        assert len(calls) == 2 and all(c["tool"] == backend for c in calls), calls
    for backend in ("autotools", "autoconf"):
        calls = run(base / backend, f"""  - name: build
    type: build
    system: {backend}
    arguments: [V=1]
    configure:
      source-dir: subproject
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
    write(root / "source/app/main.c", '#include <stdio.h>\nint main(void) { puts("hello"); return 0; }\n')
    run(root, "  - name: build\n    type: build\n    system: cmake\n", fake=False)
    assert subprocess.check_output([str(root / "install/usr/bin/hello")], text=True).strip() == "hello"
    assert (root / "install/usr/share/result.txt").read_text() == "combined-build-ok"
    # Reconfiguration and explicit skipping both work against an existing build tree.
    run(root, "  - name: build\n    type: build\n    system: cmake\n", fake=False)
    run(root, "  - name: build\n    type: build\n    system: cmake\n    configure: false\n", fake=False)
print("Recipe workflows passed")
