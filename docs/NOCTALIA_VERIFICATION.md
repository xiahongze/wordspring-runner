# Noctalia port verification

Verified on 2026-10-29 with installed Noctalia v5.4.0 (`5.4.0-1-dirty`, package `5.4.0-1.1`). The corresponding v5.4.0 source declares supported plugin APIs 3–43. Wordspring uses API 24, the minimum for direct argv process execution; shared Luau modules require API 22. The runtime accepted and loaded the four entries at API 24.

## Automated checks

- Full KDE build: nine CTest suites pass, including the existing runner module, article widget/navigation, store, and importer tests; KConfig compatibility; synthetic CLI integration tests; Noctalia manifest validation; native shell settings lint; and the Luau callback harness.
- Build with `WORDSPRING_BUILD_KDE=OFF`: six suites pass. The CLI links Qt Core and SQLite without KDE or Qt Widgets.
- `noctalia plugins lint wordspring`: zero errors, zero warnings.
- All shipping Luau scripts compile with Luau 0.728. The callback harness executes their actual source in separate simulated entry/module environments, checking empty/loading states, debounce, query and A/B/A races, late results after close, missing process, timeout, truncated output, protocol mismatch, article actions, configuration errors, IPC, and bar orientation. Control props are checked against `noctalia.d.luau`.
- CLI tests use isolated XDG locations and synthetic dictionaries for Unicode/alias lookup, enabled sources/order/limits, legacy schemas, semantic article blocks/internal links, bookmarks (including removed dictionaries), bounded history, configuration escaping, import failure/timeout, and offline operation.
- Both release archives and installers were exercised in temporary homes. The KRunner package contains its plugin, KCM, viewer, CLI, and importer; the Noctalia package contains the scripts, CLI, and importer without KDE modules. Uninstall preserves dictionary/user data. The script-only archive and catalog are validated too.
- PR CI retains the current Arch KRunner build and adds Debian 13 KDE/CLI and CLI-only builds, archive/installer smoke tests, and a pinned Luau test job.

## Live shell checks

Live tests used a synthetic dictionary and temporary settings; no existing imported dictionary or bookmark/history database was changed. The CLI also read the existing three enabled ODE/WISDOM/ZH-EN sources successfully, without recording visits.

| Check | Result |
| --- | --- |
| Plugin installation/enable and module hot reload | Passed |
| Launcher query and visible summary | Passed |
| Pointer activation of a launcher result opens the full panel | Passed |
| Panel lookup through singleton IPC | Passed |
| Full article, semantic blocks, and related word displayed | Passed |
| Horizontal bar widget rendering and actual click toggling | Passed |
| Empty and word-not-found states | Passed |
| Lookup with Noctalia offline mode enabled | Passed; original offline setting restored |
| Missing CLI and timeout handling | Automated CLI/Luau tests pass; live settings exercised, but visual confirmation was obstructed by other desktop overlays |
| Native keyboard focus, Enter activation, and Escape | Not reliably verified: simulated keyboard events were not consistently routed to the intended shell surface |
| Native clipboard button and bookmark pointer activation | Not verified end to end; callback/state behavior is covered by the Luau/CLI tests |
| Vertical bar rendering | Orientation logic tested in Luau; the desktop bar was not moved |

No script/runtime errors attributable to Wordspring were found in the Noctalia log during successful launcher, panel, IPC, and widget tests. The native launcher `detail` presentation was replaced with standard rows after live testing: standard rows provide the expected pointer activation for dictionary results.

## Remaining manual/release checks

- Check focus, Enter, Escape, clipboard buttons, and bookmarks interactively in the user's compositor without overlapping keyboard/screenshot overlays.
- Exercise a vertical bar if using one, and optionally perform a fresh Apple-bundle import with the user's PyGlossary environment. The unchanged importer and its Apple semantic parsing are covered by synthetic tests; no licensed source was reimported during these checks.
- The interactive KRunner/Plasma session was not tested on the live Niri desktop. Its module loading, backend, and offscreen article controls are tested locally and in CI.
- The release workflow builds both frontends on native x86_64 and arm64 runners, and publishes only for `v*` tags. A branch `workflow_dispatch` can verify the binary jobs without publishing a release.

Noctalia has no HTML browser component; article text is converted to native semantic blocks and related-word buttons. Precise Qt CSS/inline emphasis is not reproduced. Captured shell processes are limited to 60 seconds; panel imports stop at 55 seconds, and longer imports use the CLI directly.
