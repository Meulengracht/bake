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