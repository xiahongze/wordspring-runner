#!/usr/bin/env python3
"""Import Apple Dictionary or StarDict into Wordspring's on-disk SQLite index."""

import argparse
import gzip
from html import escape
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import shutil
import sqlite3
import struct
import sys
import tempfile


class CleanHTML(HTMLParser):
    ALLOWED = {"div", "p", "br", "b", "strong", "i", "em", "u", "ul", "ol", "li", "span", "sup", "sub", "blockquote", "h1", "h2", "h3", "a", "hr"}
    BLOCK = {"div", "p", "br", "li", "blockquote", "h1", "h2", "h3"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.html = []
        self.plain = []
        self.stack = []
        self.suppress = 0

    def handle_starttag(self, tag, attrs):
        tag = tag.rsplit(":", 1)[-1].lower()
        if tag in {"script", "style"}:
            self.suppress += 1
            return
        if self.suppress:
            return
        classes = next((v.split() for k, v in attrs if k == "class"), [])
        rendered = tag
        style_class = ""
        if tag == "span":
            if "hg" in classes:
                rendered, style_class = "h1", "ws-head"
            elif "hw" in classes:
                style_class = "ws-word"
            elif "pr" in classes or "prx" in classes:
                style_class = "ws-pron"
            elif "posg" in classes:
                rendered, style_class = "p", "ws-pos"
            elif "pos" in classes:
                style_class = "ws-posname"
            elif "se2" in classes:
                rendered, style_class = "div", "ws-sense"
            elif "se1" in classes:
                rendered, style_class = "div", "ws-part"
            elif "msDict" in classes:
                if "t_subsense" in classes:
                    rendered, style_class = "p", "ws-subsense"
                elif "t_core" in classes:
                    rendered, style_class = "p", "ws-phrase-definition"
                else:
                    style_class = "ws-meaning"
            elif "df" in classes:
                rendered, style_class = "strong", "ws-definition"
            elif "eg" in classes or "ex" in classes:
                rendered, style_class = "em", "ws-example"
            elif "subEntryBlock" in classes:
                rendered, style_class = "div", "ws-section"
            elif "subEntry" in classes:
                rendered, style_class = "div", "ws-phrase"
            elif "sn" in classes:
                style_class = "ws-number"
            elif "gp" in classes and "ty_label" in classes and "x_xoLblBlk" in classes:
                rendered, style_class = "h3", "ws-section-title"
            elif "l" in classes and "x_xoh" in classes:
                rendered, style_class = "strong", "ws-phrase-head"
            elif any(c in classes for c in ("ge", "reg", "gg", "lg")):
                style_class = "ws-qualifier"
            elif any(c.startswith("se") and c[2:].isdigit() for c in classes):
                rendered = "div"
        if rendered in self.BLOCK:
            self.plain.append(" ")
        if rendered in self.ALLOWED:
            if tag == "a":
                href = next((v for k, v in attrs if k == "href"), "")
                if href.startswith("bword://"):
                    self.html.append(f'<a href="{escape(href, quote=True)}">')
                else:
                    self.html.append("<span>")
                    rendered = "span"
            else:
                class_attribute = f' class="{style_class}"' if style_class else ""
                self.html.append(f"<{rendered}{class_attribute}>")
            if rendered not in {"br", "hr"}:
                self.stack.append((tag, rendered, style_class == "ws-section-title"))
        elif tag in self.BLOCK:
            self.html.append("<p>")
            self.stack.append((tag, "p", False))

    def handle_endtag(self, tag):
        tag = tag.rsplit(":", 1)[-1].lower()
        if tag in {"script", "style"}:
            self.suppress = max(0, self.suppress - 1)
            return
        if self.suppress:
            return
        if tag in self.BLOCK:
            self.plain.append(" ")
        if self.stack and tag == self.stack[-1][0]:
            _, rendered, add_rule = self.stack.pop()
            self.html.append(f"</{rendered}>")
            if add_rule:
                self.html.append("<hr>")

    def handle_data(self, data):
        if self.suppress:
            return
        self.html.append(escape(data))
        self.plain.append(data)

    def finish(self):
        while self.stack:
            _, rendered, add_rule = self.stack.pop()
            self.html.append(f"</{rendered}>")
            if add_rule:
                self.html.append("<hr>")
        return "".join(self.html), " ".join("".join(self.plain).split())


class MeaningCollector(HTMLParser):
    """Collect dictionary sense text, leaving examples and pronunciation out."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.depth = 0
        self.active = []
        self.primary = []
        self.definitions = []
        self.blocks = []

    def handle_starttag(self, tag, attrs):
        self.depth += 1
        attributes = dict(attrs)
        classes = attributes.get("class", "").split()
        if "df" in classes:
            kind = "primary" if attributes.get("d:def") == "1" else "definitions"
            self.active.append((self.depth, kind, []))
        elif tag in {"p", "li"}:
            self.active.append((self.depth, "blocks", []))

    def handle_data(self, data):
        for _depth, _kind, parts in self.active:
            parts.append(data)

    def handle_endtag(self, _tag):
        for capture in self.active[:]:
            depth, kind, parts = capture
            if depth == self.depth:
                value = " ".join("".join(parts).split())
                if value:
                    getattr(self, kind).append(value)
                self.active.remove(capture)
        self.depth = max(0, self.depth - 1)


def extract_meanings(definition, is_html, preview):
    if not is_html:
        candidates = definition.splitlines()
    else:
        collector = MeaningCollector()
        collector.feed(definition)
        candidates = collector.primary or collector.definitions or collector.blocks
    result = []
    for candidate in candidates:
        candidate = " ".join(candidate.split())
        if candidate and candidate not in result:
            result.append(candidate)
        if len(result) == 4:
            break
    return "\n".join(result or [preview])


def clean_definition(definition, is_html):
    if not is_html:
        text = " ".join(definition.split())
        return "<p>" + escape(definition).replace("\n", "<br>") + "</p>", text
    parser = CleanHTML()
    parser.feed(definition)
    return parser.finish()


def create_schema(db):
    db.executescript("""
        PRAGMA journal_mode=OFF;
        PRAGMA synchronous=OFF;
        CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE entries (id INTEGER PRIMARY KEY, headword TEXT NOT NULL, preview TEXT NOT NULL, html TEXT NOT NULL, meanings TEXT NOT NULL);
        CREATE TABLE keys (norm TEXT NOT NULL, entry_id INTEGER NOT NULL, UNIQUE(norm, entry_id));
        CREATE INDEX keys_norm ON keys(norm);
    """)


def insert_entry(db, words, definition, is_html):
    words = [str(w).strip() for w in words if str(w).strip()]
    if not words:
        return None
    html, text = clean_definition(definition, is_html)
    if not text:
        return None
    meanings = extract_meanings(definition, is_html, text)
    cur = db.execute("INSERT INTO entries(headword,preview,html,meanings) VALUES(?,?,?,?)", (words[0], text[:400], html, meanings))
    db.executemany("INSERT OR IGNORE INTO keys(norm,entry_id) VALUES(?,?)", ((word.casefold(), cur.lastrowid) for word in set(words)))
    return cur.lastrowid


class AppleEntry:
    def __init__(self, word, defi, defiFormat):
        self.words = word if isinstance(word, list) else [word]
        self.definition = defi
        self.is_html = defiFormat == "h"


class AppleAdapter:
    sourceLangName = ""
    targetLangName = ""

    def __init__(self):
        self.info = {}

    def setInfo(self, key, value):
        self.info[key] = str(value)

    def newEntry(self, word, defi, defiFormat="m", **_kwargs):
        return AppleEntry(word, defi, defiFormat)

    def newDataEntry(self, _name, _data):
        return None


def import_apple(db, source):
    try:
        from pyglossary.plugins.appledict_bin.reader import Reader
    except ImportError as exc:
        raise RuntimeError("Apple import requires pyglossary, lxml and biplist; see README") from exc
    adapter = AppleAdapter()
    reader = Reader(adapter)
    reader._html_full = False
    try:
        for _progress in reader.open(str(source)):
            pass
        count = 0
        for entry in reader:
            if entry is not None and insert_entry(db, entry.words, entry.definition, entry.is_html):
                count += 1
                if count % 1000 == 0:
                    print(json.dumps({"progress": count}), flush=True)
    finally:
        reader.close()
    return adapter.info.get("name", source.stem), count


def parse_ifo(path):
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    if not lines or not lines[0].startswith("StarDict's dict ifo file"):
        raise ValueError("Invalid StarDict .ifo file")
    return dict(line.split("=", 1) for line in lines[1:] if "=" in line)


def stardict_definition(blob, sequence):
    pieces = []
    pos = 0
    for fixed_typ in sequence if sequence else iter(int, 1):
        if pos >= len(blob):
            break
        if sequence:
            typ = fixed_typ
        else:
            typ = chr(blob[pos])
            pos += 1
        if typ.isupper():
            if pos + 4 > len(blob):
                raise ValueError("Invalid StarDict typed record")
            size = struct.unpack_from(">I", blob, pos)[0]
            pos += 4
            data = blob[pos:pos + size]
            pos += size
        else:
            end = blob.find(b"\0", pos)
            if end < 0:
                end = len(blob)
            data = blob[pos:end]
            pos = end + 1
        if typ.lower() in ("m", "h"):
            pieces.append((data.decode("utf-8", "replace"), typ.lower() == "h"))
    return pieces


def import_stardict(db, source):
    info = parse_ifo(source)
    base = source.with_suffix("")
    idx = base.with_suffix(".idx")
    body = base.with_suffix(".dict")
    compressed = base.with_suffix(".dict.dz")
    if not idx.is_file() or not (body.is_file() or compressed.is_file()):
        raise ValueError("StarDict needs matching .idx and .dict or .dict.dz files")
    temporary = None
    if not body.is_file():
        temporary = tempfile.NamedTemporaryFile(prefix="wordspring-dict-", delete=False)
        try:
            with gzip.open(compressed, "rb") as src:
                shutil.copyfileobj(src, temporary)
        finally:
            temporary.close()
        body = Path(temporary.name)
    count = 0
    try:
        db.execute("CREATE TABLE positions (position INTEGER PRIMARY KEY, entry_id INTEGER)")
        with idx.open("rb") as index, body.open("rb") as data:
            position = 0
            while True:
                word_bytes = bytearray()
                while True:
                    byte = index.read(1)
                    if not byte:
                        if word_bytes:
                            raise ValueError("Truncated StarDict index")
                        break
                    if byte == b"\0":
                        break
                    word_bytes.extend(byte)
                if not byte:
                    break
                width = 12 if info.get("idxoffsetbits") == "64" else 8
                location = index.read(width)
                if len(location) != width:
                    raise ValueError("Truncated StarDict index record")
                offset, size = struct.unpack(">QI" if width == 12 else ">II", location)
                data.seek(offset)
                blob = data.read(size)
                if len(blob) != size:
                    raise ValueError("Truncated StarDict data record")
                parts = stardict_definition(blob, info.get("sametypesequence", ""))
                html = "".join(part if rich else "<p>" + escape(part) + "</p>" for part, rich in parts)
                entry_id = insert_entry(db, [word_bytes.decode("utf-8")], html, True)
                if entry_id:
                    db.execute("INSERT INTO positions(position,entry_id) VALUES(?,?)", (position, entry_id))
                    count += 1
                    if count % 1000 == 0:
                        print(json.dumps({"progress": count}), flush=True)
                position += 1
        synonyms = base.with_suffix(".syn")
        if synonyms.is_file():
            with synonyms.open("rb") as stream:
                while True:
                    word_bytes = bytearray()
                    while (byte := stream.read(1)) not in (b"", b"\0"):
                        word_bytes.extend(byte)
                    if not byte:
                        break
                    position_bytes = stream.read(4)
                    if len(position_bytes) != 4:
                        raise ValueError("Truncated StarDict synonym index")
                    position = struct.unpack(">I", position_bytes)[0]
                    db.execute("INSERT OR IGNORE INTO keys(norm,entry_id) SELECT ?,entry_id FROM positions WHERE position=?", (word_bytes.decode("utf-8", "replace").casefold(), position))
        db.execute("DROP TABLE positions")
    finally:
        if temporary:
            body.unlink(missing_ok=True)
    return info.get("bookname", source.stem), count


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    source = args.input.resolve()
    if not source.exists():
        raise ValueError(f"Source does not exist: {source}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=".import-", suffix=".sqlite", dir=args.output.parent)
    os.close(fd)
    try:
        with sqlite3.connect(temp_name) as db:
            create_schema(db)
            if source.suffix == ".dictionary":
                name, count = import_apple(db, source)
            elif source.suffix == ".ifo":
                name, count = import_stardict(db, source)
            else:
                raise ValueError("Select a .dictionary bundle or StarDict .ifo file")
            if count == 0:
                raise ValueError("No entries could be imported")
            db.execute("INSERT INTO meta(key,value) VALUES('name',?)", (name,))
            db.commit()
        os.replace(temp_name, args.output)
        print(json.dumps({"name": name, "count": count}), flush=True)
    finally:
        if os.path.exists(temp_name):
            os.unlink(temp_name)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"Import failed: {exc}", file=sys.stderr)
        sys.exit(1)
