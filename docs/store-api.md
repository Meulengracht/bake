# Chef Store API

This document describes the HTTP API exposed by the Chef store backend. It is derived from `libs/chefclient` and is intended to serve as a reference for implementing a fake store backend for testing as well as for building new integrations.

## Overview

| Property | Value |
|---|---|
| Default base URL | `https://chef-api.meulen.io` |
| Transport | HTTPS (configurable at build time via `CHEF_CLIENT_API_URL` and `CHEF_CLIENT_API_SECURE`) |
| Request content type | `application/json` (unless noted otherwise) |
| Response content type | `application/json` (unless noted otherwise) |

---

## Authentication

Endpoints marked **🔒 Auth required** expect a bearer token in the `Authorization` header:

```
Authorization: Bearer <token>
```

The token is obtained via one of two login flows:

| Flow | Description |
|---|---|
| OAuth2 Device Code | Interactive browser-based login (see `libs/chefclient/oauth/`) |
| Public Key | RSA key-based authentication (see `libs/chefclient/pubkey/`) |
| API Key | A generated API key obtained via `POST /account/api-keys` |

Unauthenticated requests to protected endpoints receive **401 Unauthorized**.

---

## Common HTTP Status Codes

| Code | Meaning |
|---|---|
| `200 OK` | Request succeeded |
| `200–299` | Success range (used by update/delete endpoints) |
| `401 Unauthorized` | Missing or invalid authentication token |
| `404 Not Found` | Requested resource does not exist |
| `5xx` | Server error |

---

## Account Endpoints

### GET /account/me

Retrieve the authenticated user's account information.

**🔒 Auth required**

#### Response `200 OK`

```json
{
  "name": "string",
  "email": "string",
  "status": 1,
  "publishers": [
    {
      "name": "string",
      "email": "string",
      "public-key": "string",
      "signed-key": "string",
      "status": 3
    }
  ],
  "api-keys": [
    {
      "name": "string"
    }
  ]
}
```

