# Chef Workspace Instructions

## Code Style
- Language is C with CMake; follow existing 4-space indentation and local formatting patterns in nearby files.
- Public functions use `snake_case`; file-local helpers are `static` and prefixed with `__`.
- Prefer small helper functions over deeply nested logic or ad-hoc scoped blocks.
- Use early returns and cleanup sections (`goto cleanup`) for resource handling.
- Always check return values and allocation results.

## Logging
- Use `vlog` macros consistently:
- `VLOG_DEBUG` for function entry and internal decisions.
- `VLOG_ERROR` on all error paths.
- `VLOG_TRACE` for user-facing output only.

## Architecture
- `tools/`: user CLIs (`bake`, `order`, `serve`, `bakectl`, `cvctl`, `mkcdk`, `serve-exec`).
- `libs/`: core libraries, especially:
- `libs/protecc`: path/net/mount policy matching and profile serialization.
- `libs/containerv`: container runtime/policies, including eBPF programs.
- `daemons/`: long-running services (`served`, `cvd`, `cookd`, `waiterd`).
- `protocols/*.gr`: RPC protocol definitions; generated stubs are consumed from build outputs.

## Build And Test
- Configure:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```
- Build:
```bash
cmake --build build --config Release
```
- Run all tests:
```bash
ctest --test-dir build --output-on-failure
```
- Run `protecc` tests:
```bash
ctest --test-dir build -R '^protecc_test$' --output-on-failure
```

## Basic Container Build Smoke Test
- To verify container-backed builds, start `cvd` first; `bake` uses the `cvd` daemon directly to spawn containers and execute build steps inside them.
- Use the built binaries from the current tree for this check:
```bash
sudo -n ./build/daemons/cvd/cvd -vv
```
- After `cvd` is alive, use `examples/recipes/hello.yaml` as the basic smoke-test recipe for `bake`.
- Copy both the recipe file and its `examples/recipes/hello-world` source directory into an isolated work directory before invoking `bake`, so the recipe's relative source path resolves correctly:
```bash
work_dir="$(mktemp -d)"
cp -a examples/recipes/hello.yaml "$work_dir/hello.yaml"
cp -a examples/recipes/hello-world "$work_dir/hello-world"
(cd "$work_dir" && ./build/bin/bake build hello.yaml -v)
```
- Expected result: `bake build hello.yaml -v` succeeds after `cvd` has started, proving the basic container execution path works.
- Reference workflow: `tests/system/cases/hello-build.sh`.

## Protecc eBPF Verifier Smoke Test
- `libs/protecc` includes an isolated verifier smoke test (`protecc_bpf_verify`) that compiles BPF objects and attempts `bpftool prog loadall`.
- This test is expected to skip (return code 77) when prerequisites are missing (root privileges, `bpftool`, `/sys/kernel/btf/vmlinux`, or `/sys/fs/bpf`).
- Run explicitly:
```bash
ctest --test-dir build -R '^protecc_bpf_verify$' --output-on-failure
```

## Environment Pitfalls
- Clone with submodules:
```bash
git submodule update --init --recursive
```
- On Linux, missing dev packages (`libcurl`, `openssl`, `libfuse3`, `libseccomp`, `libbpf`, etc.) will cause configure/build failures.
- If CMake state becomes inconsistent, clear and reconfigure build dir.

## Conventions That Matter
- Keep changes localized and reuse existing helpers before adding new abstractions.
- For cross-platform behavior, prefer existing platform layers under `libs/platform` and established `#if defined(_WIN32)` patterns used in the repo.
- When editing `libs/protecc/include/protecc/bpf/*.h`, ensure verifier-safe bounds checks remain explicit and loop bounds stay finite.
