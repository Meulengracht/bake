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