| Field | Type | Description |
|---|---|---|
| `name` | string | Display name of the account |
| `email` | string | Email address of the account |
| `status` | integer | Account status (see [Account Status](#account-status)) |
| `publishers` | array | List of publisher profiles associated with the account |
| `publishers[].name` | string | Publisher name |
| `publishers[].email` | string | Publisher contact email |
| `publishers[].public-key` | string | Publisher's RSA public key |
| `publishers[].signed-key` | string | Store-signed version of the public key |
| `publishers[].status` | integer | Publisher verification status (see [Publisher Verification Status](#publisher-verification-status)) |
| `api-keys` | array | List of named API keys associated with the account |
| `api-keys[].name` | string | Label for the API key (the secret value is not returned here) |

---

### POST /account/me

Update the authenticated user's account information.

**🔒 Auth required**

#### Request body

```json
{
  "name": "string"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `name` | string | Yes | New display name for the account |

#### Response `200 OK`

Returns the updated account object (same schema as `GET /account/me`).

---

### GET /account/publisher

Retrieve information about a specific publisher by name.

**🔒 Auth required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | Yes | The publisher name to look up |

#### Response `200 OK`

```json
{
  "name": "string",
  "email": "string",
  "public-key": "string",
  "signed-key": "string",
  "status": 3
}
```

| Field | Type | Description |
|---|---|---|
| `name` | string | Publisher name |
| `email` | string | Publisher contact email |
| `public-key` | string | Publisher's RSA public key |
| `signed-key` | string | Store-signed version of the public key |
| `status` | integer | Publisher verification status (see [Publisher Verification Status](#publisher-verification-status)) |

#### Error responses

| Code | Condition |
|---|---|
| `404` | Publisher not found |

---

### POST /account/publishers

Register a new publisher profile under the authenticated account.

**🔒 Auth required**

#### Request body

```json
{
  "PublisherName": "string",
  "PublisherEmail": "string"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `PublisherName` | string | Yes | Desired publisher name |
| `PublisherEmail` | string | Yes | Publisher contact email |

#### Response `200–299`

```json
{
  "message": "string",
  "publisherId": "string"
}
```

| Field | Type | Description |
|---|---|---|
| `message` | string | Human-readable status message |
| `publisherId` | string | Optional identifier assigned to the new publisher |

---

### POST /account/api-keys

Create a new API key for programmatic access.

**🔒 Auth required**

#### Request body

```json
{
  "Name": "string"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `Name` | string | Yes | A label for the new API key |

#### Response `200 OK`

```json
{
  "key": "string"
}
```

| Field | Type | Description |
|---|---|---|
| `key` | string | The generated API key value. This is the only time the full key is returned; store it securely. |

---

### DELETE /account/api-keys

Revoke and remove an existing API key.

**🔒 Auth required**

#### Request body

```json
{
  "Name": "string"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `Name` | string | Yes | The label of the API key to delete |

#### Response `200–299`

Empty body on success.

---

## Package Query Endpoints

### GET /package/find

Search for packages in the store.

**Auth optional** — when an authorization token is provided, results may include private/privileged packages.

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `search` | string | Yes | Free-text search query |

#### Response `200 OK`

Returns a JSON array of matching packages:

```json
[
  {
    "publisher": "string",
    "name": "string",
    "summary": "string",
    "type": 1,
    "maintainer": "string",
    "maintainer-email": "string"
  }
]
```

| Field | Type | Description |
|---|---|---|
| `publisher` | string | Publisher/owner of the package |
| `name` | string | Package name |
| `summary` | string | Short description |
| `type` | integer | Package type (see [Package Type](#package-type)) |
| `maintainer` | string | Maintainer's name |
| `maintainer-email` | string | Maintainer's email address |

Returns an empty array `[]` when no packages match the query.

#### Error responses

| Code | Condition |
|---|---|
| `404` | No packages found (alternative to empty array; implementations may vary) |

---

### GET /package/info

Retrieve detailed metadata for a specific package, including all published revisions.

**Auth not required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `publisher` | string | Yes | The publisher/owner of the package |
| `name` | string | Yes | The package name |

#### Response `200 OK`

```json
{
  "publisher": "string",
  "name": "string",
  "type": 1,
  "summary": "string",
  "description": "string",
  "homepage": "string",
  "license-spdx": "string",
  "eula": "string",
  "maintainer": "string",
  "maintainer-email": "string",
  "revisions": [
    {
      "channel": "string",
      "platform": "string",
      "architecture": "string",
      "size": 1048576,
      "date": "string",
      "version": {
        "revision": 42,
        "major": 1,
        "minor": 0,
        "patch": 0,
        "tag": "string"
      }
    }
  ]
}
```

| Field | Type | Description |
|---|---|---|
| `publisher` | string | Publisher/owner of the package |
| `name` | string | Package name |
| `type` | integer | Package type (see [Package Type](#package-type)) |
| `summary` | string | Short description |
| `description` | string | Full description |
| `homepage` | string | Package homepage URL |
| `license-spdx` | string | SPDX license identifier (e.g. `MIT`, `Apache-2.0`) |
| `eula` | string | End-User License Agreement URL |
| `maintainer` | string | Maintainer's name |
| `maintainer-email` | string | Maintainer's email |
| `revisions` | array | All published revisions for this package |
| `revisions[].channel` | string | Release channel (e.g. `stable`, `dev`) |
| `revisions[].platform` | string | Target platform (e.g. `linux`, `windows`) |
| `revisions[].architecture` | string | Target architecture (e.g. `amd64`, `arm64`) |
| `revisions[].size` | integer | Package file size in bytes |
| `revisions[].date` | string | Publication date/timestamp |
| `revisions[].version.revision` | integer | Monotonically increasing revision number assigned by the store |
| `revisions[].version.major` | integer | Major version number |
| `revisions[].version.minor` | integer | Minor version number |
| `revisions[].version.patch` | integer | Patch version number |
| `revisions[].version.tag` | string | Optional pre-release/build tag |

#### Error responses

| Code | Condition |
|---|---|
| `404` | Package not found |

---

### POST /package/info

Update the metadata for an existing package.

**🔒 Auth required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `publisher` | string | Yes | The publisher/owner of the package |
| `name` | string | Yes | The package name |

#### Request body

All fields are optional; only provided fields are updated.

```json
{
  "Summary": "string",
  "Description": "string",
  "Type": 1,
  "HomepageUrl": "string",
  "LicenseSPDX": "string",
  "EulaUrl": "string",
  "Maintainer": "string",
  "MaintainerEmail": "string"
}
```

| Field | Type | Description |
|---|---|---|
| `Summary` | string | Short description |
| `Description` | string | Full description |
| `Type` | integer | Package type (see [Package Type](#package-type)) |
| `HomepageUrl` | string | Package homepage URL |
| `LicenseSPDX` | string | SPDX license identifier |
| `EulaUrl` | string | EULA URL |
| `Maintainer` | string | Maintainer's name |
| `MaintainerEmail` | string | Maintainer's email |

#### Response `200–299`

Empty body on success.

---

### GET /package/revision

Resolve the latest revision number for a given package/platform/channel combination.

**Auth not required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `publisher` | string | Yes | The publisher/owner of the package |
| `name` | string | Yes | The package name |
| `platform` | string | Yes | Target platform (e.g. `linux`, `windows`) |
| `arch` | string | Yes | Target architecture (e.g. `amd64`, `arm64`) |
| `channel` | string | Yes | Release channel (e.g. `stable`, `dev`) |

#### Response `200 OK`

```json
{
  "revision": 42
}
```

| Field | Type | Description |
|---|---|---|
| `revision` | integer | The latest revision number matching the given parameters |

#### Error responses

| Code | Condition |
|---|---|
| `404` | No revision exists for the given combination |

---

## Package Download Endpoints

### GET /package/download

Download a specific revision of a package.

**Auth not required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `publisher` | string | Yes | The publisher/owner of the package |
| `name` | string | Yes | The package name |
| `revision` | integer | Yes | The revision number to download (use `GET /package/revision` to resolve the latest) |

#### Response `200 OK`

Binary package file (`application/octet-stream`). The client should stream this directly to disk.

#### Error responses

| Code | Condition |
|---|---|
| `404` | Package revision not found |

---

### GET /package/proof

Download the cryptographic proof (signature) for a specific package revision. The proof is used to verify the integrity and authenticity of the downloaded package.

**Auth not required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `publisher` | string | Yes | The publisher/owner of the package |
| `name` | string | Yes | The package name |
| `revision` | integer | Yes | The revision number to retrieve the proof for |

#### Response `200 OK`

Body containing the `.proof` JSON document. The current API serves this as `application/octet-stream`.

Canonical proof shape:
```json
{
  "origin": "publisher",
  "identity": "chef",
  "package": "chef-server",
  "hash-algorithm": "sha512",
  "hash": "<base64 of 64 raw SHA-512 bytes>",
  "public-key": "<base64 of PEM public key bytes>",
  "signature": "<base64 of RSA signature bytes>"
}
```

Proof generation notes:
- The proof is stored next to the package object as `<package-object-key>.proof`.
- `hash` is base64 of the raw SHA-512 digest of the package bytes.
- `public-key` is base64 of the publisher PEM public key contents.
- `signature` is base64 of the RSA PKCS#1 signature produced by signing the raw digest bytes as data with SHA-512.

For more information, see [PROOF_FORMAT.md](PROOF_FORMAT.md).

#### Error responses

| Code | Condition |
|---|---|
| `404` | Proof not found for the given revision |

---

## Package Publish Workflow

Publishing a package is a three-step process:

```
1. POST /package/publish/initiate   →  receive upload-token + revision
2. POST /package/publish/upload     →  stream the package file
3. POST /package/publish/complete   →  finalize and assign to a channel
```

All three steps require authentication.

---

### POST /package/publish/initiate

Begin the publish process. The store allocates a new revision number and returns an upload token used in the subsequent steps.

**🔒 Auth required**

#### Request body

```json
{
  "PublisherName": "string",
  "PackageName": "string",
  "Platform": "string",
  "Architecture": "string",
  "Size": 1048576,
  "Major": 1,
  "Minor": 0,
  "Patch": 0,
  "Tag": "string"
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `PublisherName` | string | Yes | Publisher/owner of the package |
| `PackageName` | string | Yes | Package name |
| `Platform` | string | Yes | Target platform (e.g. `linux`, `windows`) |
| `Architecture` | string | Yes | Target architecture (e.g. `amd64`, `arm64`) |
| `Size` | integer | Yes | File size of the package in bytes |
| `Major` | integer | Yes | Major version number |
| `Minor` | integer | Yes | Minor version number |
| `Patch` | integer | Yes | Patch version number |
| `Tag` | string | No | Optional pre-release or build tag |

#### Response `200 OK`

```json
{
  "upload-token": "string",
  "revision": 42
}
```

| Field | Type | Description |
|---|---|---|
| `upload-token` | string | Opaque token to include in the upload and complete requests |
| `revision` | integer | The revision number assigned to this publish |

#### Error responses

| Code | Condition |
|---|---|
| `401` | Not authenticated |

---

### POST /package/publish/upload

Upload the package file. Uses `multipart/form-data` encoding.

**🔒 Auth required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string | Yes | The `upload-token` returned by `POST /package/publish/initiate` |

#### Request body

`Content-Type: multipart/form-data` with the following parts:

| Part name | Description |
|---|---|
| `sendfile` | The package file binary content |
| `filename` | The destination filename on the server (value: `package.chef`) |
| `submit` | Form submit indicator (value: `send`) |

#### Response `200 OK`

```json
{
  "status": "string"
}
```

| Field | Type | Description |
|---|---|---|
| `status` | string | Upload status message |

#### Error responses

| Code | Condition |
|---|---|
| `401` | Not authenticated or invalid upload token |

---

### POST /package/publish/complete

Finalize the publish and assign the package revision to a release channel, making it available for download.

**🔒 Auth required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `key` | string | Yes | The `upload-token` returned by `POST /package/publish/initiate` |
| `channel` | string | Yes | The release channel to publish to (e.g. `stable`, `dev`) |

#### Request body

Empty body.

#### Response `200 OK`

Empty body on success.

#### Error responses

| Code | Condition |
|---|---|
| `401` | Not authenticated or invalid upload token |

---

## Package Settings Endpoints

### GET /package/settings

Retrieve the settings for a package owned by the authenticated account.

**🔒 Auth required**

#### Query parameters

| Parameter | Type | Required | Description |
|---|---|---|---|
| `name` | string | Yes | The package name |

#### Response `200 OK`

```json
{
  "discoverable": true
}
```

| Field | Type | Description |
|---|---|---|
| `discoverable` | boolean | Whether the package appears in public search results |

#### Error responses

| Code | Condition |
|---|---|
| `401` | Not authenticated |
| `404` | Package not found |

---

### POST /package/settings

Update the settings for a package owned by the authenticated account.

**🔒 Auth required**

#### Request body

```json
{
  "discoverable": true
}
```

| Field | Type | Required | Description |
|---|---|---|---|
| `discoverable` | boolean | Yes | Set to `true` to make the package publicly discoverable, `false` to make it private |

#### Response `200–299`

Empty body on success.

#### Error responses

| Code | Condition |
|---|---|
| `401` | Not authenticated |

---

## Data Types

### Account Status

| Value | Name | Description |
|---|---|---|
| `0` | `UNKNOWN` | Unknown or uninitialized status |
| `1` | `ACTIVE` | Account is active and fully usable |
| `2` | `LOCKED` | Account is locked and cannot be used |
| `3` | `DELETED` | Account has been deleted |

### Publisher Verification Status

| Value | Name | Description |
|---|---|---|
| `0` | `UNKNOWN` | Unknown or uninitialized status |
| `1` | `PENDING` | Verification request is pending review |
| `2` | `REJECTED` | Verification was rejected |
| `3` | `VERIFIED` | Publisher is fully verified |

### Package Type

| Value | Name | Description |
|---|---|---|
| `0` | `UNKNOWN` | Unknown or uninitialized type |
| `1` | `APPLICATION` | Standalone application package |
| `2` | `LIBRARY` | Shared library or SDK package |
| `3` | `TOOLCHAIN` | Toolchain or development tool package |

---

## Fake Store Backend Notes

When implementing a fake store backend for testing, the following behaviors must be replicated:

- **Revision counter**: The server maintains a per-package monotonically increasing revision counter. `POST /package/publish/initiate` must atomically allocate and return the next revision number.
- **Upload token lifecycle**: The upload token returned by `initiate` is single-use and ties together the three publish steps. It may be a UUID, an opaque string, or any value that can be correlated server-side.
- **Channel assignment**: `complete` is what makes a revision visible on a specific channel. Before `complete` is called, `GET /package/revision` must not return the new revision.
- **Binary responses**: `GET /package/download` and `GET /package/proof` return raw binary data, not JSON. The client streams these directly to a file.
- **Search results**: `GET /package/find` returns an array (never `null`). Return `[]` for no matches rather than `404`.
- **Authentication**: All endpoints tagged **🔒 Auth required** must return `401` when the `Authorization` header is absent or invalid.
