"""Mirror verified GitHub build artifacts to CNB and advance the static update feed.

CNB_RELEASE_TOKEN is used only for authenticated api.cnb.cool publication calls.
Clients fetch public release attachments, without tokens or release-list APIs.
"""

import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

REPO = "jstzwj/Muffin"
API = f"https://api.cnb.cool/{REPO}/-/releases"
WEB = f"https://cnb.cool/{REPO}/-/releases"
GITHUB = f"https://github.com/{REPO}/releases"
CATALOG = "muffin-update.json"
FEED_URL = f"{WEB}/download/updates/{CATALOG}"
SUFFIXES = (".7z", ".tar.gz", ".msi", ".exe", ".dmg", ".update.json")


class PublishError(RuntimeError):
    pass


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def version(tag):
    if not re.fullmatch(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", tag):
        raise PublishError("Expected a stable vX.Y.Z release tag")
    return tuple(map(int, tag[1:].split(".")))


def artifact_files(directory, tag):
    release_version = tag[1:]
    result = {}
    for path in sorted(directory.rglob("*")):
        if not path.is_file() or not path.name.endswith(SUFFIXES):
            continue
        if not path.name.startswith((f"Muffin-{tag}-", f"Muffin-{release_version}-", f"Muffin-Setup-{release_version}-")):
            raise PublishError(f"Artifact belongs to a different release: {path.name}")
        if path.name in result:
            raise PublishError(f"Duplicate artifact: {path.name}")
        result[path.name] = path
    return result


def verify_windows_packages(files, tag, public_key_path):
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec, utils

    blob = bytes.fromhex(public_key_path.read_text().strip())
    if len(blob) != 72 or blob[:8] != bytes.fromhex("4543533120000000"):
        raise PublishError("Invalid embedded update public key")
    key = ec.EllipticCurvePublicNumbers(int.from_bytes(blob[8:40], "big"), int.from_bytes(blob[40:], "big"), ec.SECP256R1()).public_key()
    for arch in ("x64", "arm64"):
        stem = f"Muffin-Setup-{tag[1:]}-windows-{arch}"
        installer, manifest = files.get(stem + ".exe"), files.get(stem + ".update.json")
        if not installer or not manifest:
            raise PublishError(f"Missing signed Windows {arch} update")
        envelope = json.loads(manifest.read_bytes())
        payload = base64.b64decode(envelope["payload"], validate=True)
        signature = base64.b64decode(envelope["signature"], validate=True)
        if len(signature) != 64:
            raise PublishError(f"Invalid {arch} signature length")
        try:
            key.verify(utils.encode_dss_signature(int.from_bytes(signature[:32], "big"), int.from_bytes(signature[32:], "big")), payload, ec.ECDSA(hashes.SHA256()))
        except Exception as error:
            raise PublishError(f"Invalid {arch} update signature") from error
        expected = {"schema": 1, "version": tag[1:], "architecture": arch, "fileName": installer.name,
                    "size": installer.stat().st_size, "sha256": sha256(installer)}
        if json.loads(payload) != expected:
            raise PublishError(f"Signed {arch} metadata does not match the installer")


def catalog(files, tag, source):
    base = WEB if source == "cnb" else GITHUB
    return {"schema": 1, "tag_name": tag, "html_url": f"{base}/tag/{tag}", "draft": False, "prerelease": False,
            "assets": [{"name": name, "size": path.stat().st_size,
                        "browser_download_url": f"{base}/download/{tag}/{urllib.parse.quote(name)}"}
                       for name, path in sorted(files.items())]}


def write_catalog(directory, obj):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / CATALOG
    path.write_text(json.dumps(obj, ensure_ascii=True, separators=(",", ":")) + "\n", encoding="utf-8")
    return path


class Cnb:
    def __init__(self, token):
        if not token:
            raise PublishError("CNB_RELEASE_TOKEN is required (repo-release:rw, scoped to jstzwj/Muffin)")
        self.token = token
        self.authenticated = urllib.request.build_opener(NoRedirect())

    def api(self, method, url, body=None, missing_ok=False):
        # Never forward publication credentials to a storage/CDN redirect.
        parsed = urllib.parse.urlsplit(url)
        if parsed.scheme != "https" or parsed.netloc != "api.cnb.cool" or not parsed.path.startswith(f"/{REPO}/-/releases"):
            raise PublishError("Refusing a publication request outside the official CNB API")
        request = urllib.request.Request(url, method=method, data=None if body is None else json.dumps(body).encode(),
                                         headers={"Authorization": "Bearer " + self.token, "Accept": "application/vnd.cnb.api+json",
                                                  "Content-Type": "application/json", "User-Agent": "Muffin-Release-Publisher"})
        try:
            with self.authenticated.open(request, timeout=60) as response:
                data = response.read(1024 * 1024)
                return json.loads(data) if data else None
        except urllib.error.HTTPError as error:
            if missing_ok and error.code == 404:
                return None
            # Do not print request objects, authorization, or presigned URLs.
            raise PublishError(f"CNB {method} failed: HTTP {error.code}; check token scope and repository initialization") from None

    def release(self, tag, draft):
        existing = self.api("GET", f"{API}/tags/{tag}", missing_ok=True)
        if existing:
            return existing
        return self.api("POST", API, {"tag_name": tag, "target_commitish": "main", "name": f"Muffin {tag}" if tag != "updates" else "Muffin update channel",
                                      "body": f"Official binary mirror. Source and release notes: {GITHUB}/tag/{tag}" if tag != "updates" else "Static update discovery. Installers are verified against Muffin's embedded signing key.",
                                      "draft": draft, "prerelease": tag == "updates", "make_latest": "false"})

    def upload(self, release, path, overwrite=False):
        existing = next((asset for asset in (release.get("assets") or []) if asset["name"] == path.name), None)
        if existing and not overwrite:
            if release.get("draft"):
                # A partially uploaded draft is not public yet; restage it on retry.
                overwrite = True
            else:
                # Published version assets are immutable. Verify rather than replacing them.
                verify_public(f"{WEB}/download/{release['tag_name']}/{urllib.parse.quote(path.name)}", path)
                print(f"Verified existing {path.name}")
                return
        for attempt in range(3):
            try:
                upload = self.api("POST", f"{API}/{release['id']}/asset-upload-url",
                                  {"asset_name": path.name, "size": path.stat().st_size, "overwrite": overwrite, "ttl": 0})
                if urllib.parse.urlsplit(upload["upload_url"]).scheme != "https":
                    raise PublishError("CNB returned a non-HTTPS upload URL")
                # PUT begins immediately: upload URLs have a short validity window.
                with path.open("rb") as stream:
                    request = urllib.request.Request(upload["upload_url"], data=stream, method="PUT",
                                                     headers={"Content-Length": str(path.stat().st_size), "Content-Type": "application/octet-stream"})
                    with urllib.request.urlopen(request, timeout=1800) as response:
                        response.read(1024)
                verify_url = upload["verify_url"]
                separator = "&" if "?" in verify_url else "?"
                self.api("POST", verify_url + separator + "ttl=0")
                print(f"Uploaded {path.name}")
                return
            except (urllib.error.URLError, TimeoutError, PublishError):
                if attempt == 2:
                    raise PublishError(f"Upload failed for {path.name}; signed URLs and credentials omitted") from None
                time.sleep(2 ** attempt)


def verify_public(url, path):
    # A fresh anonymous request proves that installed clients need no token/login.
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "Muffin-Release-Verifier"}), timeout=180) as response:
            digest = hashlib.sha256()
            size = 0
            while chunk := response.read(1024 * 1024):
                size += len(chunk)
                if size > path.stat().st_size:
                    raise PublishError(f"Public download exceeds expected size: {path.name}")
                digest.update(chunk)
    except (urllib.error.URLError, TimeoutError):
        raise PublishError(f"Anonymous download failed: {path.name}") from None
    if size != path.stat().st_size or digest.hexdigest() != sha256(path):
        raise PublishError(f"Public download does not match the release artifact: {path.name}")


