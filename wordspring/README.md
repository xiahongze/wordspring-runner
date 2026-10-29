# Wordspring for Noctalia

An offline dictionary launcher provider, searchable article panel, and bar widget for native Noctalia v5. The same C++ core powers the Plasma KRunner plugin and the `wordspring-lookup` JSON CLI in this repository. No dictionaries are bundled.

## Install

Use the `wordspring-noctalia-VERSION-linux-ARCH.tar.gz` release for your architecture. It needs Qt 6.8 Core or newer, SQLite, and Noctalia supporting plugin API 24. It does not need KDE or Qt Widgets. Unpack it and run `./install.sh`; this installs the CLI under `~/.local/bin` and the plugin under `${XDG_DATA_HOME:-~/.local/share}/noctalia/plugins/wordspring`. The installer supports `PREFIX`, `XDG_DATA_HOME`, and `--uninstall` and keeps dictionary data.

Alternatively, build from the repository root:

```sh
cmake -S . -B build -G Ninja -DWORDSPRING_BUILD_KDE=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
noctalia msg plugins enable xiahongze/wordspring
```

Build dependencies are a C++17 compiler, CMake 3.24+, Qt6 Core development files, and SQLite development files. Python 3 is needed for tests and imports. Configure with `-DBUILD_TESTING=OFF` to build without the test dependencies. See the repository README for Apple importer environment setup.

For development, a Noctalia `path` source can point at the repository, whose root `catalog.toml` discovers `wordspring/`. For a local drop-in, copy this directory to the user plugin path above. A git source works too:

```sh
noctalia msg plugins source add wordspring git https://github.com/xiahongze/wordspring-runner
noctalia msg plugins enable xiahongze/wordspring
```

The plugin manager installs scripts, so install the CLI separately. If `~/.local/bin` is absent from Noctalia's PATH, set **Wordspring CLI** in plugin settings to `~/.local/bin/wordspring-lookup`.

## Use

- In the launcher, type `/def happy`; select an entry to read it in the full panel. Launcher settings can override the provider prefix. The manifest's prefix omits the `/`, which belongs to the shell.
- Add the `xiahongze/wordspring:dictionary` widget through Noctalia's bar settings. Left click toggles the panel and right click opens plugin settings. The widget uses a glyph on vertical bars; optional text appears on horizontal bars. Gesture bindings remain configurable.
- The panel has search, bookmarks, history, and dictionary management. Entries support bookmarking, copying the word/full text, and following related dictionary words. Escape uses the shell's normal dismiss behavior. Search takes focus on opening and preserves the input while results update.
- Add a local Apple bundle, StarDict `.ifo`, or a parent folder in **Dictionaries**. Apple imports need the configured Python environment with PyGlossary. Labels/order/enabled state are shared with KRunner when saved. Removing a dictionary asks for confirmation, deletes only its imported copy, and keeps original files and saved words.

```sh
# A singleton IPC endpoint works even before the widget or panel has opened.
noctalia msg plugin xiahongze/wordspring:ipc all lookup "happy"
noctalia msg plugin xiahongze/wordspring:ipc all open
noctalia msg panel-open xiahongze/wordspring:panel "猫"
```

These commands can be bound to compositor shortcuts. `onIpc` also exists on the launcher, widget, and panel; non-bar entries use the `all` target.

## Settings and data

By default the CLI reads `wordspringrc` from the XDG config location and uses the existing `$XDG_DATA_HOME/wordspring/dictionaries/` and `state.sqlite`. You can keep using KRunner and the Qt application. They share dictionary configuration, bookmarks, and history; there is no migration or dictionary copy.

Plugin settings include the CLI/settings paths, an optional dictionary JSON override, KRunner keyword, import interpreter, result limit, process timeout, panel debounce, glyph, and optional bar text. Blank keyword/interpreter settings keep the existing shared values. **Save dictionaries and shared settings** in the panel explicitly applies those settings to KRunner too. The dictionary JSON override affects this plugin's lookups without changing the shared list; leave it blank for shared management. Noctalia's prefix is configured in its launcher settings independently of the KRunner keyword.

All lookup and import operations use local files. There is no network access to bypass Noctalia's offline mode, and lookup keeps working with offline mode enabled or no connection.

Lookup errors, unavailable databases, missing executables, malformed JSON, timeouts, and output truncation appear in the launcher/panel. Partial dictionary failures are shown alongside successful results. Imported databases are read-only during lookup; a failed reimport preserves the old copy. No query result cache is kept, so external dictionary changes appear on the next lookup.

## API and rendering

`plugin_api = 24` is the minimum used: modules require 22, direct argv process execution requires 24, and the remaining features predate both. Verified against Noctalia v5.4.0 (supported levels 3–43). Noctalia runs each entry in a separate VM. `require("./lib/lookup.luau")` shares source, with a separate module instance per VM; `noctalia.state` carries only open-entry context. Durable state and dictionary logic stay in the CLI.

The launcher uses host `debounce_ms = 200` and query-keyed results plus generation checks. The panel requests frame ticks only during its debounce window, then stops them; late responses after a newer query, selection, or close are discarded. IPC has its own headless entry because a panel has no runtime until first opened.

Noctalia's native UI has no HTML browser. The CLI converts sanitized dictionary HTML into semantic text blocks and related-word actions for native `ui.*` controls. All article text is retained, but inline CSS, inline emphasis, and the Qt viewer's precise layout are not reproduced. Pronunciation/examples/notes/headings receive native block styling. Images/audio were already excluded by the importer.

The process API caps captured operations at 60 seconds. Panel imports use a 55-second importer deadline so the child is terminated cleanly first. For longer imports, run the same backend directly:

```sh
wordspring-lookup import "/path/to/dictionary.ifo" --timeout-ms 300000
```

## Development checks

```sh
noctalia plugins lint wordspring
python3 tests/test_noctalia.py /path/to/luau
```

Run from the repository root. The second command executes the actual entry/module source in Luau with separate simulated host environments, testing races, debounce, errors, article actions, IPC, and orientation. CTest discovers `luau` and `noctalia` when available. Runtime tests supplement live compositor testing; they do not test native rendering, pointer delivery, or the system clipboard.

Point luau-lsp's `types.definitionFiles` at `wordspring/noctalia.d.luau`; the repository's VS Code settings already do this. Entry scripts use `--!nonstrict`. Type declarations are from the official Noctalia plugins repository. Imported modules live-reload with their owning entry; manifest edits need `noctalia msg config-reload`. See [Noctalia plugin development](https://docs.noctalia.dev/noctalia/plugins/development/).

Licensed GPL-3.0-or-later. Dictionary content belongs to its publishers and is not part of this plugin. Import only copies you are entitled to use; do not redistribute imported databases.
