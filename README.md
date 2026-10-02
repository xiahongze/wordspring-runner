# Wordspring Runner

An offline dictionary lookup plugin for Plasma 6 KRunner. Type `define word` to see up to four short meanings from each enabled dictionary; press Enter to read the full entry in a styled article window. Bookmark entries and revisit recent lookups from settings. The trigger word is configurable.

| Article | Dictionaries | Bookmarks and history |
| --- | --- | --- |
| ![Wordspring article window, using synthetic dictionary content](docs/screenshots/wordspring-article.png) | ![Wordspring dictionary settings, using synthetic dictionary names](docs/screenshots/wordspring-settings.png) | ![Wordspring saved words settings, using synthetic entries](docs/screenshots/wordspring-activity.png) |

The screenshots use synthetic entries.

> [!IMPORTANT]
> **Wordspring Runner does not include any dictionaries.** You supply them. Apple's dictionaries are copyrighted by Apple and their publishers (Oxford University Press, Sanseido and others). Import only copies you own or are licensed to use, for example from a Mac you own, and follow the terms of that licence. Do not share imported databases. This project is not affiliated with or endorsed by Apple or any dictionary publisher.

## Features

- Import original macOS `.dictionary` bundles directly, or StarDict `.ifo` files with their companion `.idx`, `.dict` or `.dict.dz`, and optional `.syn` files.
- Add a folder to discover multiple dictionary bundles and StarDict files. Enable, disable, reorder, reimport, or remove sources in settings. Each source has an editable short label for KRunner; for example, Oxford Dictionary of English defaults to **ODE**.
- Keep an indexed SQLite copy of each dictionary in your XDG data directory. Lookup opens only the enabled databases and never runs the importer.
- Use the Apple bundle's semantic markup to distinguish headwords, pronunciation, parts of speech, numbered senses, examples, phrases, and origins. Bilingual dictionaries show each example translation on its own line, and usage notes such as WISDOM's grammar points appear in a box. Original Apple CSS, audio, and embedded images are not included.
- Use the bookmark icon in the article window; the settings icon opens preferences. The **Bookmarks & History** page lets you filter and reopen saved words, remove bookmarks, and clear lookup history without removing bookmarks.

## Install

