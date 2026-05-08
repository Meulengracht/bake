#!/usr/bin/env bash
set -euo pipefail

mkuvm_path=""
output_dir=""
archive_path=""
base_archive=""
delta_archive=""
kernel_path=""
arch="amd64"
bash_bin="bash"
working_dir=""

while (($# > 0)); do
    case "$1" in
        --mkuvm-path)
            mkuvm_path="$2"
            shift 2
            ;;
        --output-dir)
            output_dir="$2"
            shift 2
            ;;
        --archive-path)
            archive_path="$2"
            shift 2
            ;;
        --base-archive)
            base_archive="$2"
            shift 2
            ;;
        --delta-archive)
            delta_archive="$2"
            shift 2
            ;;
        --kernel-path)
            kernel_path="$2"
            shift 2
            ;;
        --arch)
            arch="$2"
            shift 2
            ;;
        --bash-bin)
            bash_bin="$2"
            shift 2
            ;;
        --working-directory)
            working_dir="$2"
            shift 2
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 1
            ;;
    esac
done

if [[ -z "$mkuvm_path" || -z "$output_dir" || -z "$archive_path" || -z "$base_archive" || -z "$kernel_path" ]]; then
    echo "Required arguments: --mkuvm-path, --output-dir, --archive-path, --base-archive, --kernel-path" >&2
    exit 1
fi

if [[ ! -f "$mkuvm_path" ]]; then
    echo "mkuvm executable not found: $mkuvm_path" >&2
    exit 1
fi

if ! command -v tar >/dev/null 2>&1; then
    echo "tar is required to archive the LCOW UVM bundle" >&2
    exit 1
fi

if [[ ! -f "$base_archive" ]]; then
    echo "Base archive not found: $base_archive" >&2
    exit 1
fi

if [[ -n "$delta_archive" && ! -f "$delta_archive" ]]; then
    echo "Delta archive not found: $delta_archive" >&2
    exit 1
fi

if [[ ! -f "$kernel_path" ]]; then
    echo "Kernel path not found: $kernel_path" >&2
    exit 1
fi

if [[ "$bash_bin" == */* ]]; then
    if [[ ! -x "$bash_bin" ]]; then
        echo "bash executable is not executable: $bash_bin" >&2
        exit 1
    fi
elif ! command -v "$bash_bin" >/dev/null 2>&1; then
    echo "bash command not found: $bash_bin" >&2
    exit 1
fi

mkdir -p "$(dirname "$output_dir")"
mkdir -p "$(dirname "$archive_path")"

construct_args=(
    construct
    --output "$output_dir"
    --archive "$archive_path"
    --base-archive "$base_archive"
    --kernel "$kernel_path"
    --arch "$arch"
    --bash-bin "$bash_bin"
    --force
)

if [[ -n "$delta_archive" ]]; then
    construct_args+=(--delta-archive "$delta_archive")
fi

if [[ -n "$working_dir" ]]; then
    mkdir -p "$working_dir"
    construct_args+=(--working-directory "$working_dir")
fi

echo "Running mkuvm ${construct_args[*]}"
"$mkuvm_path" "${construct_args[@]}"

expected_paths=(
    "$output_dir/bundle.json"
    "$output_dir/kernel"
    "$output_dir/initrd"
    "$archive_path"
)

for expected_path in "${expected_paths[@]}"; do
    if [[ ! -e "$expected_path" ]]; then
        echo "Expected output is missing: $expected_path" >&2
        exit 1
    fi
done

echo "Constructed LCOW UVM bundle at $output_dir"
echo "Created LCOW UVM archive at $archive_path"

if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
    {
        echo "archive_path=$archive_path"
        echo "output_dir=$output_dir"
    } >> "$GITHUB_OUTPUT"
fi