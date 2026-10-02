import importlib.util
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "importer" / "import_dictionary.py"
spec = importlib.util.spec_from_file_location("wordspring_importer", SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ImporterTest(unittest.TestCase):
    def test_stardict_import_and_synonym(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "tiny"
            (root / "tiny.ifo").write_text("StarDict's dict ifo file\nversion=2.4.2\nbookname=Tiny\nwordcount=1\nsynwordcount=1\nsametypesequence=m\n")
            (root / "tiny.idx").write_bytes(b"Stra\xc3\x9fe\0" + struct.pack(">II", 0, 7))
            (root / "tiny.dict").write_bytes(b"A road\0")
            (root / "tiny.syn").write_bytes(b"road\0" + struct.pack(">I", 0))
            output = root / "index.sqlite"
            result = subprocess.run([sys.executable, str(SCRIPT), "--input", str(base.with_suffix(".ifo")), "--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with sqlite3.connect(output) as db:
                self.assertEqual(db.execute("SELECT value FROM meta WHERE key='name'").fetchone()[0], "Tiny")
                self.assertEqual(db.execute("SELECT COUNT(*) FROM entries").fetchone()[0], 1)
                self.assertEqual({row[0] for row in db.execute("SELECT norm FROM keys")}, {"strasse", "road"})

    def test_rejects_unsafe_markup(self):
        html, preview = module.clean_definition('<p>word<script>alert(1)</script><a href="https://example.com">bad link</a></p>', True)
        self.assertNotIn("<script", html)
        self.assertNotIn("https://", html)
        self.assertIn("word", preview)

    def test_typed_stardict_record(self):
        parts = module.stardict_definition(b"mA small definition\0h<b>bold</b>\0", "")
        self.assertEqual(parts, [("A small definition", False), ("<b>bold</b>", True)])

    def test_failed_import_preserves_existing_database(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "old.sqlite"
            output.write_bytes(b"existing data")
            source = root / "broken.ifo"
            source.write_text("not a StarDict file")
            result = subprocess.run([sys.executable, str(SCRIPT), "--input", str(source), "--output", str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(output.read_bytes(), b"existing data")


if __name__ == "__main__":
    unittest.main()
