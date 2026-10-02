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
                self.assertEqual(db.execute("SELECT meanings FROM entries").fetchone()[0], "A road")
                self.assertEqual({row[0] for row in db.execute("SELECT norm FROM keys")}, {"strasse", "road"})

    def test_rejects_unsafe_markup(self):
        html, preview = module.clean_definition('<p>word<script>alert(1)</script><a href="https://example.com">bad link</a></p>', True)
        self.assertNotIn("<script", html)
        self.assertNotIn("https://", html)
        self.assertIn("word", preview)

    def test_apple_article_structure_is_preserved(self):
        html = ('<span class="hg"><span class="hw">happy</span><span class="pr"> /hapi/</span></span>'
                '<span class="se2"><span class="sn">1</span><span class="df">pleased</span>'
                '<span class="eg">she smiled</span></span>'
                '<span class="subEntryBlock"><span class="gp ty_label x_xoLblBlk">PHRASES</span></span>')
        cleaned, _preview = module.clean_definition(html, True)
        for expected in ('class="ws-head"', 'class="ws-pron"', 'class="ws-sense"',
                         'class="ws-definition"', 'class="ws-example"', '<hr>', 'class="ws-section-title"'):
            self.assertIn(expected, cleaned)
        self.assertIn('<span class="ws-number">1</span>', cleaned)

    def test_typed_stardict_record(self):
        parts = module.stardict_definition(b"mA small definition\0h<b>bold</b>\0", "")
        self.assertEqual(parts, [("A small definition", False), ("<b>bold</b>", True)])

    def test_apple_primary_meanings_exclude_examples_and_subsenses(self):
        html = ('<span class="df" d:def="1">first meaning</span>'
                '<span class="eg">example text</span>'
                '<span class="df">a submeaning</span>'
                '<span class="df" d:def="1">second meaning</span>')
        self.assertEqual(module.extract_meanings(html, True, "fallback"), "first meaning\nsecond meaning")

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
