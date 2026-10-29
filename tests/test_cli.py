"""Exercise the public JSON interface with synthetic, isolated dictionaries."""
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import struct
import subprocess
import sys
import tempfile
import unittest

CLI = Path(sys.argv.pop(1)).resolve()
IMPORTER = Path(__file__).resolve().parents[1] / "importer/import_dictionary.py"


class CliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="wordspring-cli-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = dict(os.environ, XDG_CONFIG_HOME=str(self.root / "config"), XDG_DATA_HOME=str(self.root / "data"))
        self.source = str(self.root / "dictionary with spaces.ifo")
        self.items = [{"source": self.source, "name": "Synthetic Dictionary", "label": "SYN", "enabled": True}]
        self.call("configure", "--dictionaries-json", json.dumps(self.items))
        self.db = self.database(self.source)
        self.db.parent.mkdir(parents=True)
        with sqlite3.connect(self.db) as db:
            db.executescript("""
                CREATE TABLE entries(id INTEGER PRIMARY KEY,headword TEXT,preview TEXT,html TEXT,meanings TEXT);
                CREATE TABLE keys(norm TEXT,entry_id INTEGER,UNIQUE(norm,entry_id));
                CREATE INDEX keys_norm ON keys(norm);
            """)
            rows = [
                (1, "happy", "pleased", '<h1 class="ws-head">happy</h1><p>pleased &amp; content</p><div class="ws-example-row">a happy cat</div><p>See <a href="bword://%E7%8C%AB">猫</a></p>', "pleased\nfortunate"),
                (2, "猫", "cat", "<p>猫 means cat</p>", "cat"),
                (3, "happy", "lucky", "<p>lucky</p>", "lucky"),
                (4, "happy", "cheerful", "<p>cheerful</p>", "cheerful"),
                (5, "Straße", "a road", "<p>a road</p>", "a road"),
            ]
            db.executemany("INSERT INTO entries VALUES(?,?,?,?,?)", rows)
            db.executemany("INSERT INTO keys VALUES(?,?)", [(r[1].casefold(), r[0]) for r in rows])
            db.execute("INSERT INTO keys VALUES('glad',1)")

    def database(self, source):
        name = hashlib.sha256(os.path.normpath(source).encode()).hexdigest() + ".sqlite"
        return self.root / "data/wordspring/dictionaries" / name

    def call(self, *args, error=None):
        process = subprocess.run([str(CLI), *args], env=self.env, capture_output=True, text=True, timeout=15)
        self.assertEqual(process.stderr, "", process.stderr)
        value = json.loads(process.stdout)
        self.assertEqual(value["protocol"], 1)
        self.assertEqual(value["ok"], error is None, value)
        self.assertEqual(process.returncode == 0, error is None, value)
        if error:
            self.assertEqual(value["error"]["code"], error, value)
        return value

    def test_lookup_limits_order_alias_and_unicode(self):
        result = self.call("lookup", "  HAPPY  ")
        self.assertEqual([r["id"] for r in result["results"]], ["1", "3"])
        self.assertEqual(result["results"][0]["meanings"], ["pleased", "fortunate"])
        self.assertNotIn("blocks", result["results"][0])
        self.assertEqual(len(self.call("lookup", "happy", "--limit", "3")["results"]), 3)
        self.assertEqual(self.call("lookup", "glad")["results"][0]["headword"], "happy")
        self.assertEqual(self.call("lookup", "猫")["results"][0]["headword"], "猫")
        self.assertEqual(self.call("lookup", "STRASSE")["results"][0]["headword"], "Straße")

    def test_article_links_and_shared_activity(self):
        args = ("--source", self.source, "--id", "1")
        article = self.call("entry", *args, "--record-visit")["article"]
        self.assertIn("pleased & content", article["text"])
        self.assertEqual(article["links"], [{"query": "猫", "text": "猫"}])
        self.assertTrue(any(b["role"] == "heading" for b in article["blocks"]))
        self.assertFalse(article["favorite"])
        self.call("favorite", *args, "--enabled", "true")
        self.assertTrue(self.call("entry", *args)["article"]["favorite"])
        self.assertEqual(len(self.call("favorites", "--filter", "hap")["entries"]), 1)
        self.assertEqual(len(self.call("history")["entries"]), 1)
        self.call("clear-history")
        self.assertEqual(self.call("history")["entries"], [])
        self.assertEqual(len(self.call("favorites")["entries"]), 1)
        self.call("favorite", *args, "--enabled", "false")
        self.assertEqual(self.call("favorites")["entries"], [])

    def test_history_is_bounded(self):
        self.call("history")
        with sqlite3.connect(self.root / "data/wordspring/state.sqlite") as db:
            db.executemany("INSERT INTO history VALUES(?,?,?,?,?)", [(self.source, i, str(i), "Synthetic", i) for i in range(1, 505)])
        self.call("entry", "--source", self.source, "--id", "1", "--record-visit")
        with sqlite3.connect(self.root / "data/wordspring/state.sqlite") as db:
            self.assertEqual(db.execute("SELECT COUNT(*) FROM history").fetchone()[0], 500)

    def test_not_found_empty_invalid_and_disabled(self):
        self.call("lookup", "unfindable", error="not_found")
        self.call("lookup", "", error="invalid_query")
        self.call("lookup", "a" * 129, error="invalid_query")
        self.call("lookup", "happy", "--limit", "0", error="invalid_arguments")
        self.call("entry", "--source", self.source, "--id", "999", error="entry_not_found")
        self.call("--unknown", error="invalid_arguments")
        disabled = [dict(self.items[0], enabled=False)]
        self.call("lookup", "happy", "--dictionaries-json", json.dumps(disabled), error="no_dictionaries")
        self.assertEqual(len(self.call("lookup", "happy")["results"]), 2)

    def test_missing_and_partial_dictionary_errors(self):
        missing = {"source": str(self.root / "missing.ifo"), "name": "Missing"}
        self.call("lookup", "happy", "--dictionaries-json", json.dumps([missing]), error="database_unavailable")
        result = self.call("lookup", "happy", "--dictionaries-json", json.dumps(self.items + [missing]))
        self.assertEqual(len(result["results"]), 2)
        self.assertEqual(result["warnings"][0]["code"], "database_unavailable")

    def test_legacy_database_without_meanings(self):
        with sqlite3.connect(self.db) as db:
            db.execute("ALTER TABLE entries DROP COLUMN meanings")
        self.assertEqual(self.call("lookup", "happy")["results"][0]["meanings"], [])
        self.assertIn("pleased", self.call("entry", "--source", self.source, "--id", "1")["article"]["text"])

    def test_configuration_escaping_and_preservation(self):
        path = self.root / "config/wordspringrc"
        with path.open("a") as stream:
            stream.write("\n[Other]\nUntouched=keep me\n")
        special = 'Space \\ dictionary "日本語"'
        self.items[0]["name"] = special
        self.call("configure", "--trigger", "lookup", "--python", "/interpreter with spaces/python", "--dictionaries-json", json.dumps(self.items))
        settings = self.call("config")
        self.assertEqual(settings["dictionaries"][0]["name"], special)
        self.assertEqual(settings["trigger"], "lookup")
        self.assertIn("Untouched=keep me", path.read_text())
        self.call("configure", "--dictionaries-json", "{}", error="invalid_config")
        self.call("configure", "--dictionaries-json", '[{"source":"relative","name":"bad"}]', error="invalid_config")
        self.call("configure", "--trigger", "two words", error="invalid_config")

    def test_import_and_failure_preserves_database(self):
        source = self.root / "new dictionary.ifo"
        source.write_text("StarDict's dict ifo file\nversion=2.4.2\nbookname=New Synthetic\nsametypesequence=m\n")
        definition = b"a synthetic definition"
        source.with_suffix(".dict").write_bytes(definition)
        source.with_suffix(".idx").write_bytes(b"fixture\0" + struct.pack(">II", 0, len(definition)))
        result = self.call("import", str(source), "--importer", str(IMPORTER))
        self.assertEqual(result["imported"][0]["count"], 1)
        self.assertEqual(self.call("lookup", "fixture")["results"][0]["dictionary"], "New Synthetic")
        previous = self.database(str(source)).read_bytes()
        source.with_suffix(".idx").write_bytes(b"broken")
        self.call("import", str(source), "--importer", str(IMPORTER), error="import_failed")
        self.assertEqual(self.database(str(source)).read_bytes(), previous)
        self.call("remove", "--source", str(source))
        self.assertFalse(self.database(str(source)).exists())
        self.assertTrue(source.exists())

    def test_import_timeout_and_missing_interpreter(self):
        slow = self.root / "slow.py"
        slow.write_text("import time\ntime.sleep(5)\n")
        Path(self.source).touch()
        self.call("import", self.source, "--importer", str(slow), "--timeout-ms", "50", error="timeout")
        self.call("import", self.source, "--importer", str(IMPORTER), "--python", "/missing/python", error="python_missing")

    def test_state_failure_is_visible(self):
        state = self.root / "data/wordspring/state.sqlite"
        state.mkdir()
        self.call("history", error="state_unavailable")

    def test_remove_orphaned_bookmark(self):
        args = ("--source", self.source, "--id", "1")
        self.call("favorite", *args, "--enabled", "true")
        self.db.unlink()
        self.call("favorite", *args, "--enabled", "false")
        self.assertEqual(self.call("favorites")["entries"], [])

    def test_lookup_does_not_need_a_network(self):
        self.env.update(http_proxy="http://127.0.0.1:1", https_proxy="http://127.0.0.1:1", NOCTALIA_OFFLINE="1")
        self.assertTrue(self.call("lookup", "happy")["ok"])


if __name__ == "__main__":
    unittest.main()
