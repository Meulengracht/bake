---
description: "Use when editing CMake files or build/test wiring to keep target setup, options, and CTest integration consistent in this workspace."
applyTo: "**/{CMakeLists.txt,*.cmake}"
---

# CMake Build Instructions

## Configure And Build Baseline
- Prefer out-of-source builds in `build/`.
- Canonical configure command:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```
- Canonical build command:
```bash
cmake --build build --config Release
```

## Test Integration
- Use CTest for all test registration (`add_test(...)`).
- Prefer stable, explicit test names (example: `protecc_test`, `protecc_bpf_verify`).
- Keep tests runnable with:
```bash
ctest --test-dir build --output-on-failure
```
- For optional/environment-dependent tests, use skip semantics instead of hard fail where appropriate.

## Target And Dependency Conventions
- Keep target names descriptive and scoped by component (`protecc_*`, `containerv_*`, etc.).
- Use `target_link_libraries(...)` and `target_include_directories(...)` on targets directly; avoid global include/link side effects.
- Prefer explicit target dependencies for generated artifacts (`add_dependencies(...)` or command dependencies) instead of relying on build order.
- Keep generated file outputs under the current binary dir, not source dirs.

## Option And Feature Gating
- Guard optional tooling/features (BPF tools, platform-only deps) with clear `find_program`/`find_package` checks.
- When optional features are unavailable, degrade gracefully (disable feature, keep core build working).
- Keep option names consistent and component-scoped (`PROTECC_*`, `CHEF_*`).

## Cross-Platform Discipline
- Maintain Linux/Windows compatibility patterns already used in the workspace.
- Avoid introducing platform-specific commands without guards.
- For architecture-specific BPF settings, keep mappings explicit and include a safe fallback.

## Custom Commands And Generated Artifacts
- For `add_custom_command`, always define:
- Explicit `OUTPUT`
- Complete `DEPENDS`
- `VERBATIM`
- Human-readable `COMMENT`
- Use binary-dir output locations and avoid modifying tracked source files during build.

## Change Discipline
- Keep CMake edits localized to the owning component (`libs/protecc`, `libs/containerv`, etc.).
- Do not silently rename existing public targets/tests unless all call sites are updated.
- Preserve existing behavior unless the change request explicitly asks for build/test behavior changes.

## Validation Workflow
- After CMake edits, validate in order:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```
