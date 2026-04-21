<h1 align="center" style="margin-top: 0px;">Proof Format</h1>

This document specifies the proof file written by `bake sign <package-file>`.

A proof is written next to the package file as `<package-file>.proof`. For example,
signing `hello-world.pack` produces `hello-world.pack.proof`.

This format is **not** the same as the store proof payload returned by `GET /package/proof`
in [store-api.md](store-api.md). Store proofs are raw binary payloads. This document only
describes the local JSON `.proof` file emitted by `bake sign`.

## File Encoding

- The file is a single JSON object.
- The current writer emits ASCII-compatible UTF-8 text with two-space indentation and a
  trailing newline.
- Member order is not semantically significant, although the current writer emits the members
  in the order shown below.
- The current writer inserts `identity` and `package` directly with `fprintf(...)` and does
  not JSON-escape them. In practice those values must already be valid JSON string content.

## Base64 Encoding

The `hash`, `public-key`, and `signature` members use the OpenSSL `EVP_EncodeBlock` output
format:

- standard RFC 4648 character set (`A-Z`, `a-z`, `0-9`, `+`, `/`)
- `=` padding when required
- no embedded newlines

## File Name

Given a package path `PACK`, the proof path is `PACK.proof`.

Examples:

- `foo.pack` -> `foo.pack.proof`
- `artifacts/linux/foo.pack` -> `artifacts/linux/foo.pack.proof`

## Top-Level Members

| Member | Type | Generated value | Required by current local loader | Notes |
| --- | --- | --- | --- | --- |
| `origin` | string | Always `"developer"` | No | Informational today. `served` sets the in-memory origin to developer without reading this field. |
| `identity` | string | Configured developer identity. The current writer uses the configured email address. | Yes | `served` uses this value as the publisher identity during local install staging. |
| `package` | string | Package name from the package manifest (`manifest->name`) | No | Informational today. The current local loader does not read or validate it. |
| `hash-algorithm` | string | Always `"sha512"` | Yes | The current verifier accepts only `sha512`. |
| `hash` | string | Base64 of the raw 64-byte SHA-512 digest of the package file | Yes | This is base64 of binary digest bytes, not hex and not base64 of text. |
| `public-key` | string | Base64 of the entire public key file contents | Yes | Usually this is base64 of a PEM public key file, which means the field contains base64-of-PEM-text. |
| `signature` | string | Base64 of the OpenSSL signature output | Yes | The signature covers the raw package digest bytes as described below. |

## Canonical JSON Shape

```json
{
  "origin": "developer",
  "identity": "developer@example.com",
  "package": "hello-world",
  "hash-algorithm": "sha512",
  "hash": "<base64 of 64 raw SHA-512 bytes>",
  "public-key": "<base64 of the public key file bytes>",
  "signature": "<base64 of the signature bytes>"
}
```

## Generation Algorithm

Given:

- `package-file`: the `.pack` artifact being signed
- `public-key-file`: the configured public key path
- `private-key-file`: the configured PEM private key path

`bake sign` generates the payload as follows:

```text
package_hash = SHA512(bytes(package-file))
hash = Base64(package_hash)

public_key_bytes = bytes(public-key-file)
public-key = Base64(public_key_bytes)

signature_bytes = OpenSSL EVP_DigestSign(
    key = private-key-file,
    digest = SHA512,
    message = package_hash
)
signature = Base64(signature_bytes)
```

Important compatibility details:

- The signature input is the raw 64-byte package digest.
- The signature is not computed over the base64 text in `hash`.
- The signature is not computed over the package bytes directly.
- Because `EVP_DigestSign*` is used with `EVP_sha512()`, OpenSSL hashes the 64-byte digest
  input again as part of the signature operation. Interoperable implementations must reproduce
  that exact call pattern.
- The file does not carry an explicit key algorithm or signature scheme field. Consumers infer
  that from the embedded public key and their cryptographic implementation.

## Current implementation

The current local proof loader and verifier in `served` behave as follows:

- require `identity`, `hash-algorithm`, `hash`, `public-key`, and `signature`
- ignore `origin` and `package`
- reject any `hash-algorithm` other than `sha512`
- base64-decode `hash` and `signature`
- recompute SHA-512 over the package bytes and compare the raw digest against `hash`

Compatibility note:

- The current local verifier passes `public-key` directly to a PEM parser instead of
  base64-decoding it first.
- That behavior does not match the writer, which base64-encodes the full public key file
  contents.
- Treat the on-disk format defined above as authoritative for newly generated `.proof` files;
  the verifier discrepancy is an implementation gap.
