#!/usr/bin/env bash
# signing.sh — helpers for isolated package-signing setup in system tests
#
# Source this file after env.sh.

signing_keygen_binary() {
    if command -v ssh-keygen >/dev/null 2>&1; then
        command -v ssh-keygen
        return 0
    fi

    if command -v openssh-keygen >/dev/null 2>&1; then
        command -v openssh-keygen
        return 0
    fi

    echo "ERROR: ssh-keygen (or openssh-keygen) is required for signing tests" >&2
    return 1
}

# Configure an isolated signing identity for bake sign.
# Usage: setup_test_signing_identity ROOT_DIR KEY_PATH [NAME] [EMAIL]
setup_test_signing_identity() {
    local root_dir="${1:-}"
    local key_path="${2:-}"
    local signer_name="${3:-Github CI}"
    local signer_email="${4:-bake-ci@github.com}"
    local keygen

    if [[ -z "$root_dir" || -z "$key_path" ]]; then
        echo "ERROR: setup_test_signing_identity requires ROOT_DIR and KEY_PATH" >&2
        return 1
    fi

    if [[ -z "${CMD_ORDER:-}" || ! -x "$CMD_ORDER" ]]; then
        echo "ERROR: order binary not found or not executable: ${CMD_ORDER:-<unset>}" >&2
        return 1
    fi

    keygen="$(signing_keygen_binary)" || return 1

    mkdir -p "$root_dir" "$(dirname "$key_path")"

    if [[ -e "$key_path" && ! -e "${key_path}.pub" ]]; then
        echo "ERROR: private key exists without matching public key: $key_path" >&2
        return 1
    fi

    if [[ -e "${key_path}.pub" && ! -e "$key_path" ]]; then
        echo "ERROR: public key exists without matching private key: ${key_path}.pub" >&2
        return 1
    fi

    if [[ ! -e "$key_path" ]]; then
        if ! "$keygen" -q -m PEM -t rsa -b 4096 -f "$key_path" -N "" >/dev/null 2>&1; then
            echo "ERROR: failed to generate RSA keypair at $key_path" >&2
            return 1
        fi
    fi

    # ssh-keygen always writes the public key in OpenSSH format, which served cannot parse.
    if ! head -n 1 "${key_path}.pub" | grep -q -- "-----BEGIN PUBLIC KEY-----"; then
        if ! "$keygen" -e -m PKCS8 -f "$key_path" > "${key_path}.pub.pem" 2>/dev/null; then
            echo "ERROR: failed to convert public key to PEM at ${key_path}.pub" >&2
            rm -f "${key_path}.pub.pem"
            return 1
        fi
        mv "${key_path}.pub.pem" "${key_path}.pub"
    fi

    if ! "$CMD_ORDER" --root "$root_dir" config auth.name "$signer_name" >/dev/null; then
        echo "ERROR: failed to configure signing name in $root_dir" >&2
        return 1
    fi

    if ! "$CMD_ORDER" --root "$root_dir" config auth.email "$signer_email" >/dev/null; then
        echo "ERROR: failed to configure signing email in $root_dir" >&2
        return 1
    fi

    if ! "$CMD_ORDER" --root "$root_dir" config auth.key "$key_path" >/dev/null; then
        echo "ERROR: failed to configure signing key in $root_dir" >&2
        return 1
    fi

    return 0
}

# Generate an isolated RSA keypair for the fake store.
# Usage: setup_test_store_signing_identity ROOT_DIR KEY_PATH
setup_test_store_signing_identity() {
    local root_dir="${1:-}"
    local key_path="${2:-}"

    if [[ -z "$root_dir" || -z "$key_path" ]]; then
        echo "ERROR: setup_test_store_signing_identity requires ROOT_DIR and KEY_PATH" >&2
        return 1
    fi
    if ! command -v openssl >/dev/null 2>&1; then
        echo "ERROR: openssl is required for fake-store signing" >&2
        return 1
    fi

    mkdir -p "$root_dir" "$(dirname "$key_path")"
    if [[ ! -e "$key_path" ]]; then
        if ! openssl genrsa -out "$key_path" 4096 >/dev/null 2>&1; then
            echo "ERROR: failed to generate fake-store private key: $key_path" >&2
            return 1
        fi
    fi

    if ! openssl pkey -in "$key_path" -pubout -out "${key_path}.pub" >/dev/null 2>&1; then
        echo "ERROR: failed to derive fake-store public key: ${key_path}.pub" >&2
        return 1
    fi
}

# Create a publisher-origin proof using the fake store keypair.
# Usage: create_test_store_proof PACK_FILE PROOF_FILE PRIVATE_KEY PUBLIC_KEY PACKAGE_NAME
create_test_store_proof() {
    local pack_file="${1:-}"
    local proof_file="${2:-}"
    local private_key="${3:-}"
    local public_key="${4:-}"
    local package_name="${5:-}"
    local temp_dir
    local digest_file
    local hash
    local encoded_public_key
    local signature

    if [[ -z "$pack_file" || -z "$proof_file" || -z "$private_key" ||
        -z "$public_key" || -z "$package_name" ]]; then
        echo "ERROR: create_test_store_proof requires pack, proof, keys, and package name" >&2
        return 1
    fi
    for required_file in "$pack_file" "$private_key" "$public_key"; do
        if [[ ! -f "$required_file" ]]; then
            echo "ERROR: create_test_store_proof: file not found: $required_file" >&2
            return 1
        fi
    done

    temp_dir=$(mktemp -d) || return 1
    digest_file="$temp_dir/digest"

    if ! openssl dgst -sha512 -binary "$pack_file" >"$digest_file"; then
        rm -rf "$temp_dir"
        return 1
    fi
    hash=$(base64 --wrap=0 "$digest_file") || {
        rm -rf "$temp_dir"
        return 1
    }
    encoded_public_key=$(base64 --wrap=0 "$public_key") || {
        rm -rf "$temp_dir"
        return 1
    }
    signature=$(openssl dgst -sha512 -sign "$private_key" "$digest_file" | base64 --wrap=0) || {
        rm -rf "$temp_dir"
        return 1
    }

    cat >"$proof_file" <<EOF
{
  "origin": "publisher",
  "identity": "fake-store",
  "package": "$package_name",
  "hash-algorithm": "sha512",
  "hash": "$hash",
  "public-key": "$encoded_public_key",
  "signature": "$signature"
}
EOF
    rm -rf "$temp_dir"
}