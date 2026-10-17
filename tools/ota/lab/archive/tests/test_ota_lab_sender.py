"""Offline package and explicit-consent checks for the lab serial sender."""
import hashlib
import importlib.util
import json
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SENDER_PATH = ROOT / "tools/ota/lab/send_lab_package.py"
SPEC = importlib.util.spec_from_file_location("ota_lab_sender", SENDER_PATH)
sender = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sender)


def make_package(**overrides):
    image = b"candidate-image-test"
    manifest = {
        "model": sender.EXPECTED["model"],
        "version": sender.EXPECTED["version"],
        "target": sender.EXPECTED["target"],
        "offset": sender.EXPECTED["offset"],
        "slot_size": sender.EXPECTED["slot_size"],
        "build_id": sender.EXPECTED["build_id"],
        "data_schema": sender.EXPECTED["data_schema"],
        "rollback_compatible": True,
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "signature": "test-only-signature",
    }
    manifest.update(overrides)
    encoded = json.dumps(manifest, separators=(",", ":")).encode()
    return len(encoded).to_bytes(2, "little") + encoded + image


class LabSenderTests(unittest.TestCase):
    def test_decodes_exact_target_and_hash_offline(self):
        manifest, image = sender.decode_package(make_package())
        self.assertEqual(manifest["target"], "ota_1")
        self.assertEqual(image, b"candidate-image-test")

    def test_rejects_wrong_target_size_hash_or_extra_fields(self):
        for changes in ({"target": "ota_0"}, {"offset": 0x10000},
                        {"size": 21}, {"sha256": "0" * 64},
                        {"extra": "ignored fields are forbidden"}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                sender.decode_package(make_package(**changes))

    def test_rejects_truncated_and_unbounded_manifest(self):
        for package in (b"", b"\x01", b"\xff\xff{" , b"\x00\x00x"):
            with self.subTest(package=package), self.assertRaises(ValueError):
                sender.decode_package(package)

    def test_sender_requires_explicit_port_and_physical_confirmation(self):
        source = SENDER_PATH.read_text(encoding="utf-8")
        self.assertIn('parser.add_argument("--port", required=True', source)
        self.assertIn('parser.add_argument("--confirm-physical", required=True', source)
        self.assertNotIn("list_ports", source)
        self.assertIn("port.dtr = False", source)
        self.assertIn("port.rts = False", source)


if __name__ == "__main__":
    unittest.main()
