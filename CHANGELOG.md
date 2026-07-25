# Changelog — drmbt/FFGLTouchEngine fork

All changes on this fork relative to upstream
[medcelerate/FFGLTouchEngine](https://github.com/medcelerate/FFGLTouchEngine)
v2.0.4, newest first. Maintained per-commit so it can seed an eventual
upstream PR description. Full investigation notes live in
[docs/knowledge/](docs/knowledge/README.md).

## Unreleased / branch `modernize-te`

### Added
- **Par-state echo channel (#28 second half — TD→host reflection)**: since TE
  input-link values are host-authoritative (see below), the tox can instead
  expose its parameter state through outputs, for which TE does fire per-cook
  `ValueChange`. Convention: a Par CHOP → Out CHOP (`TELinkTypeFloatBuffer`,
  channels named like the par components, e.g. `Rgbar`) and/or a Par DAT →
  Out DAT (`TELinkTypeStringData`, `name`/`value` rows with optional header).
  The plugin registers the first FloatBuffer and first StringData output link
  (at enumeration or when added late) and maps TD par-component names back to
  FFGL slots — floats/ints/toggles from either channel, menu tokens (DAT) or
  indices (CHOP) to option slots, strings from the DAT. Echo values never
  mark parameters dirty, so they are not pushed back. A pending host set
  always beats the echo (dirty-wins guard). Verified end-to-end with a
  preset-recall tox: TD-side writes now appear in the Resolume UI/REST.
  A per-param settling window (30 frames after a host push) drops stale
  echoes of the just-pushed par, so a menu that is part of its own echo set
  no longer gets reverted by a pre-push cook — no tox-side Select needed.
  Verified end-to-end on an unpinned tox: preset recall reflects colors and
  floats into the Resolume UI/REST, menu selections stick.
- **Newest-engine preference (macOS)**: left to its own devices TE resolved
  unpinned tox folders to an old TouchDesigner install whose engine exposes
  ONLY texture output links (no CHOP/DAT outputs — silently breaking the
  echo channel; symptom: `GetLinkGroups(output)` returns a single texture
  child). The plugin now scans `/Applications` for the newest
  `TouchDesigner.<build>.app` and calls `TEInstanceSetPreferredEnginePath`,
  so toxes load a modern engine from ANY folder. A `TouchEngine` file-system
  link next to the tox still overrides this (deliberate pinning), and the
  configured engine path is logged at every load.
- **Superseding loads are queued, not dropped**: changing the Tox File (or
  pulsing Reload) while a load was in flight used to be silently ignored,
  leaving the old tox running behind an updated path display. The request is
  now queued and replayed as soon as the in-flight load completes.
- **Dynamic parameter updates (#28) — TD-side changes now persist**:
  `PushParametersToTouchEngine` pushes only host-modified (dirty) parameters
  instead of every parameter every frame — the blanket push was stomping
  TD-side changes one frame after they happened (verified live: a
  Parameter-Execute preset recall inside the tox now sticks). A
  `TELinkEventValueChange` handler reads changed links back into the
  parameter maps and raises `FF_EVENT_FLAG_VALUE` (Resolume 7.4.0+);
  TE-originated values are stored without dirtying (echo guard).
  **Architectural finding (verified empirically): full TD→host reflection of
  input parameters is impossible in TouchEngine's model** — comp-internal
  writes to root custom pars emit no ValueChange events AND are invisible to
  `TEInstanceLinkGet*Value` (input link values are host-authoritative; traced
  with instrumented builds against a preset-recall tox). Reflecting TD-side
  state into the Resolume UI therefore needs an output-link channel — e.g. a
  tox convention of an Out CHOP with par-named channels, for which TE does
  fire per-cook ValueChange — tracked as a future item.

### Fixed
- **Reload segfaulted the host — root cause: `TEEventInstanceReady`
  mis-semantics** (upstream bug, likely also behind #34-class instability).
  Per the `TEInstanceConfigure` docs, `InstanceReady` means "configure
  completed, ready to load" — and during a reload the previously loaded comp
  has just been *unloaded* at that point. The plugin treated it as
  render-ready, so per-frame texture pushes resumed into links that no longer
  existed (`TEInstanceLinkSetTextureValue` → null link → SIGSEGV, reproduced
  3× live). Render-readiness is now granted only after
  `DidLoad(success) → Resume → enumeration`. Verified: three consecutive
  Reloads on a playing FX clip, host alive, full re-enumeration each time.
  Defense in depth added alongside: load requests are debounced while a load
  is pending (`isLoadPending`), a `TELinkEventRemoved` safety net drops
  readiness if TE tears down a link we hold (holds last frame instead of
  crashing), and a failed load no longer resumes/enumerates a dead instance
  (which was the remaining "Failed to set double value" spam source —
  whole-log count after the gauntlet: zero).
- **Reload segfault / TE thread races**: a recursive mutex now serializes the
  TE instance lifecycle (load/unload/reload/clear), parameter-map mutation
  (enumeration runs on the TE callback thread), and the render thread's
  per-frame TE section in both plugins. Previously, pulsing Reload on a
  playing FX clip could crash the host (`TEInstanceLinkSetTextureValue` on a
  freed link from the render thread) and failed loads left params pushing
  into dead instances every frame. Frame-completion events bypass the lock
  (atomic flag) so TE cook completion never waits on a held frame section.
  This is also the prerequisite for dynamic parameter updates (#28).
- **Float sliders clamped TD values to the 0–1 prototype range**: the FFGL
  wire now carries a normalized 0–1 position and the plugin remaps it against
  the TD-side range on both directions (`ParameterRanges`, populated at
  enumeration and widened to include the initial value so out-of-range
  defaults on unranged floats survive). Real TD values are shown in the host
  readout via a `GetParameterDisplay` override. Color slots keep the native
  0–1 wire; ints remain unscaled within the ±10000 prototype range.
  OSC/MIDI/REST speak normalized positions (full range reachable; absolute
  TD values require sender-side scaling). Limitations: an unranged float
  cannot be pushed above its load-time value from the host, and the effective
  range is frozen until the next tox load.
- **Stale parameter display strings**: value events are re-raised for all
  active parameters after the enumeration walk completes, so the host
  re-queries rows whose display it cached while enumeration was mid-flight.
- **macOS red/blue channel swap** (`7fef691`): removed the `.bgra` swizzle from
  both plugins' output shaders. The IOSurface→GL binding
  (`CGLTexImageIOSurface2D` with `GL_BGRA` + `GL_UNSIGNED_INT_8_8_8_8_REV`)
  already yields correct RGBA when sampling, so the swizzle double-corrected —
  every TE output frame had red and blue exchanged on macOS.
- **Menu ParamID collision** (`7fef691`): a second menu parameter previously
  collapsed into the first (one FFGL slot drove both TE menus, the second menu
  never appeared). ParamIDs are now allocated from per-family counters instead
  of `ParameterMap*.size()` arithmetic; menus store their initial value and
  `GetFloatParameter` handles `FF_TYPE_OPTION`, so menus report real values
  before first interaction.
- **Spurious load error on fresh instances** (`7fef691`): `LoadTEFile` is
  guarded against an empty tox path, which used to log
  `TEInstanceLoad failed for ''` at every plugin instantiation.
- **macOS FX transparent-frame flicker** (`06697a9`): the busy/not-ready path
  in `TouchEngineFX::ProcessOpenGL` drew nothing and returned `FF_FAIL`
  whenever TouchEngine was mid-cook. It now redraws the cached
  IOSurface-backed last frame and returns `FF_SUCCESS`; the input base-pass
  draw also gained the previously missing shader/texture bindings.
  Live-verified: no flicker under continuous cooking.
- **Enumeration fragility** (`06697a9`): a failed group read aborted the whole
  parameter walk (dropping every parameter after it) and a failed value read
  could leave a half-registered parameter that spammed
  `Failed to set double value` every frame. Group/link failures now log and
  skip only the affected link; every branch reads all TE values before
  registering; unknown/future `TELinkType`s are logged and skipped; TE load
  errors are surfaced via `FFGLLog` with `TEResultGetDescription`.

### Changed
- **Unique FFGL slot names — breaking for saved compositions** (`7fef691`):
  pre-allocated slots renamed from `Parameter<N>`/`Color`/`Pulse` to
  `Float1–40`, `Int1–40`, `Toggle1–40`, `Text1–40`, `Menu1–40`,
  `Pulse1–40`, `Color1R/G/B/A`–`Color10R/G/B/A`, giving every parameter a
  distinct OSC/REST address (identically-named slots collapse to a single
  API entry, which had left all but the first event slot unreachable via
  OSC/REST). Event-button captions read the static name ("Pulse2") since
  FFGL cannot rename it dynamically; rows still show TD labels via display
  names. Compositions saved against the old slot names will not restore
  parameter values.
- **TouchEngine.framework updated** (`7bacc42`) to Derivative
  TouchEngine-macOS @ `c3ceb1a` (2025-06-13), replacing the 2023-era
  framework bundled with v2.0.4.

### Added
- **Knowledge base** (`9c8cf98`): `docs/knowledge/` — issues audit, codebase
  and FFGL SDK notes, sprint plan, per-session test results, session handoff.

### Known issues (tracked, not yet fixed)
- RGBA parameters render as 4 separate faders rather than Resolume's native
  color picker UI.
- Hardcoded 60 fps (#17); 32-bit tox renders corrupted on macOS (#12).
