# Base Images

Chef uses three different base-artifact families for build containers and runtime-base delivery:

| Artifact | Used for | Recommended producer | Runtime/import consumer | Example recipe |
| --- | --- | --- | --- | --- |
| Ubuntu rootfs | Linux build containers, plus LCOW rootfs mapping on Windows | Built-in Ubuntu fetch for `ubuntu:24`, or the Chisel-based example recipes | `bake` Linux containers and Windows LCOW rootfs mapping | [examples/recipes/linux/base.yaml](../../examples/recipes/linux/base.yaml), [examples/recipes/windows/linux.yaml](../../examples/recipes/windows/linux.yaml) |
| LCOW UVM bundle | Windows-hosted Linux containers | `mkuvm` | `cvctl uvm import`, `cvctl uvm import-pack`, `cvctl uvm fetch` | [examples/recipes/windows/lcow-uvm.yaml](../../examples/recipes/windows/lcow-uvm.yaml) |
| Windows HCS base | Windows-hosted Windows containers | `mkwbase` | `bakectl base import` | [examples/recipes/windows/windows.yaml](../../examples/recipes/windows/windows.yaml), [examples/recipes/windows/servercore.yaml](../../examples/recipes/windows/servercore.yaml), [examples/recipes/windows/nanoserver.yaml](../../examples/recipes/windows/nanoserver.yaml) |

The consumer side is easiest to see in [examples/recipes/hello.yaml](../../examples/recipes/hello.yaml): Linux builds target `base: ubuntu:24`, and Windows builds target `base: windows:ltsc2022`.

That `platforms[].base` value is not only a build-time hint. `bake` records it in the package manifest, `served` uses it to resolve and install the matching base package before the application, and the runtime path then hands the base package to `cvd` and `containerv` as a runtime layer.

## Directory, Pack, and VaFS Forms

Each base can exist in more than one form:

1. A normalized directory or bundle.
   This is what the runtime tools consume directly.
   Examples: a Linux rootfs directory, an LCOW bundle containing `uvm.vhdx`, or a Windows base directory containing `windowsfilter`.
2. A Chef `.pack` file.
   This is the format produced by `bake` when a recipe declares a `packs:` section. `.pack` files are built on top of VaFS, add Chef package metadata, and are the primary format used by `served` to deliver runtime bases to `cvd` and `containerv`.
3. A raw `.vafs` image.
   This is optional and is mainly useful when you want a plain filesystem archive without Chef package metadata.

The important distinction is that Chef runtime tools do not all consume the same format:

- `cvctl uvm import` and `bakectl base import` expect normalized directories.
- `cvctl uvm import-pack` accepts a Chef `.pack` directly.
- There is no `bakectl base import-pack` today, so Windows HCS base packs must be unpacked back to a directory first.

For application runtime, the normal managed flow is:

1. The application recipe declares `platforms[].base`.
2. `bake` writes that base into the `.pack` manifest.
3. `served` resolves the matching base package during dependency handling.
4. `served` passes the base package and application package to `cvd`.
5. `cvd` maps those `.pack` files into containerv as VaFS-backed layers.

## Runtime Sequence

The short version of the runtime path is:

1. A recipe declares the required platform base in `platforms[].base`, for example `ubuntu:24` or `windows:ltsc2022`.
2. `bake` emits a Chef `.pack` whose manifest records that base requirement.
3. `served` reads the manifest, resolves the matching base package, and installs that dependency before the application.
4. `served` asks `cvd` to create the runtime container using the base package plus the application package.
5. `containerv` composes those layers into the final runtime filesystem.

Useful background docs:

- [daemons/served/README.md](../../daemons/served/README.md) for the package-install and dependency state machine.
- [libs/containerv/README.md](../../libs/containerv/README.md) for the container-side base and layer contracts.

The producer tools documented below are the administrative side of that same system: they let you construct the normalized base artifacts that eventually become the base packages `served` distributes or the local assets imported into Chef caches.

If you only need a raw VaFS archive, use `mkvafs` directly:

```bash
mkvafs --git-ignore --out base.vafs <normalized-base-dir>
```

If you want the artifact form that Chef runtime services actually manage and distribute, use a recipe with `packs:` and run `bake build ...` so Chef creates a `.pack` image with the correct metadata.

## Current Recommendation

Use the dedicated producer tools to construct runtime-ready base artifacts first:

- Use `mkwbase` for Windows HCS bases.
- Use `mkuvm` for LCOW UVM bundles.
- Use the built-in Ubuntu fetch path for plain Linux runtime resolution, or the example recipes when you want a redistributable Ubuntu rootfs pack.

The older example recipes under [examples/recipes/windows](../../examples/recipes/windows) are still useful because they show what content gets staged into the final package, but the current standalone producer flow is more direct when you only need the runtime base itself.

## Guides

- [Ubuntu base guide](ubuntu.md)
- [LCOW UVM guide](lcow.md)
- [Windows HCS base guide](windows-hcs.md)