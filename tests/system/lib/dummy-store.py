#!/usr/bin/env python3
"""
dummy-store.py — minimal filesystem-backed fake Chef store for system tests

Usage:
    python3 dummy-store.py --port PORT --root DIR [--host HOST]

The store exposes the subset of the Chef Store HTTP API needed by
libs/chefclient and daemons/served:

    GET  /package/find
    GET  /package/info
    GET  /package/revision
    GET  /package/download
    GET  /package/proof
    POST /package/publish/initiate
    POST /package/publish/upload
    POST /package/publish/complete
    GET  /account/publisher    (returns a stub publisher for test use)
    GET  /account/me           (returns a stub account)

All state is stored under --root:
    <root>/
        packages/
            <publisher>/<name>/
                meta.json          — package metadata and revision list
                rev/<revision>/
                    package.pack   — binary package blob
                    proof.bin      — proof blob (placeholder)
        uploads/
            <token>.json           — pending upload metadata
            <token>.pack           — pending upload blob (incomplete)

Authentication:
    All "auth required" endpoints accept ANY non-empty Bearer token so that
    tests do not need to obtain real credentials.

Seeding:
    Use the three-step publish API (initiate → upload → complete) to seed
    the store before running tests.  A convenience script wrapper is provided
    in tests/system/lib/store.sh.
"""

import argparse
import http.server
import json
import os
import shutil
import sys
import threading
import time
import uuid
from urllib.parse import urlparse, parse_qs


# ---------------------------------------------------------------------------
# Storage helpers
# ---------------------------------------------------------------------------

def _pkg_dir(root, publisher, name):
    return os.path.join(root, "packages", publisher, name)


def _rev_dir(root, publisher, name, revision):
    return os.path.join(_pkg_dir(root, publisher, name), "rev", str(revision))


def _meta_path(root, publisher, name):
    return os.path.join(_pkg_dir(root, publisher, name), "meta.json")


def _upload_meta_path(root, token):
    return os.path.join(root, "uploads", token + ".json")


def _upload_blob_path(root, token):
    return os.path.join(root, "uploads", token + ".pack")


def _load_meta(root, publisher, name):
    path = _meta_path(root, publisher, name)
    if not os.path.exists(path):
        return None
    with open(path, "r") as f:
        return json.load(f)


def _save_meta(root, publisher, name, meta):
    pkg_dir = _pkg_dir(root, publisher, name)
    os.makedirs(pkg_dir, exist_ok=True)
    with open(_meta_path(root, publisher, name), "w") as f:
        json.dump(meta, f, indent=2)


def _next_revision(meta):
    if not meta.get("revisions"):
        return 1
    return max(r["version"]["revision"] for r in meta["revisions"]) + 1


# ---------------------------------------------------------------------------
# Request handler
# ---------------------------------------------------------------------------

