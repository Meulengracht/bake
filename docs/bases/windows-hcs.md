# Windows HCS Base

## What This Artifact Is

The Windows HCS base is the runtime base used by true Windows containers on a Windows host. Chef expects it as a normalized directory containing at least:

- `windowsfilter/`
- `windowsfilter/layerchain.json`
- `base.json`
- optionally `UtilityVM/`

This is the artifact behind logical bases such as `windows:ltsc2022`, which you can see used in [examples/recipes/hello.yaml](../../examples/recipes/hello.yaml).

## Recommended Producer: `mkwbase`

The current standalone producer is [tools/mkwbase/main.c](../../tools/mkwbase/main.c). It supports two flows:

- `mkwbase construct` to build a normalized base from a Docker or MCR Windows image
- `mkwbase normalize` to normalize an already extracted Windows base directory

Typical flow for the default LTSC 2022 Windows base:

```powershell
mkwbase construct --base windows:ltsc2022 --image mcr.microsoft.com/windows:ltsc2022 --output C:\temp\windows-base
bakectl base import windows:ltsc2022 C:\temp\windows-base
```

If you already have the raw extracted layer tree:

```powershell
mkwbase normalize --base windows:ltsc2022 --source C:\raw\windows-base --output C:\temp\windows-base
bakectl base import windows:ltsc2022 C:\temp\windows-base
```

`mkwbase` handles the awkward bits that the old scripts used to do manually:

- locating the writable `windowsfilter` layer
- copying and rewriting parent layers into a local `parents/` folder
- copying `UtilityVM` if present
- writing `base.json`

## Importing It For Runtime Use

Once the normalized directory exists, register it with [tools/bakectl/commands/base.c](../../tools/bakectl/commands/base.c):

```powershell
bakectl base import windows:ltsc2022 C:\temp\windows-base
```

After that, recipes that target `base: windows:ltsc2022` can use the registered base when `bake` asks `cvd` to create a Windows build container.

## Example Recipe Flow

The repository still contains example packaging recipes that show the older inline construction flow:

- [examples/recipes/windows/windows.yaml](../../examples/recipes/windows/windows.yaml)
- [examples/recipes/windows/servercore.yaml](../../examples/recipes/windows/servercore.yaml)
- [examples/recipes/windows/nanoserver.yaml](../../examples/recipes/windows/nanoserver.yaml)

Those recipes pull a Docker image, inspect its graph driver, copy the current layer into `windowsfilter`, rewrite the parent chain into `parents`, and copy `UtilityVM` when present.

That is effectively the recipe-level equivalent of what `mkwbase construct` now does as a dedicated producer utility. The recipes are still valuable as packaging examples, but for creating the runtime base itself, prefer `mkwbase`.

## Packaging It As A Chef `.pack`

The Windows example recipes all declare a `packs:` section, for example:

```yaml
packs:
- name: windows
  type: ingredient
```

So you can package one with:

```powershell
cd examples/recipes/windows
bake build windows.yaml
```

Or, for Server Core and Nano Server:

```powershell
bake build servercore.yaml
bake build nanoserver.yaml
```

The resulting `.pack` file is a VaFS-backed Chef package image that contains the staged Windows base directory. This is the primary form used when `served` is responsible for supplying runtime bases to `cvd` and `containerv`.

That managed runtime flow looks like this:

1. An application declares `platforms[].base`, for example `windows:ltsc2022`.
2. `bake` writes that base into the application package manifest.
3. `served` resolves and installs the matching Windows base package before the application.
4. `served` hands the base `.pack` and application `.pack` to `cvd`.
5. `containerv` materializes those VaFS package layers into the final Windows container rootfs.

## Importing A Packaged Windows Base

Unlike LCOW bundles, Windows HCS bases do not currently have a direct `bakectl base import-pack` flow.

Today the supported administrative path is:

1. Unpack the VaFS-backed `.pack` image back to a directory.
2. Import that directory with `bakectl base import`.

For example:

```powershell
unmkvafs --out C:\temp\windows-base C:\packages\windows-base.pack
bakectl base import windows:ltsc2022 C:\temp\windows-base
```

## Creating A Raw VaFS Image

If you only want a plain filesystem archive without Chef package metadata, pack the normalized base directory directly:

```powershell
mkvafs --git-ignore --out C:\temp\windows-base.vafs C:\temp\windows-base
```

That can be useful for transport or inspection, but the managed runtime path is the Chef `.pack` flow via `served`, while the administrative runtime-registration path is still the normalized directory plus `bakectl base import`.

## Choosing The Logical Base Name

Keep the logical name you construct, package, and import aligned with the name your recipes use in `platforms[].base`.

Examples:

- `windows:ltsc2022`
- `windows:servercore-ltsc2022`
- `windows:nanoserver-ltsc2022`

The exact package name can differ, but the imported logical base should stay consistent with what your recipes request.