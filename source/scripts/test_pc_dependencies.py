"""Dependency cache contracts: missing archives, checksum mismatch, cache tamper, and traversal."""
from pathlib import Path
import hashlib
import tempfile
import unittest
import zipfile
from prepare_pc_dependencies import acquire

class DependencyCacheContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "archives").mkdir()
        self.archive = self.root / "archives/test.zip"
        with zipfile.ZipFile(self.archive, "w") as zipped:
            zipped.writestr("public-source/include/header.h", "upstream header")
        self.spec = {"directory": "public-source", "archive_sha256": hashlib.sha256(self.archive.read_bytes()).hexdigest()}
    def tearDown(self):
        self.temp.cleanup()
    def test_missing_offline_archive_does_not_contact_network(self):
        with self.assertRaisesRegex(ValueError, "Offline archive is missing"):
            acquire(self.root, "missing", self.spec, True)
    def test_corrupt_archive_is_rejected_before_source_creation(self):
        self.archive.write_bytes(self.archive.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "archive SHA-256 mismatch"):
            acquire(self.root, "test", self.spec, True)
        self.assertFalse((self.root / "sources").exists())
    def test_changed_cached_header_is_rejected(self):
        source = acquire(self.root, "test", self.spec, True)
        (source / "include/header.h").write_text("changed")
        with self.assertRaisesRegex(ValueError, "source cache changed"):
            acquire(self.root, "test", self.spec, True)
    def test_archive_escape_is_rejected(self):
        with zipfile.ZipFile(self.archive, "w") as zipped:
            zipped.writestr("public-source/../../escape.h", "escape")
        self.spec["archive_sha256"] = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        with self.assertRaisesRegex(ValueError, "Unsafe archive member"):
            acquire(self.root, "test", self.spec, True)

if __name__ == "__main__":
    unittest.main()
