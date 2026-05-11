#!/usr/bin/env bash
set -euo pipefail

# These are provided by the workflow environment or can be set for local testing.
# All defaults are pinned for deterministic, reproducible builds.

output_dir="${LCOW_OUTPUT_DIR:-.}"
arch="${LCOW_ARCH:-amd64}"
ubuntu_version="${LCOW_UBUNTU_VERSION:-24}"
ubuntu_release="${LCOW_UBUNTU_RELEASE:-3}"
kernel_image="${LCOW_KERNEL_IMAGE:-linuxkit/kernel:6.12.59-0ef72d722190ecfe0b3b37711f9a871a696e301a}"
hcsshim_repo="${LCOW_HCSSHIM_REPO:-https://github.com/microsoft/hcsshim.git}"
hcsshim_ref="${LCOW_HCSSHIM_REF:-main}"

tmp_root=""
kernel_ctr=""

cleanup() {
    if [[ -n "$kernel_ctr" ]]; then
        docker rm -f "$kernel_ctr" >/dev/null 2>&1 || true
    fi
    if [[ -n "$tmp_root" && -d "$tmp_root" ]]; then
        rm -rf "$tmp_root"
    fi
}
trap cleanup EXIT

case "$arch" in
    amd64|arm64)
        ;;
    *)
        echo "Unsupported architecture: $arch" >&2
        exit 1
        ;;
esac

required_tools=(curl docker git gzip make tar)
for tool in "${required_tools[@]}"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Required tool not found: $tool" >&2
        exit 1
    fi
done

mkdir -p "$output_dir"
tmp_root="$(mktemp -d)"
hcsshim_dir="$tmp_root/hcsshim"

echo "=== Producing LCOW upstream inputs ==="
echo "Architecture: $arch"
echo "Ubuntu version: $ubuntu_version (release $ubuntu_release)"
echo "LinuxKit kernel: $kernel_image"
echo "hcsshim ref: $hcsshim_ref"
echo ""

# Download base archive from Ubuntu CDImage (consistent with Linux container setup)
echo "Downloading base archive from cdimage.ubuntu.com"
base_archive_url="https://cdimage.ubuntu.com/ubuntu-base/releases/${ubuntu_version}.04/release/ubuntu-base-${ubuntu_version}.04.${ubuntu_release}-base-${arch}.tar.gz"
base_archive_file="$output_dir/base.tar.gz"

if ! curl -fSL --progress-bar --output "$base_archive_file" "$base_archive_url"; then
    echo "Failed to download base archive from $base_archive_url" >&2
    exit 1
fi
echo "  Downloaded: $base_archive_file"

# Clone hcsshim and build delta
echo "Cloning $hcsshim_repo at $hcsshim_ref"
git init "$hcsshim_dir" >/dev/null 2>&1
(
    cd "$hcsshim_dir"
    git remote add origin "$hcsshim_repo" >/dev/null 2>&1
    git fetch --depth 1 origin "$hcsshim_ref" >/dev/null 2>&1
    git checkout --detach FETCH_HEAD >/dev/null 2>&1
)

echo "Building upstream delta archive from hcsshim"
(
    cd "$hcsshim_dir"
    make out/delta.tar.gz >/dev/null 2>&1
)
cp "$hcsshim_dir/out/delta.tar.gz" "$output_dir/delta.tar.gz"
echo "  Built: $output_dir/delta.tar.gz"

echo "Extracting kernel artifact from $kernel_image"
docker pull --platform "linux/$arch" "$kernel_image" >/dev/null 2>&1
kernel_ctr="$(docker create --platform "linux/$arch" "$kernel_image" /bin/sh)"
if docker cp "$kernel_ctr":/kernel "$output_dir/kernel" >/dev/null 2>&1; then
    echo "  Extracted: /kernel"
elif docker cp "$kernel_ctr":/vmlinux "$output_dir/kernel" >/dev/null 2>&1; then
    echo "  Extracted: /vmlinux"
else
    echo "Failed to extract /kernel or /vmlinux from $kernel_image" >&2
    exit 1
fi
docker rm -f "$kernel_ctr" >/dev/null 2>&1 || true
kernel_ctr=""

cat > "$output_dir/source.env" <<EOF
ARCH=$arch
UBUNTU_VERSION=$ubuntu_version
UBUNTU_RELEASE=$ubuntu_release
KERNEL_IMAGE=$kernel_image
HCSHIM_REPO=$hcsshim_repo
HCSHIM_REF=$hcsshim_ref
EOF

echo ""
echo "✓ Successfully produced upstream inputs"
echo "  - $base_archive_file"
echo "  - $output_dir/delta.tar.gz"
echo "  - $output_dir/kernel"
