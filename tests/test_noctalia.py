"""Validate the shipping plugin and execute its callbacks in the real Luau VM.

The host is mocked, with separate globals/module caches for each entry and a
shared state broker. This tests actual plugin source, including asynchronous
ordering, without depending on a compositor in CI.
"""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[1]
PLUGIN = ROOT / "wordspring"


def quote_luau(text):
    equals = "="
    while "]" + equals + "]" in text:
        equals += "="
    return "[" + equals + "[" + text + "]" + equals + "]"


def validate():
    manifest = tomllib.loads((PLUGIN / "plugin.toml").read_text())
    catalog = tomllib.loads((ROOT / "catalog.toml").read_text())["plugin"][0]
    for key in ("id", "name", "version", "plugin_api", "license", "dependencies"):
        assert manifest[key] == catalog[key], key
    project = re.search(r"project\(wordspring-runner VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    assert project == manifest["version"], "Release versions must match"
    assert manifest["plugin_api"] == 24
    translations = json.loads((PLUGIN / "translations/en.json").read_text())
    ids = set()
    for kind in ("widget", "panel", "launcher_provider", "service"):
        for entry in manifest[kind]:
            assert entry["id"] not in ids
            ids.add(entry["id"])
            assert (PLUGIN / entry["entry"]).is_file()
    defaults = {}
    for setting in manifest["setting"]:
        assert setting["label_key"] in translations
        if "description_key" in setting:
            assert setting["description_key"] in translations
        defaults[setting["key"]] = setting["default"]
    for path in PLUGIN.rglob("*.luau"):
        if path.name == "noctalia.d.luau":
            continue
        source = path.read_text()
        assert source.startswith("--!nonstrict"), str(path)
        for required in re.findall(r'require\("([^\"]+)"\)', source):
            assert required.startswith(("./", "../")) and required.endswith(".luau")
            assert (path.parent / required).is_file()
    return defaults


def main():
    defaults = validate()
    if len(sys.argv) == 1:
        print("Noctalia manifest, catalog, imports, translations, and release versions validated")
        return
    sources = {str(p.relative_to(PLUGIN)): p.read_text() for p in PLUGIN.rglob("*.luau") if p.name != "noctalia.d.luau"}
    code = "local SOURCES = {\n" + "\n".join("[" + json.dumps(k) + "] = " + quote_luau(v) + "," for k, v in sources.items()) + "\n}\n"
    code += "local SETTINGS = {\n" + "\n".join("[" + json.dumps(k) + "] = " + json.dumps(v) + "," for k, v in defaults.items()) + "\n}\n"
    definitions = (PLUGIN / "noctalia.d.luau").read_text()
    props = {}
    for name, declaration in re.findall(r"export type (Ui\w+Props) = \{(.*?)\n\}", definitions, re.S):
        props[name] = re.findall(r"^  (\w+):", declaration, re.M)
    props["UiSpacerProps"] = props["UiCommonProps"]
    controls = {name: props[type_name] for name, type_name in re.findall(r"^  (\w+): \(props: (Ui\w+Props)", definitions, re.M)}
    code += "local PROPS = {\n" + "\n".join("[" + json.dumps(k) + "] = {" + ",".join("[" + json.dumps(p) + "]=true" for p in v) + "}," for k, v in controls.items()) + "\n}\n"
    code += (ROOT / "tests/noctalia_harness.luau").read_text()
    with tempfile.TemporaryDirectory(prefix="wordspring-luau-test-") as temporary:
        path = Path(temporary) / "test.luau"
        path.write_text(code)
        subprocess.run([sys.argv[1], str(path)], check=True)


if __name__ == "__main__":
    main()
