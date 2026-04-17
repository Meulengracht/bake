# LCOW UVM Bundle

## What This Artifact Is

The LCOW base is not a root filesystem by itself. It is the Linux utility VM bundle required to run Linux containers on a Windows host.

A normalized LCOW bundle contains:

- `uvm.vhdx`
- optionally `kernel`
- optionally `initrd` or `initrd.img`
- optionally `boot_parameters`
- `bundle.json`

This is the artifact configured into `cvd` and imported with `cvctl uvm ...`.

## When `bake` Needs It

Windows-hosted Linux builds need two separate artifacts:

1. A Linux rootfs, documented in [ubuntu.md](ubuntu.md).
2. An LCOW UVM bundle, documented here.

The rootfs gives the container filesystem. The UVM gives the Windows host the Linux guest environment needed to boot and run that filesystem.

## Recommended Producer: `mkuvm`

The current offline producer is [tools/mkuvm/main.c](../../tools/mkuvm/main.c). It supports four useful commands:

- `mkuvm normalize --source <raw-dir> --output <bundle-dir>`
- `mkuvm fetch --url <zip-url> --output <bundle-dir>`
- `mkuvm archive --source <bundle-dir> --archive <path>`
- `mkuvm construct --output <bundle-dir>`

Typical construction flow:

```powershell
mkuvm construct --output C:\temp\lcow-uvm
cvctl uvm import C:\temp\lcow-uvm
```

If you already have a raw bundle tree:

```powershell
mkuvm normalize --source C:\raw\lcow-uvm --output C:\temp\lcow-uvm
cvctl uvm import C:\temp\lcow-uvm
```

If you want a zip transport artifact as well:

```powershell
mkuvm archive --source C:\temp\lcow-uvm --archive C:\temp\lcow-uvm.zip
```

`mkuvm fetch` is the mirror image of that archive flow and downloads a zipped bundle with host-native `curl` and `tar` before normalizing it.

## Importing It For Runtime Use

The runtime/import side is handled by [tools/cvctl/commands/uvm.c](../../tools/cvctl/commands/uvm.c):

- `cvctl uvm import <bundle-dir>` for a local normalized directory
- `cvctl uvm import-pack <bundle.pack>` for a Chef package
- `cvctl uvm fetch <zip-url>` for a prebuilt zip archive

This is the important distinction:

- Directories are imported with `cvctl uvm import`.
- Chef `.pack` files are imported with `cvctl uvm import-pack`.
- Zip archives are fetched with `cvctl uvm fetch`.

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

`mkuvm construct` and the example script both rely on external host tools:

- `git`
- `bash`
- `linuxkit`
- an `hcsshim` checkout, either supplied via `--hcsshim-dir` or cloned automatically by `mkuvm`

If you only need to consume an existing bundle, you can avoid those build prerequisites and use `mkuvm normalize`, `mkuvm fetch`, or `cvctl uvm import-pack` instead.