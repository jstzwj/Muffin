"""Offline publication-contract tests; no CNB/GitHub credentials or network."""

import base64
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec, utils

spec = importlib.util.spec_from_file_location("publisher", Path(__file__).with_name("publish_cnb_release.py"))
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)


class FakeCnb:
    def __init__(self):
        self.events = []

    def release(self, tag, draft):
        self.events.append(("release", tag, draft))
        return {"id": tag, "tag_name": tag}

    def upload(self, release, path, overwrite=False):
        self.events.append(("upload", release["id"], path.name, overwrite))

    def api(self, method, url, body):
        self.events.append(("api", method, url, body))


class PublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.key = ec.generate_private_key(ec.SECP256R1())
        numbers = self.key.public_key().public_numbers()
        self.public_key = self.root / "key.txt"
        self.public_key.write_text((bytes.fromhex("4543533120000000") + numbers.x.to_bytes(32, "big") + numbers.y.to_bytes(32, "big")).hex())
        self.assets = self.root / "assets"
        self.assets.mkdir()
        for arch in ("x64", "arm64"):
            installer = self.assets / f"Muffin-Setup-1.2.3-windows-{arch}.exe"
            installer.write_bytes((arch + " fixture").encode() * 100)
            payload = json.dumps({"schema": 1, "version": "1.2.3", "architecture": arch, "fileName": installer.name,
                                  "size": installer.stat().st_size, "sha256": publisher.sha256(installer)}).encode()
            r, s = utils.decode_dss_signature(self.key.sign(payload, ec.ECDSA(hashes.SHA256())))
            installer.with_suffix(".update.json").write_text(json.dumps({"payload": base64.b64encode(payload).decode(),
                "signature": base64.b64encode(r.to_bytes(32, "big") + s.to_bytes(32, "big")).decode()}))
        self.files = publisher.artifact_files(self.assets, "v1.2.3")
        self.catalog = publisher.write_catalog(self.root / "catalog", publisher.catalog(self.files, "v1.2.3", "cnb"))

    def test_signed_packages_remain_identical_across_sources(self):
        publisher.verify_windows_packages(self.files, "v1.2.3", self.public_key)
        cnb = publisher.catalog(self.files, "v1.2.3", "cnb")
        github = publisher.catalog(self.files, "v1.2.3", "github")
        self.assertEqual(cnb["tag_name"], github["tag_name"])
        self.assertEqual([a["size"] for a in cnb["assets"]], [a["size"] for a in github["assets"]])
        self.assertTrue(all(a["browser_download_url"].startswith(publisher.WEB + "/download/v1.2.3/") for a in cnb["assets"]))
        for path in self.files.values():
            if path.suffix == ".exe":
                path.write_bytes(b"modified")
                break
        with self.assertRaises(publisher.PublishError):
            publisher.verify_windows_packages(self.files, "v1.2.3", self.public_key)

    def test_signature_from_another_publisher_is_rejected(self):
        wrong_key = ec.generate_private_key(ec.SECP256R1()).public_key().public_numbers()
        self.public_key.write_text((bytes.fromhex("4543533120000000") + wrong_key.x.to_bytes(32, "big") + wrong_key.y.to_bytes(32, "big")).hex())
        with self.assertRaises(publisher.PublishError):
            publisher.verify_windows_packages(self.files, "v1.2.3", self.public_key)

    def test_no_feed_promotion_before_all_anonymous_downloads_match(self):
        client = FakeCnb()
        checks = []
        def verify(url, path):
            self.assertFalse(any(event[:2] == ("release", "updates") for event in client.events))
            checks.append(path.name)
        # The final check verifies the promoted feed rather than a versioned asset.
        def checked(url, path):
            if url != publisher.FEED_URL:
                verify(url, path)
        with patch.object(publisher, "verify_public", side_effect=checked), patch.object(publisher, "published_feed", return_value=None):
            publisher.publish(client, self.files, "v1.2.3", self.catalog)
        self.assertEqual(set(checks), set(self.files) | {publisher.CATALOG})
        promotions = [event for event in client.events if event[:2] == ("upload", "updates")]
        self.assertEqual(promotions, [("upload", "updates", publisher.CATALOG, True)])

    def test_failed_verification_keeps_previous_channel(self):
        client = FakeCnb()
        with patch.object(publisher, "verify_public", side_effect=publisher.PublishError("unavailable")):
            with self.assertRaises(publisher.PublishError):
                publisher.publish(client, self.files, "v1.2.3", self.catalog)
        self.assertFalse(any(event[:2] == ("release", "updates") for event in client.events))

    def test_old_release_rebuild_cannot_downgrade_channel(self):
        client = FakeCnb()
        with patch.object(publisher, "verify_public"), patch.object(publisher, "published_feed", return_value={"tag_name": "v1.3.0"}):
            publisher.publish(client, self.files, "v1.2.3", self.catalog)
        self.assertFalse(any(event[:2] == ("release", "updates") for event in client.events))

    def test_ambiguous_or_mixed_artifacts_are_rejected(self):
        nested = self.assets / "duplicate"
        nested.mkdir()
        next(iter(self.files.values())).rename(nested / next(iter(self.files)))
        original = next(iter(self.files))
        (self.assets / original).write_bytes(b"duplicate")
        with self.assertRaises(publisher.PublishError):
            publisher.artifact_files(self.assets, "v1.2.3")
        for tag in ("v1.2.3-beta", "v01.2.3", "v1.2.3/other"):
            with self.assertRaises(publisher.PublishError):
                publisher.version(tag)

    def test_credentials_never_follow_upload_or_download_hosts(self):
        client = publisher.Cnb("test credential, never used on network")
        for url in ("https://example.com/verify", "https://api.cnb.cool.evil.test/jstzwj/Muffin/-/releases", "http://api.cnb.cool/jstzwj/Muffin/-/releases"):
            with self.assertRaises(publisher.PublishError):
                client.api("POST", url)

    def test_new_release_and_draft_retry_preserve_published_assets(self):
        client = publisher.Cnb("offline fixture credential")
        upload = {"upload_url": "https://storage.example.test/upload",
                  "verify_url": publisher.API + "/release/verify"}
        for assets, draft, overwrite in ((None, False, False), ([{"name": self.catalog.name}], True, True)):
            release = {"id": "release", "tag_name": "v1.2.3", "draft": draft, "assets": assets}
            with patch.object(client, "api", side_effect=[upload, None]) as api, patch.object(publisher.urllib.request, "urlopen"):
                client.upload(release, self.catalog)
                self.assertEqual(api.call_args_list[0].args[2]["overwrite"], overwrite)
                self.assertEqual(api.call_args_list[0].args[2]["ttl"], 0)
        release = {"id": "release", "tag_name": "v1.2.3", "draft": False, "assets": [{"name": self.catalog.name}]}
        with patch.object(client, "api") as api, patch.object(publisher, "verify_public") as verify:
            client.upload(release, self.catalog)
            api.assert_not_called()
            verify.assert_called_once()


if __name__ == "__main__":
    unittest.main()