You need Plasma 6. Choose a prebuilt release or build from source, then [set up the importer](#set-up-the-dictionary-importer) and [add dictionaries](#add-dictionaries).

### Prebuilt release (x86_64 and arm64)

Each [release](https://github.com/xiahongze/wordspring-runner/releases) includes `linux-x86_64` and `linux-aarch64` tarballs. They are built on Debian 13 and need Qt 6.8 and KDE Frameworks 6.13 or newer, so they work on Debian 13 and on current Plasma distributions such as Arch, Fedora and openSUSE Tumbleweed. If your distribution ships older versions, build from source.

```bash
version=0.1.0
arch=$(uname -m)   # x86_64 or aarch64
curl -LO "https://github.com/xiahongze/wordspring-runner/releases/download/v$version/wordspring-runner-$version-linux-$arch.tar.gz"
curl -LO "https://github.com/xiahongze/wordspring-runner/releases/download/v$version/SHA256SUMS"
sha256sum --check --ignore-missing SHA256SUMS
tar -xzf "wordspring-runner-$version-linux-$arch.tar.gz"
cd "wordspring-runner-$version-linux-$arch"
./install.sh
```

`./install.sh` installs for your user under `~/.local` and adds `~/.config/plasma-workspace/env/wordspring.sh`, so Plasma adds the user plugin directory to Qt's search path. **Log out and back in** before using KRunner. To install for all users instead, run `sudo ./install.sh --system`, then restart KRunner with `kquitapp6 krunner`. `./install.sh --uninstall` (with `sudo` and `--system` for a system install) removes the program and keeps your imported dictionaries and bookmarks.

### Build from source

Build dependencies: Qt 6, KDE Frameworks 6 (KRunner, KCMUtils, KConfig, KCoreAddons, KI18n), SQLite, CMake, Python 3 and a C++17 compiler.

```bash
# Arch
sudo pacman -S --needed cmake ninja gcc qt6-base krunner kcmutils kconfig kcoreaddons ki18n sqlite python
# Debian 13 and newer Debian or Ubuntu releases
sudo apt install cmake ninja-build g++ extra-cmake-modules qt6-base-dev libkf6runner-dev libkf6kcmutils-dev \
  libkf6config-dev libkf6coreaddons-dev libkf6i18n-dev libsqlite3-dev python3
```

Then build and install:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
```

The install adds a native KRunner plugin, its configuration module, the `wordspring` article and settings application, and the importer. Restart KRunner or log out and back in so Plasma discovers the new plugin.

For a per-user installation, configure with `-DCMAKE_INSTALL_PREFIX="$HOME/.local"`, run `cmake --install build` without `sudo`, and copy [`scripts/wordspring-plasma-env.sh`](scripts/wordspring-plasma-env.sh) into `~/.config/plasma-workspace/env/`. Log out and back in so Plasma includes the user plugin directory in Qt's search path.

On Debian-based systems, also pass `-DCMAKE_INSTALL_LIBDIR=lib` for a per-user install so the plugin lands in `~/.local/lib/qt6/plugins`, which the login script adds.

## Set up the dictionary importer

Importing needs Python 3. StarDict import uses only the standard library. Apple import also needs [PyGlossary](https://github.com/ilius/pyglossary) and the packages in [`importer/requirements-apple.txt`](importer/requirements-apple.txt) (also included in the release tarball). Install them in an isolated environment:

```bash
python3 -m venv ~/.local/share/wordspring/import-venv
~/.local/share/wordspring/import-venv/bin/python -m pip install -r requirements-apple.txt   # importer/requirements-apple.txt in a source checkout
```

If your Python does not provide `venv` or `pip`, use `uv venv` and `uv pip install --python` with the same requirements file. In Wordspring settings, set **Import Python** to `~/.local/share/wordspring/import-venv/bin/python`. Importing is the only time PyGlossary runs; lookups only read the imported SQLite copies.

## Add dictionaries

Open **System Settings → Search → Plasma Search → Wordspring Runner**, or run `wordspring --settings`.

- **Apple dictionaries.** Copy them from a Mac you own. On macOS, open Dictionary → Settings and enable the dictionaries you want, so macOS downloads them. Recent macOS versions keep downloaded dictionaries under `/System/Library/AssetsV2/com_apple_MobileAsset_DictionaryServices_dictionaryOSX/`, with each `.dictionary` bundle inside an `*.asset/AssetData/` folder. Copy that folder to your Linux machine, then choose **Add folder** to import every bundle in it, or **Add Apple bundle** for a single `.dictionary`. Some bundles contain no usable `Body.data`; their import reports an error, and you can remove them from the list. Read the [notice above](#wordspring-runner) about dictionary licences first.
- **StarDict dictionaries.** Choose **Add StarDict** and select the `.ifo` file. Keep the `.idx`, `.dict` or `.dict.dz`, and optional `.syn` files beside it.

Each source gets a short label for KRunner's narrow left column, such as **ODE** for Oxford Dictionary of English. Double-click the **Label** cell to change it. Importing starts as soon as you add a source. Press **Save settings** (or **Apply** in System Settings) to keep the dictionary list.

## Use

Use `define run` in KRunner. Selecting a result opens the full entry. Click an internal word link in the article to look it up. Change **KRunner keyword** to use another trigger.

## Data and performance

Imported databases live at `$XDG_DATA_HOME/wordspring/dictionaries/` (usually `~/.local/share/wordspring/dictionaries/`). Bookmarks and recently opened entries live in `$XDG_DATA_HOME/wordspring/state.sqlite`; history is capped at 500 distinct entries. Settings live in `wordspringrc` under the XDG config directory. Reimport replaces a database atomically; a failed import leaves the previous copy available. Removing a source deletes its imported copy but never touches the original dictionary.

On the development machine, a locally imported copy of the Oxford Dictionary of English produced 114,925 entries and the WISDOM English–Japanese/Japanese–English bundle 95,174, and the Oxford Chinese–English bundle 136,120. A repeated exact lookup of `run` took about 0.14 ms median with a warm filesystem cache. These numbers are local measurements, not a cross-machine guarantee. Import is a one-time operation; large dictionaries can take tens of seconds. The Apple parser keeps key and cross-reference indexes in memory while importing, but the runtime plugin never loads the whole dictionary.

## Development

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The C++ tests check indexed, Unicode, enabled-source lookup, bookmark and history persistence, article controls, and linked-entry navigation. The Python test builds a tiny StarDict fixture and checks synonym import, HTML filtering, and Apple-style structure. The importer can also be run directly:

```bash
python importer/import_dictionary.py --input /path/to/dictionary.ifo --output /tmp/example.sqlite
```

### Releases

Pushing a `v*` tag runs [`.github/workflows/release.yml`](.github/workflows/release.yml). It builds and tests in a Debian 13 container on native x86_64 and arm64 GitHub runners, packages each build with [`scripts/package.sh`](scripts/package.sh), and publishes the tarballs, a source archive and `SHA256SUMS` to a GitHub release. Release notes come from the matching `CHANGELOG.md` section.

## Credits and license

The Apple reader is provided by [PyGlossary](https://github.com/ilius/pyglossary) and is invoked only during import. Wordspring Runner is licensed under [GPL-3.0-or-later](LICENSE). Apple and the dictionary publishers own their dictionaries; Wordspring Runner ships no dictionary content, and you are responsible for having the right to use the dictionaries you import.