def published_feed():
    try:
        with urllib.request.urlopen(FEED_URL, timeout=30) as response:
            return json.loads(response.read(128 * 1024))
    except urllib.error.HTTPError as error:
        if error.code == 404:
            return None
        raise PublishError(f"Cannot read current CNB update feed: HTTP {error.code}") from None


def publish(client, files, tag, cnb_catalog):
    release = client.release(tag, draft=True)
    for path in files.values():
        client.upload(release, path)
    client.upload(release, cnb_catalog)
    client.api("PATCH", f"{API}/{release['id']}", {"draft": False, "make_latest": "false"})
    for path in [*files.values(), cnb_catalog]:
        verify_public(f"{WEB}/download/{tag}/{urllib.parse.quote(path.name)}", path)
    # Advance the pointer only after every versioned download is public and verified.
    current = published_feed()
    if current and version(current["tag_name"]) > version(tag):
        print("Newer CNB update feed exists; historical rebuild did not change it")
        return
    channel = client.release("updates", draft=False)
    client.upload(channel, cnb_catalog, overwrite=True)
    verify_public(FEED_URL, cnb_catalog)
    client.api("PATCH", f"{API}/{release['id']}", {"make_latest": "true"})
    print(f"CNB update channel now points to {tag}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("update-metadata"))
    parser.add_argument("--public-key", type=Path, default=Path(__file__).resolve().parents[1] / "resources/update-public-key.txt")
    parser.add_argument("--prepare-only", action="store_true")
    args = parser.parse_args()
    version(args.tag)
    files = artifact_files(args.assets, args.tag)
    verify_windows_packages(files, args.tag, args.public_key)
    write_catalog(args.output / "github", catalog(files, args.tag, "github"))
    cnb_catalog = write_catalog(args.output / "cnb", catalog(files, args.tag, "cnb"))
    if not args.prepare_only:
        publish(Cnb(os.environ.get("CNB_RELEASE_TOKEN")), files, args.tag, cnb_catalog)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Network exceptions may contain signed URLs; only our sanitized errors are printable.
        print(str(error) if isinstance(error, PublishError) else f"Publication failed ({type(error).__name__}); sensitive details omitted", file=sys.stderr)
        sys.exit(1)
