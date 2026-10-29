# Wordspring JSON CLI

`wordspring-lookup` is built from the same `wordspring_core` static library that KRunner and the article application link. It requires Qt6 Core and SQLite, with no KDE/KRunner/Qt Widgets dependency. CMake option `WORDSPRING_BUILD_KDE=OFF` builds it independently.

Each command emits one JSON document followed by a newline on stdout. Success has `{"protocol":1,"ok":true,...}` and exit status 0. Failure has `{"protocol":1,"ok":false,"error":{"code":"...","message":"..."}}` and nonzero exit status. `--help` and `--version` print human-readable output. Protocol 1 IDs are strings to preserve SQLite integer precision in clients.

```sh
wordspring-lookup lookup "happy"
wordspring-lookup lookup --limit 4 -- "a phrase"
wordspring-lookup entry --source "/path/to/source.ifo" --id 42 --record-visit
wordspring-lookup favorite --source "/path/to/source.ifo" --id 42 --enabled true
wordspring-lookup favorites --filter "hap"
wordspring-lookup history
wordspring-lookup clear-history
wordspring-lookup config
wordspring-lookup configure --trigger define --python python3
wordspring-lookup configure --dictionaries-json '[{"source":"/path/to/source.ifo","name":"Example","enabled":true,"label":"EX"}]'
wordspring-lookup import "/path/to/source.ifo"
wordspring-lookup import "/path/to/dictionary/folder"
wordspring-lookup remove --source "/path/to/source.ifo"
```

`lookup` uses exact trimmed case-folded keys (including imported aliases), enabled dictionary order, and entry-ID order. Its `results` contain `source`, string `id`, `headword`, `preview`, `meanings` (up to four imported senses), `dictionary`, and `label`. The default is two entries per dictionary. It does not fetch HTML or record history.

`entry` returns `article` with the same identifiers plus `blocks` (`role`, `text`), `links` (`query`, `text`), full `text`, and `favorite`. `--record-visit` updates the existing history database. The original HTML remains in the imported database for the Qt article frontend.

`favorites`/`history` return `entries` with source/ID/headword/dictionary/timestamp. History is capped at 500 distinct entries. `clear-history` keeps bookmarks. `remove` drops a source from settings and deletes only its indexed copy; original dictionary files and bookmarks/history remain.

Lookup returns `warnings` for failed dictionaries when another source supplies results. If no results exist and a source failed, the error is `database_unavailable`; a successful search without matches is `not_found`. Other errors include `invalid_query`, `invalid_arguments`, `invalid_config`, `no_dictionaries`, `entry_not_found`, `state_unavailable`, `config_unavailable`, `config_busy`, `importer_missing`, `python_missing`, `source_unavailable`, `import_failed`, and `timeout`.

`--config PATH` overrides the XDG `wordspringrc` path. `--dictionaries-json JSON` overrides the list for the current process, except `configure` explicitly saves it. Sources in this option must be absolute or start with `~/`. `--python` overrides the import interpreter; `configure --python` saves it. `--importer PATH` selects an importer script for source-tree testing. Installed builds find it under the XDG data search paths or relative to the executable (`../share/wordspring/import_dictionary.py`). `--timeout-ms` controls the importer deadline; the default is five minutes. Imports are atomic per dictionary; folder imports return successful imports and warnings for failed sources.

Dictionary databases and user state retain their existing XDG paths and schemas. Configuration updates use locking and atomic replacement, preserving other groups/keys and KConfig escaping for the General values used by Wordspring. The optional KDE test verifies compatibility using KConfig itself. Concurrent edits by other applications should still be made one frontend at a time.
