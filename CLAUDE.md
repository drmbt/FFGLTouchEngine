# FFGLTouchEngine — fork working notes

Fork of medcelerate/FFGLTouchEngine (FFGL plugins hosting TouchEngine in
Resolume). Remote `origin` = drmbt/FFGLTouchEngine (ours), `upstream` =
medcelerate. Branch `modernize-te` carries all fork work; `master` tracks
upstream v2.0.4.

## Branch policy — only `modernize-te` is maintained

**Work on `modernize-te`, build from `modernize-te`, release from
`modernize-te`.** It is the only branch that has to be correct.

`fix/stability`, `feat/slot-naming-ranges` and `feat/dynamic-params` exist
solely to make a future upstream PR easy to cherry-pick from. They are
**stale by design and deliberately not maintained** — do not rebase them,
re-split them, or forward-port fixes onto them as part of normal work. That
costs real effort every session and buys nothing until a PR actually happens.

**Before opening an upstream PR** (and only then): revisit those branches,
re-split them fresh from `modernize-te`, verify each builds standalone, and
check the acceptance invariant (`git diff feat/dynamic-params modernize-te --
src/` should be only the `PluginInfo` version lines). Treat whatever is on
them now as scratch — nothing on them is precious.

This fork has diverged from the maintainer's direction on purpose, with
breaking changes and different priorities, so the PR split is a
someday-and-on-our-terms exercise, not a running obligation.

## Rules

- **Every commit that changes plugin behavior must update `CHANGELOG.md`**
  (same commit or an immediate follow-up). It documents all deltas vs
  upstream v2.0.4 — for ourselves and as the basis of an eventual upstream
  PR. Keep entries grouped Fixed/Changed/Added with commit hashes, and keep
  the "Known issues" list current.
- Investigation notes, test results, and session handoffs go in
  `docs/knowledge/` (see its README for the index).
- Build dirs (`build-baseline/`, `build-modern/`), `tests/` (contains
  machine-specific `TouchEngine` symlinks that pin the engine build), and
  `Example/*.tox` test probes stay untracked.

## Build & install (macOS)

- Configure once: `cmake -B build-modern -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build-modern --config Release`
- Install: replace `FFGLTouchEngine.bundle` and `FFGLTouchEngineFX.bundle` in
  `~/Documents/Resolume Arena/Extra Effects/`, then restart Arena.
- Plugin backups live OUTSIDE the scanned tree in
  `~/Documents/Resolume Arena/_plugin-backups/` — Resolume recursively scans
  Extra Effects subfolders and duplicate FFGL IDs break loading.

## Testing

- Arena log: `~/Library/Logs/Resolume Arena/Resolume Arena log.txt`
  (plugin lines prefixed `FFGL:`).
- Test toxes: `tests/engine-<build>/` — the `TouchEngine` symlink next to a
  loaded tox pins which TouchDesigner build hosts the engine.
- Reload on a playing FX clip is safe now (the TE lifecycle mutex and the
  `InstanceReady` fix landed); the 3× gauntlet is part of the standard check
  on both platforms.
- Windows: `TouchDesigner\bin\TouchEngine.dll` is NOT the redistributable —
  ship `lib/TouchEngine/TouchEngine.dll` or every load fails `BadUsage`. And a
  macOS `TouchEngine` symlink pin that reaches Windows arrives as a plain text
  file that poisons every tox in its folder; the plugin now names it in the log.
- FFGL constraint worth remembering: slot names are static (host reads them
  at scan time, uses them for OSC/REST addresses and event-button captions);
  only display names can change at runtime.
