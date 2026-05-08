# LCOW UVM Bundle

## What This Artifact Is

The LCOW base is not a root filesystem by itself. It is the Linux utility VM bundle required to run Linux containers on a Windows host.

A normalized LCOW bundle contains:

- either `uvm.vhdx`/`uvm.vhd` or a boot-files root such as `rootfs.vhd`
- optionally `kernel`
- optionally `vmlinux`
- optionally `initrd` or `initrd.img`
- optionally `boot_parameters`
- `bundle.json`

This is the artifact configured for `cvd`.

## When `bake` Needs It

Windows-hosted Linux builds need two separate artifacts:

1. A Linux rootfs, documented in [ubuntu.md](ubuntu.md).
2. An LCOW UVM bundle, documented here.

The rootfs gives the container filesystem. The UVM gives the Windows host the Linux guest environment needed to boot and run that filesystem.

## Producer Notes: `mkuvm`

The current offline producer is [tools/mkuvm/main.c](../../tools/mkuvm/main.c). It supports four useful commands:

- `mkuvm normalize --source <raw-dir> --output <bundle-dir>`
- `mkuvm fetch --url <archive-url> --output <bundle-dir>`
- `mkuvm archive --source <bundle-dir> --archive <path>`
- `mkuvm construct --base-archive <base.tar.gz> --kernel <kernel> --output <bundle-dir>`

The stable flows today are `normalize`, `fetch`, `archive`, and the explicit `construct` path when you already have the upstream boot inputs.

`mkuvm normalize` now accepts both Chef's legacy LCOW bundle layout and the current upstream `hcsshim` boot-files layout, as long as the source directory contains a usable kernel plus either an initrd or a `rootfs.vhd(.x)` boot disk.

`mkuvm construct` is now an explicit assembly step for the modern boot-files flow. It takes a tar-compatible base archive plus an explicit kernel path, optionally merges an extra delta archive into the rootfs staging tree, packs `initrd.img`, and then normalizes the result into Chef's stable LCOW bundle layout. It no longer clones `hcsshim` or shells into the removed `build-lcow-uvm.sh` compatibility path.

If you already have a raw legacy bundle tree:

```powershell
mkuvm normalize --source C:\raw\lcow-uvm --output C:\temp\lcow-uvm
```

If you have a prebuilt bundle archive:

```powershell
mkuvm fetch --url https://example.invalid/windows-lcow-arm64.tar.xz --output C:\temp\lcow-uvm
```

Explicit modern construction flow from upstream-style inputs:

```powershell
mkuvm construct --base-archive C:\inputs\base.tar.gz --kernel C:\inputs\vmlinux --output C:\temp\lcow-uvm
```

If you also need to layer an extra rootfs overlay before `initrd.img` is packed:

```powershell
mkuvm construct --base-archive C:\inputs\base.tar.gz --delta-archive C:\inputs\delta.tar.gz --kernel C:\inputs\vmlinux --output C:\temp\lcow-uvm
```

If you want a transport archive as well:

```powershell
mkuvm archive --source C:\temp\lcow-uvm --archive C:\temp\lcow-uvm.zip
```

`mkuvm fetch` is the mirror image of that archive flow and downloads a tar-compatible bundle with host-native `curl` and `tar` before normalizing it.

## Importing It For Runtime Use

The runtime/import side is handled by [tools/cvctl/commands/uvm.c](../../tools/cvctl/commands/uvm.c):

- `cvctl uvm import <bundle-dir>` for a local normalized directory
- `cvctl uvm import-pack <bundle.pack>` for a Chef package
- `cvctl uvm fetch <archive-url>` for a prebuilt tar-compatible archive

This is the important distinction:

- Directories are imported with `cvctl uvm import`.
- Chef `.pack` files are imported with `cvctl uvm import-pack`.
- Tar-compatible bundle archives are fetched with `cvctl uvm fetch`.

## Example Recipe Flow

The repo's example recipe is [examples/recipes/windows/lcow-uvm.yaml](../../examples/recipes/windows/lcow-uvm.yaml). It currently runs [examples/recipes/windows/construct-lcow-uvm.sh](../../examples/recipes/windows/construct-lcow-uvm.sh) on a Linux host, installs the normalized bundle into the recipe output tree, and then lets `bake` package it.

That example remains useful because it shows how to turn the bundle into a distributable Chef package. For constructing the runtime bundle itself, prefer `mkuvm`.

## Packaging It As A Chef `.pack`

The example recipe declares:

```yaml
packs:
- name: lcow-uvm
  type: os
```

So you can build and package it with:

```bash
cd examples/recipes/windows
bake build lcow-uvm.yaml
```

The resulting `.pack` is a VaFS-backed Chef package image containing the normalized LCOW bundle. This is the managed distribution form when you want the LCOW bundle to move through Chef package workflows instead of local directories. Unlike Windows HCS bases, this one has a direct import path:

```powershell
cvctl uvm import-pack C:\packages\lcow-uvm.pack
```

## Creating A Raw VaFS Image

If you want a plain VaFS archive instead of a Chef package, pack the normalized bundle directory manually:

```bash
mkvafs --git-ignore --out lcow-uvm.vafs /tmp/lcow-uvm
```

That can be useful for ad hoc transport or inspection, but Chef's runtime-aware package flow is the Chef `.pack` path. `cvctl uvm import-pack` expects a Chef `.pack`, not a raw `.vafs` image.

## Host Requirements

`mkuvm construct` relies on external host tools:

- `tar`

On hosts where `tar` cannot emit a gzipped cpio archive directly, it also falls back to:

- `bash`
- `find`
- `cpio`
- `gzip`

It also expects you to provide the boot inputs explicitly:

- a tar-compatible base archive such as `base.tar.gz`
- a kernel path such as `vmlinux`
- optionally an extra delta archive layered into the rootfs before `initrd.img` is packed

If you only need to consume an existing bundle, you can avoid those build prerequisites and use `mkuvm normalize`, `mkuvm fetch`, or `cvctl uvm import-pack` instead.