class StoreHandler(http.server.BaseHTTPRequestHandler):

    def log_message(self, fmt, *args):
        # Route server logs to stderr so they do not mix with test output
        sys.stderr.write("[dummy-store] %s - - %s\n" % (self.address_string(), fmt % args))

    # ---- helpers -----------------------------------------------------------

    def _root(self):
        return self.server.store_root

    def _send_json(self, code, data):
        body = json.dumps(data).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_error(self, code, message="error"):
        self._send_json(code, {"error": message})

    def _send_binary(self, code, data):
        self.send_response(code)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _send_empty(self, code=200):
        self.send_response(code)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def _query(self):
        parsed = urlparse(self.path)
        return parse_qs(parsed.query)

    def _path(self):
        return urlparse(self.path).path

    def _check_auth(self):
        """Return True if the Authorization header is present and non-empty."""
        auth = self.headers.get("Authorization", "")
        return auth.startswith("Bearer ") and len(auth) > len("Bearer ")

    def _read_body(self):
        length = int(self.headers.get("Content-Length", 0))
        if length == 0:
            return b""
        return self.rfile.read(length)

    # ---- routing -----------------------------------------------------------

    def do_GET(self):
        path = self._path()
        q    = self._query()

        if path == "/package/find":
            self._handle_find(q)
        elif path == "/package/info":
            self._handle_info(q)
        elif path == "/package/revision":
            self._handle_revision(q)
        elif path == "/package/download":
            self._handle_download(q)
        elif path == "/package/proof":
            self._handle_proof(q)
        elif path == "/account/publisher":
            self._handle_account_publisher(q)
        elif path == "/account/me":
            self._handle_account_me()
        else:
            self._send_error(404, "not found")

    def do_POST(self):
        path = self._path()
        q    = self._query()

        if path == "/package/publish/initiate":
            self._handle_publish_initiate()
        elif path == "/package/publish/upload":
            self._handle_publish_upload(q)
        elif path == "/package/publish/complete":
            self._handle_publish_complete(q)
        else:
            self._send_error(404, "not found")

    # ---- GET /package/find -------------------------------------------------

    def _handle_find(self, q):
        search = (q.get("search", [""])[0]).lower()
        results = []
        packages_root = os.path.join(self._root(), "packages")
        if os.path.isdir(packages_root):
            for publisher in os.listdir(packages_root):
                pub_dir = os.path.join(packages_root, publisher)
                if not os.path.isdir(pub_dir):
                    continue
                for name in os.listdir(pub_dir):
                    meta = _load_meta(self._root(), publisher, name)
                    if meta is None:
                        continue
                    # simple substring match
                    if search in publisher.lower() or search in name.lower():
                        results.append({
                            "publisher": publisher,
                            "name": name,
                            "summary": meta.get("summary", ""),
                            "type": meta.get("type", 1),
                            "maintainer": meta.get("maintainer", ""),
                            "maintainer-email": meta.get("maintainer-email", ""),
                        })
        self._send_json(200, results)

    # ---- GET /package/info -------------------------------------------------

    def _handle_info(self, q):
        publisher = q.get("publisher", [None])[0]
        name      = q.get("name", [None])[0]
        if not publisher or not name:
            self._send_error(400, "publisher and name required")
            return
        meta = _load_meta(self._root(), publisher, name)
        if meta is None:
            self._send_error(404, "package not found")
            return
        self._send_json(200, {
            "publisher":        publisher,
            "name":             name,
            "type":             meta.get("type", 1),
            "summary":          meta.get("summary", ""),
            "description":      meta.get("description", ""),
            "homepage":         meta.get("homepage", ""),
            "license-spdx":     meta.get("license-spdx", ""),
            "eula":             meta.get("eula", ""),
            "maintainer":       meta.get("maintainer", ""),
            "maintainer-email": meta.get("maintainer-email", ""),
            "revisions":        meta.get("revisions", []),
        })

    # ---- GET /package/revision ---------------------------------------------

    def _handle_revision(self, q):
        publisher = q.get("publisher", [None])[0]
        name      = q.get("name", [None])[0]
        platform  = q.get("platform", [None])[0]
        arch      = q.get("arch", [None])[0]
        channel   = q.get("channel", [None])[0]

        if not all([publisher, name, platform, arch, channel]):
            self._send_error(400, "missing required parameters")
            return

        meta = _load_meta(self._root(), publisher, name)
        if meta is None:
            self._send_error(404, "package not found")
            return

        # find the latest revision matching platform/arch/channel
        best = None
        for rev in meta.get("revisions", []):
            if (rev.get("platform") == platform and
                    rev.get("architecture") == arch and
                    rev.get("channel") == channel):
                r = rev["version"]["revision"]
                if best is None or r > best:
                    best = r

        if best is None:
            self._send_error(404, "no matching revision")
            return

        self._send_json(200, {"revision": best})

    # ---- GET /package/download ---------------------------------------------

    def _handle_download(self, q):
        publisher = q.get("publisher", [None])[0]
        name      = q.get("name", [None])[0]
        revision  = q.get("revision", [None])[0]

        if not all([publisher, name, revision]):
            self._send_error(400, "missing required parameters")
            return

        pack_path = os.path.join(_rev_dir(self._root(), publisher, name, revision), "package.pack")
        if not os.path.exists(pack_path):
            self._send_error(404, "package revision not found")
            return

        with open(pack_path, "rb") as f:
            data = f.read()
        self._send_binary(200, data)

    # ---- GET /package/proof ------------------------------------------------

    def _handle_proof(self, q):
        publisher = q.get("publisher", [None])[0]
        name      = q.get("name", [None])[0]
        revision  = q.get("revision", [None])[0]

        if not all([publisher, name, revision]):
            self._send_error(400, "missing required parameters")
            return

        proof_path = os.path.join(_rev_dir(self._root(), publisher, name, revision), "proof.bin")
        if not os.path.exists(proof_path):
            self._send_error(404, "proof not found")
            return

        with open(proof_path, "rb") as f:
            data = f.read()
        self._send_binary(200, data)

    # ---- GET /account/publisher --------------------------------------------

    def _handle_account_publisher(self, q):
        # Return a stub publisher so that proof resolution does not fail
        # fatally at the network level.  Cryptographic verification will
        # still fail (expected for a test dummy) but the download path can
        # be exercised independently.
        name = q.get("name", ["test-publisher"])[0]
        self._send_json(200, {
            "name":        name,
            "email":       "test@example.com",
            "public-key":  "dummy-public-key",
            "signed-key":  "dummy-signed-key",
            "status":      3,
        })

    # ---- GET /account/me ---------------------------------------------------

    def _handle_account_me(self):
        if not self._check_auth():
            self._send_error(401, "unauthorized")
            return
        self._send_json(200, {
            "name":       "test-account",
            "email":      "test@example.com",
            "status":     1,
            "publishers": [{"name": "test-publisher", "email": "test@example.com",
                             "public-key": "dummy", "signed-key": "dummy", "status": 3}],
            "api-keys":   [],
        })

    # ---- POST /package/publish/initiate ------------------------------------

    def _handle_publish_initiate(self):
        if not self._check_auth():
            self._send_error(401, "unauthorized")
            return

        body = self._read_body()
        try:
            params = json.loads(body)
        except json.JSONDecodeError:
            self._send_error(400, "invalid JSON")
            return

        publisher = params.get("PublisherName")
        name      = params.get("PackageName")
        platform  = params.get("Platform", "linux")
        arch      = params.get("Architecture", "amd64")
        major     = params.get("Major", 0)
        minor     = params.get("Minor", 0)
        patch     = params.get("Patch", 0)
        tag       = params.get("Tag", "")
        size      = params.get("Size", 0)

        if not publisher or not name:
            self._send_error(400, "PublisherName and PackageName required")
            return

        with self.server.lock:
            meta = _load_meta(self._root(), publisher, name)
            if meta is None:
                meta = {
                    "publisher":        publisher,
                    "name":             name,
                    "summary":          "",
                    "description":      "",
                    "type":             1,
                    "maintainer":       "",
                    "maintainer-email": "",
                    "revisions":        [],
                }
            revision = _next_revision(meta)

            token = str(uuid.uuid4())
            upload_meta = {
                "publisher": publisher,
                "name":      name,
                "platform":  platform,
                "arch":      arch,
                "major":     major,
                "minor":     minor,
                "patch":     patch,
                "tag":       tag,
                "size":      size,
                "revision":  revision,
                "complete":  False,
                "channel":   None,
            }

            os.makedirs(os.path.join(self._root(), "uploads"), exist_ok=True)
            with open(_upload_meta_path(self._root(), token), "w") as f:
                json.dump(upload_meta, f)

        self._send_json(200, {"upload-token": token, "revision": revision})

    # ---- POST /package/publish/upload --------------------------------------

    def _handle_publish_upload(self, q):
        if not self._check_auth():
            self._send_error(401, "unauthorized")
            return

        token = q.get("key", [None])[0]
        if not token:
            self._send_error(400, "key parameter required")
            return

        meta_path = _upload_meta_path(self._root(), token)
        if not os.path.exists(meta_path):
            self._send_error(401, "invalid upload token")
            return

        content_type = self.headers.get("Content-Type", "")
        if "multipart/form-data" in content_type:
            # Parse multipart/form-data to extract the file part
            body = self._read_body()
            # Locate the binary content between the multipart boundaries
            file_data = self._extract_multipart_file(content_type, body)
        else:
            # Treat entire body as raw binary (simpler curl usage)
            file_data = self._read_body()

        blob_path = _upload_blob_path(self._root(), token)
        with open(blob_path, "wb") as f:
            f.write(file_data)

        self._send_json(200, {"status": "uploaded"})

    def _extract_multipart_file(self, content_type, body):
        """Extract the first file part from a multipart/form-data body."""
        try:
            # Find boundary
            boundary = None
            for part in content_type.split(";"):
                part = part.strip()
                if part.startswith("boundary="):
                    boundary = part[len("boundary="):].strip().encode()
                    break
            if not boundary:
                return body  # fall back to raw body

            # Split on boundary lines
            delim = b"--" + boundary
            parts = body.split(delim)
            for part in parts:
                if b"Content-Disposition" not in part:
                    continue
                # Split headers from body
                if b"\r\n\r\n" in part:
                    header_section, file_body = part.split(b"\r\n\r\n", 1)
                elif b"\n\n" in part:
                    header_section, file_body = part.split(b"\n\n", 1)
                else:
                    continue
                # Strip trailing boundary marker
                file_body = file_body.rstrip(b"\r\n")
                if file_body.endswith(b"--"):
                    file_body = file_body[:-2].rstrip(b"\r\n")
                # Return first non-empty file body
                if b'filename' in header_section and file_body:
                    return file_body
            return body
        except (ValueError, IndexError, AttributeError) as exc:
            sys.stderr.write("[dummy-store] multipart parse error (%s): falling back to raw body\n" % exc)
            return body

    # ---- POST /package/publish/complete ------------------------------------

    def _handle_publish_complete(self, q):
        if not self._check_auth():
            self._send_error(401, "unauthorized")
            return

        token   = q.get("key", [None])[0]
        channel = q.get("channel", ["stable"])[0]

        if not token:
            self._send_error(400, "key parameter required")
            return

        meta_path = _upload_meta_path(self._root(), token)
        blob_path = _upload_blob_path(self._root(), token)

        if not os.path.exists(meta_path):
            self._send_error(401, "invalid upload token")
            return

        with self.server.lock:
            with open(meta_path, "r") as f:
                upload_meta = json.load(f)

            publisher = upload_meta["publisher"]
            name      = upload_meta["name"]
            revision  = upload_meta["revision"]

            rev_dir = _rev_dir(self._root(), publisher, name, revision)
            os.makedirs(rev_dir, exist_ok=True)

            # Move uploaded blob to the revision directory
            if os.path.exists(blob_path):
                shutil.move(blob_path, os.path.join(rev_dir, "package.pack"))
            else:
                # No blob was uploaded — create a placeholder
                open(os.path.join(rev_dir, "package.pack"), "wb").close()

            # Write a placeholder proof blob.
            # Format: "dummy-proof:<publisher>/<name>" — intentionally not a
            # real cryptographic signature.  The verify state in served will
            # fail against this placeholder, which is expected and documented.
            with open(os.path.join(rev_dir, "proof.bin"), "wb") as f:
                f.write(b"dummy-proof:" + publisher.encode() + b"/" + name.encode())

            # Update package metadata
            meta = _load_meta(self._root(), publisher, name)
            if meta is None:
                meta = {
                    "publisher":        publisher,
                    "name":             name,
                    "summary":          "",
                    "description":      "",
                    "type":             1,
                    "maintainer":       "",
                    "maintainer-email": "",
                    "revisions":        [],
                }

            meta["revisions"].append({
                "channel":      channel,
                "platform":     upload_meta["platform"],
                "architecture": upload_meta["arch"],
                "size":         os.path.getsize(os.path.join(rev_dir, "package.pack")),
                "date":         time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "version": {
                    "revision": revision,
                    "major":    upload_meta["major"],
                    "minor":    upload_meta["minor"],
                    "patch":    upload_meta["patch"],
                    "tag":      upload_meta.get("tag", ""),
                },
            })
            _save_meta(self._root(), publisher, name, meta)

            # Remove upload metadata
            os.remove(meta_path)

        self._send_empty(200)


# ---------------------------------------------------------------------------
# Server bootstrap
# ---------------------------------------------------------------------------

class StoreServer(http.server.ThreadingHTTPServer):
    def __init__(self, server_address, handler, store_root):
        super().__init__(server_address, handler)
        self.store_root = store_root
        self.lock = threading.Lock()


def main():
    parser = argparse.ArgumentParser(description="Dummy Chef store for testing")
    parser.add_argument("--port", type=int, default=9876, help="TCP port to listen on")
    parser.add_argument("--host", default="127.0.0.1", help="Host to bind to")
    parser.add_argument("--root", required=True, help="Filesystem root for store data")
    args = parser.parse_args()

    os.makedirs(args.root, exist_ok=True)

    server = StoreServer((args.host, args.port), StoreHandler, args.root)
    sys.stderr.write("[dummy-store] listening on http://%s:%d  root=%s\n" %
                     (args.host, args.port, args.root))
    sys.stderr.flush()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        sys.stderr.write("[dummy-store] stopped\n")


if __name__ == "__main__":
    main()
