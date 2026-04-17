# Ubuntu Base

## What This Artifact Is

The Ubuntu base is the Linux guest root filesystem used by recipes that target `base: ubuntu:24`, such as [examples/recipes/hello.yaml](../../examples/recipes/hello.yaml).

Chef currently uses two related Ubuntu-base flows:

- A built-in runtime resolver that downloads and extracts the upstream `ubuntu-base` tarball.
- Example recipes that build a richer Ubuntu rootfs with Chisel and then package it as a Chef `.pack`.

These are complementary rather than identical. The built-in resolver is the minimal path for getting a Linux build container running. The example recipes are the better reference when you want a redistributable base artifact.

## Runtime Construction Used By `bake`

On Linux hosts, the built-in Ubuntu helper downloads the matching upstream archive with `wget` and extracts it with `tar`. That logic lives in [libs/containerv/disk/ubuntu.c](../../libs/containerv/disk/ubuntu.c) and resolves names like `ubuntu:24` using [libs/containerv/include/chef/containerv/disk/ubuntu.h](../../libs/containerv/include/chef/containerv/disk/ubuntu.h).

That flow is what backs the default runtime resolution for `base: ubuntu:24`. It is the right choice when you only need the build container and do not need a package to publish.

## Building A Redistributable Ubuntu Base

The repo ships two example construction recipes:

- [examples/recipes/linux/base.yaml](../../examples/recipes/linux/base.yaml)
- [examples/recipes/windows/linux.yaml](../../examples/recipes/windows/linux.yaml)

Both use Chisel to cut a curated Ubuntu filesystem. The Linux-hosted variant writes directly into the install root. The Windows/LCOW-oriented variant writes into `<install>/rootfs` and can optionally also emit an `ext4.vhdx` for compatibility workflows.

The shared construction logic is in:

- [examples/recipes/linux/construct.sh](../../examples/recipes/linux/construct.sh)
- [examples/recipes/windows/construct.sh](../../examples/recipes/windows/construct.sh)

### What The Scripts Do

The scripts:

1. Download a Go toolchain.
2. Install `chisel` if it is not already present.
3. Cut a selected set of Ubuntu slices into the output directory.
4. In the Windows/LCOW variant, optionally build `ext4.raw` and `ext4.vhdx` if `BUILD_VHDX=1`.

The LCOW variant keeps the `rootfs` directory by default because Chef currently uses the directory tree for LCOW rootfs mapping. The UVM itself is a separate artifact documented in [lcow.md](lcow.md).

### Running The Scripts Directly

If you want only the rootfs directory and not the final `.pack`, run the construction script directly on a Linux host:

```bash
cd examples/recipes/linux
./construct.sh /tmp/linux-base amd64
```

For the Windows/LCOW-oriented variant:

```bash
cd examples/recipes/windows
./construct.sh /tmp/linux-base amd64
```

Useful environment switches for the Windows/LCOW-oriented script:

- `BUILD_VHDX=1` also emits `ext4.vhdx`.
- `KEEP_ROOTFS=0` removes the `rootfs` directory after image generation.

## Packaging It As A Chef `.pack`

Both example recipes declare an OS pack:

```yaml
packs:
- name: linux-base
  type: os
```

That means `bake` will take the staged install tree and emit a `.pack` file:

```bash
cd examples/recipes/linux
bake build base.yaml
```

Or:

```bash
cd examples/recipes/windows
bake build linux.yaml
```

The resulting `.pack` is a VaFS-backed Chef package image, not just a tarball. This is the primary format when you want `served` to manage and deliver the Ubuntu runtime base for applications that declare `base: ubuntu:24`.

In other words:

1. The application recipe declares `platforms[].base: ubuntu:24`.
2. `bake` records that base in the application package manifest.
3. `served` ensures the matching Ubuntu base package is installed before the application.
4. The base `.pack` is then provided to `cvd` and `containerv` as part of the runtime layer stack.

The built-in Ubuntu fetch path is still the simpler route for direct build-container setup when you do not need served-managed distribution.

## Creating A Raw VaFS Image

If you want a plain VaFS archive instead of a Chef package, pack the normalized rootfs directory with `mkvafs`:

```bash
mkvafs --git-ignore --out linux-base.vafs /tmp/linux-base
```

Use this when you need a plain filesystem image for transport or inspection. Use `bake build ...` when you need the served-facing runtime package format.

## When To Use Which Flow

- Use plain `base: ubuntu:24` when you only need Chef to bring up a Linux build container.
- Use [examples/recipes/linux/base.yaml](../../examples/recipes/linux/base.yaml) when you want a Linux base package for Linux hosts.
- Use [examples/recipes/windows/linux.yaml](../../examples/recipes/windows/linux.yaml) when you are preparing the Linux rootfs side of a Windows LCOW workflow.
- Remember that Windows-hosted Linux builds need both the Ubuntu rootfs and an LCOW UVM bundle.