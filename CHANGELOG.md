# Changelog

## 0.2.0

- Add native Noctalia v5 launcher lookup, a searchable article panel, bar widget, and IPC endpoint (plugin API 24).
- Share one KDE-independent C++ core and JSON CLI between the KRunner and Noctalia frontends, including existing dictionary configuration, bookmarks, and history.
- Manage dictionary imports, enabled sources, labels, and ordering in the Noctalia panel. Surface lookup/process/database errors and reject stale asynchronous results.
- Build the CLI and Noctalia frontend without KDE using `WORDSPRING_BUILD_KDE=OFF`; keep KRunner enabled by default.
- Test and release both frontends from the same repository, with x86_64/arm64 binary packages and an independent Noctalia script archive.

## 0.1.0

- Styled article sections inspired by Apple Dictionary, with local bookmarks and recent lookup history.
- Filter and reopen saved entries, and clear history without removing bookmarks.
- Icon-only article controls for bookmarking and preferences; recover bookmarks saved by the earlier article-window bug.
- Format WISDOM Japanese translation examples into readable rows and support Japanese internal links.
- Put bilingual example translations on their own line, render WISDOM grammar notes as boxes, and show `!` usage marks as badges.
- Keep etymology and derived forms below the headword, and keep sense numbers on the same line as their translations.
- Show up to four primary meanings in a multiline KRunner result, with the dictionary name in the category column.
- Use editable short dictionary labels in KRunner's fixed-width category column.
- Native Plasma 6 KRunner lookup with a configurable trigger.
- Prebuilt x86_64 and arm64 Linux tarballs with a user or system `install.sh`; the runner finds the article viewer beside its plugin, so tarball installs work in any prefix.
- Apple Dictionary and StarDict import into indexed SQLite files.
- Dictionary management and an article window.
