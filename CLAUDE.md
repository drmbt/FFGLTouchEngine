# FFGLTouchEngine — fork working notes

Fork of medcelerate/FFGLTouchEngine (FFGL plugins hosting TouchEngine in
Resolume). Remote `fork` = drmbt/FFGLTouchEngine (ours), `origin` = upstream.
Branch `modernize-te` carries all fork work; `master` tracks upstream v2.0.4.

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
- **Do not pulse Reload on a playing FX clip** until the parameter/link mutex
  lands — it races the render thread and can segfault Arena.
- FFGL constraint worth remembering: slot names are static (host reads them
  at scan time, uses them for OSC/REST addresses and event-button captions);
  only display names can change at runtime.
