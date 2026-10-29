"""Smoke-test both release archives/installers using an isolated fake home."""
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*arguments, env=None):
    return subprocess.run(list(map(str, arguments)), cwd=ROOT, env=env, check=True, capture_output=True, text=True)


def main():
    build = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="wordspring-packaging-") as temporary:
        root = Path(temporary)
        output = root / "dist"
        run(ROOT / "scripts/package.sh", build, output, "linux-test")
        run(ROOT / "scripts/package-plugin.sh", output)
        for archive in output.glob("*.tar.gz"):
            with tarfile.open(archive) as tar:
                names = tar.getnames()
                assert not any(name.endswith(".sqlite") for name in names)
                tar.extractall(root / archive.stem, filter="data")
            if archive.name.endswith("plugin.tar.gz"):
                assert "wordspring/plugin.toml" in names
                assert "catalog.toml" in names
                continue
            unpacked = root / archive.stem / names[0].split("/")[0]
            home = root / ("noctalia-home" if archive.name.startswith("wordspring-noctalia") else "kde-home")
            prefix = home / ".local"
            data = home / "custom-data"
            env = dict(os.environ, HOME=str(home), PREFIX=str(prefix), XDG_DATA_HOME=str(data),
                       XDG_CONFIG_HOME=str(home / "config"), XDG_RUNTIME_DIR=str(home / "runtime"))
            run("sh", unpacked / "install.sh", env=env)
            cli = prefix / "bin/wordspring-lookup"
            assert cli.is_file() and os.access(cli, os.X_OK)
            run(cli, "config", env=env)
            marker = data / "wordspring/keep.sqlite"
            marker.parent.mkdir(parents=True, exist_ok=True)
            marker.write_text("saved data must survive uninstall")
            if archive.name.startswith("wordspring-noctalia"):
                assert not any(name.endswith(".so") or name.endswith("/bin/wordspring") for name in names)
                plugin = data / "noctalia/plugins/wordspring/plugin.toml"
                assert plugin.is_file()
                assert (data / "wordspring/import_dictionary.py").is_file()
                # Direct dependencies must not pull in KDE or Qt Widgets.
                dynamic = run("ldd", cli).stdout
                assert "libKF6" not in dynamic and "libQt6Widgets" not in dynamic
                run("sh", unpacked / "install.sh", "--uninstall", env=env)
                assert not plugin.exists() and not cli.exists()
            else:
                assert (prefix / "bin/wordspring").is_file()
                assert (prefix / "lib/qt6/plugins/kf6/krunner/krunner_wordspring.so").is_file()
                assert (home / "config/plasma-workspace/env/wordspring.sh").is_file()
                run("sh", unpacked / "install.sh", "--uninstall", env=env)
                assert not (prefix / "bin/wordspring").exists()
                assert cli.exists(), "The CLI is shared with Noctalia"
            assert marker.read_text() == "saved data must survive uninstall"
            print("Verified", archive.name)


if __name__ == "__main__":
    main()
