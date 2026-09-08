# Build and install chef action

Composite GitHub Action that checks out chef, installs build dependencies, configures CMake, builds chef, optionally runs CTest, and installs into a workflow-controlled prefix.

## Usage

From another repository:

```yaml
steps:
  - name: Build and install chef
    id: chef
    uses: Meulengracht/bake/.github/actions/build-install@main
    with:
      build-type: Release
      install-prefix: ${{ runner.temp }}/chef-install
```

From a workflow in this repository using the current checkout:

```yaml
steps:
  - uses: actions/checkout@v4
    with:
      submodules: recursive

  - name: Build and install chef
    uses: ./.github/actions/build-install
    with:
      checkout-source: 'false'
      source-dir: .
      build-type: Release
      install-prefix: ${{ runner.temp }}/chef-install
      run-tests: 'true'
```

The action outputs `source-dir`, `build-dir`, and `install-prefix` for later workflow steps.

## Inputs

| Name | Default | Description |
| --- | --- | --- |
| `repository` | `Meulengracht/bake` | GitHub repository containing the chef source to build. |
| `ref` | empty | Git ref to check out. Empty resolves to the action ref when used remotely, then the workflow ref. |
| `checkout-source` | `true` | Check out the chef source before configuring. |
| `checkout-path` | `.chef-source` | Workspace-relative path used when checking out the chef source. |
| `build-type` | `Release` | CMake build type/configuration to build. |
| `source-dir` | empty | Source directory passed to CMake. Empty uses the checked-out chef source. |
| `build-dir` | `build` | Build directory passed to CMake. Relative paths are resolved from `github.workspace`. |
| `install-prefix` | empty | Install prefix. Empty resolves to `$RUNNER_TEMP/chef-install`. Relative paths are resolved from `github.workspace`. |
| `install-dependencies` | `true` | Install platform dependencies before configuring. Supports Ubuntu and Windows runners. |
| `run-tests` | `false` | Run CTest before installation. |

## Outputs

| Name | Description |
| --- | --- |
| `source-dir` | Absolute path to the chef source directory. |
| `build-dir` | Absolute path to the CMake build directory. |
| `install-prefix` | Absolute path where chef was installed. |
