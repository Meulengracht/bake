---
description: "Use when editing eBPF-facing protecc matcher headers to keep code verifier-safe, bounded, and maintainable."
applyTo: "libs/protecc/include/protecc/bpf/**"
---

# Protecc BPF Header Instructions

## Scope
These rules apply to headers under `libs/protecc/include/protecc/bpf/` that are compiled into BPF programs.

## Verifier-Safety Rules
- Keep all loops explicitly bounded with compile-time constants (`bpf_for` with `PROTECC_BPF_*` limits).
- Never introduce unbounded loops or data-dependent termination without a hard upper bound.
- Validate offsets, sizes, and pointer ranges before dereferencing profile-backed data.
- Use existing bounds helpers/macros first (`__VALID_PROFILE_PTR`, `__VALID_PTR`, local validate helpers).
- Keep pointer arithmetic simple and monotonic; avoid complex aliasing patterns.
- Do not rely on libc behavior or host-only assumptions in BPF header logic.

## Structure And Complexity
- Prefer small `static __always_inline` helpers over deeply nested logic.
- Avoid local anonymous scoped blocks; extract helper functions instead.
- Keep stack usage conservative; avoid large local arrays and large temporary structs.
- Reuse existing `protecc/bpf/*.h` helpers before adding new helper variants.

## Data And Compatibility
- Preserve on-disk profile layout assumptions from `protecc/profile.h`.
- Treat all profile fields as untrusted; validate before use.
- Keep net/mount/path matcher behavior consistent with corresponding userspace serialization/validation code.

## Logging And Side Effects
- No user-space logging/macros in BPF header logic.
- Keep helpers pure where possible (compute/validate/match), with minimal side effects.

## Validation Workflow
- Build after changes:
```bash
cmake --build build --config Release
```
- Run protecc unit tests:
```bash
ctest --test-dir build -R '^protecc_test$' --output-on-failure
```
- Run verifier smoke test (expected to skip without privileges/tooling):
```bash
ctest --test-dir build -R '^protecc_bpf_verify$' --output-on-failure
```

## Change Discipline
- Keep edits localized and avoid broad refactors in verifier-sensitive headers.
- If a safety check is moved, preserve equivalent guards and ordering.
- When adding helpers, include only concise comments for non-obvious verifier constraints.
