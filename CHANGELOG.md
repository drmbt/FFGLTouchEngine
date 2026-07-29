# Changelog — drmbt/FFGLTouchEngine fork

All changes on this fork relative to upstream
[medcelerate/FFGLTouchEngine](https://github.com/medcelerate/FFGLTouchEngine)
v2.0.4, newest first. Maintained per-commit so it can seed an eventual
upstream PR description. [FORK-GUIDE.md](FORK-GUIDE.md) maps each change below
to the branch that carries it (`fix/stability`, `feat/slot-naming-ranges`,
`feat/dynamic-params`) and holds the migration and tox-authoring notes. Full
investigation notes live in [docs/knowledge/](docs/knowledge/README.md).

## v3.6.0 — 2026-07-29 (branch `feat/te-fx-presets`)

### Added
- **New plugin variant: `FFGLTouchEngineFXPresets` — "TEFX Presets" (ID `TEFP`,
  v0.1.0).** The FX plugin plus two opt-in features carried by the shared base
  (both OFF for the existing plugins, whose param layouts and behavior are
  byte-for-byte unchanged). Ported from the proven implementations in
  drmbt-custom-fx (`ffgl-color-picker` / `ffgl-preset-morph` skills;
  `sdk/drmbt/PresetMorph.*` copied to `src/plugins/shared/`).

  - **Native Resolume color pickers for TD-side RGBA parameters**
    (`UseHsbaColorQuads`). The pre-allocated color family is declared as
    consecutive `HUE→SATURATION→BRIGHTNESS→ALPHA` runs (`Color1`, `Color1_sat`,
    `Color1_bri`, `Color1_alpha`, …) — the type run is what makes Arena render
    its internal picker (PICK/HSB/RGB/Palette tabs + alpha strip); RGBA-typed
    runs only ever render as loose sliders (verified in Arena 7.27.1, per the
    drmbt findings). The host wire carries HSBA while TE keeps straight RGBA:
    `ParameterMapFloat` for the children stays RGBA (vector push/echo paths
    untouched) and a per-quad `ColorQuadHsba` holds the host-authoritative HSBA
    so hue survives grays (RGB→HSB is undefined there — deriving on every read
    would snap hue back to red at brightness/saturation 0). Conversions happen
    at the host boundary (`SetHsbaChannel` / `RefreshQuadHsbaFromRgba`),
    including the TD→host echo and idle-release restore paths.

    This is a NEW plugin rather than a change to TouchEngineFX because the
    switch changes what stored color values *mean*: a comp saved against
    RGBA-typed slots would reinterpret red (1,0,0) as hue 1/sat 0/bright 0 =
    black.

  - **Host-preset recall with morph** (`EnablePresetControls`): a six-param
    block appended after all pre-allocated families (indices 287–292, so no
    tox-driven slot moves) — `Preset` (menu scanned from Arena's own per-effect
    preset XMLs under `Documents/Resolume Arena/Presets/Video Effects/TEFX
    Presets/`; element 0 = None), `Morph` (0–10 s glide, default 0.5),
    `Recall`, `Rescan` (live menu rebuild via `FF_EVENT_FLAG_ELEMENTS`,
    selection re-matched by name), `Snap` (momentary instant-recall modifier;
    finishes an in-flight glide, settling the double-mapping race), `Curve`
    (14 monotonic easings, default SineInOut). Preset *saving* stays native
    host UI (the P. dropdown) — the plugin only reads those XMLs. Recall
    carries values only and never touches the Tox File slot; floats and color
    quads glide (hue shortest-path around the wheel), ints/bools/menus snap,
    text and pulses are excluded. The glide is clocked on rendered-frame deltas
    (clamped), so a bypassed/ejected clip pauses instead of finishing on wall
    time. Host restores (comp load, native P. recall) are adopted without
    firing a surprise recall via the write-burst guard from drmbt — only slots
    a recall itself writes may arm it.

  Plugin display name: FFGL's `PluginName` is a hard 16-char field, so the
  intended "TouchEngineFX_presets" cannot fit on the wire — the shipped name is
  **"TEFX Presets"** (also the preset folder name, which must match exactly).

  Verified headlessly (probe_dll: scan lifecycle + full 293-param dump matches
  design). Not yet smoke-tested in Arena — picker rendering, preset round trip,
  and morph feel need the in-host pass.

## v3.5.0 — 2026-07-29 (branch `modernize-te`)

### Fixed
- **Resolume presets now recall tox parameter values, not just the tox path.**
  Two halves, both required:

  - **Same-path load guard.** Resolume re-sends the Tox File path on every
    preset recall (and on the second recall of the same preset). `SetTextParameter`
    reloaded unconditionally, tearing down the live comp — observed to come back
    `TEResultComponentErrors`, leaving a dead instance — and re-arming the
    not-ready guards right before the preset's values arrived. An identical path
    against an instance that is loaded or loading is now a logged no-op; the
    Reload slot remains the way to force a real reload.

  - **Pending host-value cache.** Preset recall sprays every slot's value while
    the load the path just triggered is still in flight; those sets were silently
    dropped by the not-ready guards, and enumeration then overwrote everything
    with the tox's own saved state anyway. Values arriving with no tox enumerated
    are now stashed raw (`FF_TYPE_STANDARD` still 0-1 normalized — ranges don't
    exist yet) and applied at the end of `GetAllParameters`: after the
    idle-release retained-value restore (an explicit host set is newer intent),
    before the final `RaiseParamEvent` sweep (so the host re-reads preset values,
    not tox defaults), and routed through the dirty-only push so the par echo's
    `LastPushFrame` guard protects them. Pulse-family slots are never stashed — a
    recalled preset must not fire events. The cache clears on path change,
    Unload, Clear, and after enumeration; values matching no slot (preset saved
    against a different tox layout) are dropped with a log line.

  Caveat unchanged from any dynamic-param FFGL plugin: a preset is only valid
  for the tox layout it was saved against — editing the tox's parameters shifts
  slot assignment.

- **Missing tox routed to Resolume's video relink dialog** (which cannot list a
  `.tox`, so the preset was unrecoverable). The Tox File slot was declared with
  bare `SetParamInfof(..., FF_TYPE_FILE)` — no extensions — so Resolume treated
  it as generic media. Now declared via `SetFileParamInfo(0, "Tox File",
  { "tox" }, "")` (FFGL 2.2 `FF_GET_FILE_PARAMETER_EXTENSION`). Needs in-Arena
  verification of both the browse filter and the relink routing.

## v3.4.1 — 2026-07-26 (branch `modernize-te`)

### Fixed
- **`Idle Seconds = 0` would have killed a playing clip.** The watchdog tested
  `idle >= IdleReleaseSeconds`, and at 60 fps a *live* clip is only ~16 ms past
  its last frame — so with a threshold of 0 that test was true on every tick and
  the engine was released out from under a clip that was still on screen. Worse,
  it would not have come back: `Connect()` does not fire again for a clip that
  is already connected, so it would have stayed black until re-triggered.

  A render-gap detector fundamentally cannot express "immediately", because
  "rendering right now" and "stopped 16 ms ago" are the same measurement. The
  effective threshold is now floored at 1 s (`IdleSecondsFloor`), just above one
  watchdog tick, so 0 means "as soon as this can safely be detected".

  Verified both directions: a playing clip at `Idle Seconds = 0` survived 30 s
  untouched with its engine intact, and the same clip released within 2 s of
  being ejected.

## v3.4.0 — 2026-07-26 (branch `modernize-te`)

### Added
- **`Idle Seconds` — the release threshold is now per clip**, 0–300, default 20.
  One global value cannot serve both a workhorse effect that gets cut back to
  and a one-shot fired once for a track: too short and a return stalls for
  25–40 s, too long and the memory is never reclaimed. Raise it for anything
  re-fired inside the window; lower it for material that is done once played.

  `0` means "as soon as this can safely be detected" — about a second in
  practice, see the floor in v3.4.1. Low values are legitimate but fragile:
  any gap in rendering longer than the threshold costs a full reload, and a
  dropped-frame hitch is indistinguishable from a clip going away.

  Registered as `FF_TYPE_INTEGER`, not `FF_TYPE_STANDARD`: the SDK's
  `SetParamInfo` hard-clamps a STANDARD default into `[0,1]` *before* any range
  is declared, so a default of 20 silently arrived as 1 and every clip released
  after a single second. Integer defaults pass through untouched, and whole
  seconds is the right granularity regardless.

### Notes on what the threshold measures
The watchdog checks exactly one thing: **how long since `ProcessOpenGL` was last
called**. There is no notion of "playing", "connected" or "on screen" — only
whether the host has recently asked the plugin to draw. Two consequences:

- Swapping to a different clip on the same layer does **not** release the old
  one immediately; its clock starts at its last frame and it releases
  `Idle Seconds` later. Nothing tells the displaced clip it was displaced —
  Resolume calls neither `Disconnect()` on it nor anything else — so
  "B took the layer" and "the frame loop paused" are the same observation.
  During that window both engines are resident.
- Re-firing **within** the threshold is a complete no-op, not an unload/reload:
  the instance was never released, so rendering simply resumes with no reload,
  no re-enumeration and no parameter round trip.

## v3.3.0 — 2026-07-26 (branch `modernize-te`)

### Added
- **`Release On Idle` toggle, on by default.** A TouchEngine process costs
  ~1.3–1.5 GB and was held for the life of the clip: ejecting a layer freed
  nothing, so a timecode-driven set ratcheted memory upward all night. With this
  on, a clip that stops rendering for 20 s hands its engine back; arming the
  clip again brings it straight back.

  It does what **Clear Instance** does, not what Unload does. Unload is the
  shallower operation — it keeps the instance alive, which is why Reload after
  Unload is instant — and it does not return the memory. This drops the tox
  parameters and the engine together.

  **Parameter values survive the round trip.** They are snapshotted at release
  and re-applied (and re-pushed to TE) after the tox comes back, because
  enumeration would otherwise reset every slot to the tox's own defaults — and
  since the host re-*queries* rather than pushes, Resolume would adopt those
  defaults and the composition would silently lose its tweaks.

  Two findings shaped the design, both verified with an instrumented build:

  - **Resolume never calls FFGL's `Disconnect()` when a clip is ejected** (only
    `Connect()` when it is armed), so there is no host callback for
    deactivation. Idleness has to be inferred from the render loop going quiet,
    which is what the watchdog does.
  - **Reload must be driven by `Connect()`, never by rendering.** Selecting a
    clip to preview it renders continuously and is indistinguishable from
    playback frame-by-frame, so a render-driven reload turned every clip
    selection into a 25–40 s engine start — and left a selected-but-stopped clip
    in an endless release/reload cycle. That was observed before the fix.

  Consequence worth knowing: **a clip left selected in the UI keeps
  preview-rendering and therefore never goes idle.** Click away for it to
  release.

### Changed
- **A manually cleared plugin now reloads when the clip is next armed.**
  Previously, after `Clear Instance`, neither previewing nor connecting brought
  the tox back and `Reload` had to be pressed per plugin. Arming the clip is now
  enough; previewing or selecting still leaves it cleared.

## v3.2.1 — 2026-07-25 (branch `modernize-te`)

### Changed
- **`Log` moved to the top, directly beneath `Clear Instance`** and above every
  tox-driven parameter, rather than trailing them. It now reads as part of the
  plugin's own header, which is where diagnostics belong.

  This shifts every pre-allocated family up by one index (`OffsetParamsByType`
  4 → 5). **That is not a breaking change**, and the assumption was verified
  rather than trusted: Resolume serialises FFGL parameters *by name* —
  `<Param name="Float1" .../>` in the `.avc` — and OSC/REST addresses are
  likewise name-derived. Confirmed end-to-end by reopening a composition saved
  under the old layout: `Float1` (0.565), `Int1` (239) and `Text1` ("Test") all
  restored onto the correct slots despite all 280 indices moving.

  The one thing this *would* affect is anything addressing the plugin by raw
  FFGL parameter index rather than through a host — nothing in normal Resolume,
  OSC or REST use does.

## v3.2.0 — 2026-07-25 (branch `modernize-te`)

### Added
- **`Log` diagnostic slot.** A read-only text parameter carrying the state you
  would otherwise have to tail the Arena log for: why a load failed, how long
  the load took, and what the engine is costing. It reports, in one line:
  the load result (`loaded in 25.02s`) or the error that stopped it, then
  `GPU <n> MB · CPU <n> MB · <n> fps · cook <n> ms`, plus a dropped-frame count
  **only when it is non-zero** (a permanent "0 dropped" is noise; the moment it
  moves it is the most important number in the row).

  Placement: **after every pre-allocated family**, so introducing it shifts no
  existing slot index and breaks no saved composition or OSC map. Because the
  unused slots are hidden, it renders directly beneath whatever parameters a
  loaded tox has exposed. It is the one slot that stays visible with nothing
  loaded — a failed load is exactly when it has something to say, and at that
  point no tox parameters exist.

  **Costs nothing per frame.** Memory and timing come from
  `TEInstanceStatistics`, which TouchEngine *pushes* on its own cadence
  (~1 Hz) rather than being polled, and the slot is written only from lifecycle
  events and that callback — never from `ProcessOpenGL`. The host is asked to
  re-query only when the composed line actually changes. The per-frame
  `Releasing texture` line removed in v3.1.0 (702 log entries in one short
  session) is the cautionary example this design avoids.

  Note on `fps`: it is derived from wall-clock between statistics deliveries,
  not from `frameTimeCPU`. `frameTimeCPU` is CPU time *spent*, so dividing by it
  yields throughput capacity — a frame cooked in 1.1 ms reads as "655 fps"
  rather than the 59 fps it is actually running at. `cook <n> ms` reports that
  CPU cost separately, as the headroom figure it actually is.

## v3.1.0 — 2026-07-25 (branch `modernize-te`)

A pass over long-standing upstream defects found by auditing the render loop
rather than by reproducing symptoms — several had never been reported because
they degrade slowly or only bite with more than one instance. Nothing here is
breaking: saved compositions and OSC/MIDI maps are unaffected, so this is a
minor bump. `PluginInfo` moves to `3.100` (FFGL exposes major.minor only).

**Verified live on Windows** (Arena 7.27.1, TouchDesigner 2025.33070 engine):
three simultaneous TouchEngine instances each rendering their own tox with
independent parameters (setting one instance's text left the others untouched —
the case that previously collided); 3× Reload on a playing instance with the
host surviving and re-enumerating each time; `Unload` cleanly stopping output
instead of leaving the render thread on a dead instance; the engine-pin
diagnostic firing verbatim on a real poisoned folder. Zero occurrences of
`Failed to set double value`, `skipping parameter`, `Releasing texture`, or any
interop error across the whole session log.

### Fixed
- **The generator rebuilt its Spout interop on every frame.** The resize test
  compared `GetGlType(RawTextureDesc.Format)` — a GL type enum — against
  `GLFormat`, which `FFGLTouchEngine` never assigns. It stayed `0` while
  `GetGlType()` never returns `0`, so the branch was taken every frame and ran
  `CleanupInterop()` + `CreateInterop()` + `CreateDX11Texture()` +
  `InitializeGlTexture()` each time. Now compares against `DXFormat` and
  assigns it, matching what `FFGLTouchEngineFX` already did correctly.
- **D3D immediate-context refcount underflow, once per frame, in both plugins.**
  `devContext` is a `ComPtr`; the explicit `devContext->Release()` dropped a
  reference the ComPtr destructor then dropped again. `keyedMutex` had the
  mirror-image bug: a raw pointer from `QueryInterface`, left uninitialised (so
  the null check read garbage on failure) and leaked on every early return. It
  is now a `ComPtr` and the `QueryInterface` result is checked.
- **Error paths reported success.** Six sites returned `FF_FALSE` on failure,
  but `FF_SUCCESS == 0 == FF_FALSE`, so a failed texture transfer was
  indistinguishable from a good frame. Now `FF_FAIL`.
- **`Unload` left the render thread running on a dead instance** (likely
  upstream #34). It suspended and unloaded but left `isTouchEngineLoaded` /
  `isTouchEngineReady` set, and unlike `Clear` it does not reset the instance —
  so `ProcessOpenGL`'s guard still passed. `Unload` and `Clear` now also clear
  `isLoadPending` / `isReloadQueued`, which could otherwise stick after an
  unload during an in-flight load and queue every later load behind one that can
  never complete.
- **Two undefined-behaviour fall-throughs** (`GlToDXFromat`, `GetGlType(GLint)`)
  returned whatever was in the return register for an unhandled format, and that
  value went on to describe a D3D texture or a GL upload. Both `C4715` warnings
  are now gone rather than suppressed.
- **Multiple TouchEngine clips corrupted each other.** Spout sender names came
  from `rand()`'s global sequence, which `FFGLTouchEngine` never seeded (every
  process produced the same name) and `FFGLTouchEngineFX` re-seeded from
  `time(0)` in its constructor (two hosts started in the same second matched).
  Now seeded per-thread from `std::random_device`. Separately, the
  texture-access mutexes were the literals `"mutex"`, `"mutex1"`, `"mutex2"` —
  Spout derives that mutex from a *sender* name, so every instance in every
  process shared one per role. They now pass the sender name.
- **Per-frame log I/O**: the FX texture-release callback wrote to the host log
  on every TE texture release, burying every other `FFGL:` line.

### Added
- **The engine pin is named when it is what refused the load.** A file-system
  link called `TouchEngine` beside the tox is a deliberate engine pin and
  overrides the preferred-engine path; when unusable, TE fails with
  `TEResultTouchEngineBadPath`, whose description names neither the file nor the
  folder. The plugin now points at the offending path, notes that every tox in
  that folder is affected, and — when the pin is a small plain file holding
  something path-shaped — quotes it and identifies it as a symlink that did not
  survive the trip between machines. This case cost a live set.

## v3.0.1 — 2026-07-25 (branch `modernize-te`)

**No plugin behaviour change: `git diff v3.0.0 v3.0.1 -- src/` is empty.** The
binaries are identical to what v3.0.0 would have produced. This release exists
because v3.0.0 never built — the CI workflow's only trigger was a tag push and
its release job had no ref guard, so no artifact was ever published for it. The
in-binary FFGL `PluginInfo` therefore stays at `3.000`; FFGL exposes only
major.minor, so the whole 3.0.x line reports the same version to the host.

### Changed
- **CI can actually release.** `workflow_dispatch` added, so a build can be
  produced from any branch without minting a tag; the release job is now gated
  on `refs/tags/`, which also prevents a non-tag run from publishing a release
  named after a branch. Fork releases are marked prerelease until the artifact
  itself — not merely the source it was built from — has been verified on both
  platforms.
- **The Windows zip's README no longer under-documents the install.** It listed
  only the two FFGL plugins, omitting the `TouchEngine.dll` the zip has always
  shipped and which the plugins cannot load without. It now says to copy all
  three, and carries the two Windows traps from the parity pass (the
  `TouchDesigner\bin` DLL is not the redistributable and fails every Configure
  with `TEResultBadUsage`; backup copies in plugin-dir subfolders break loading
  because Resolume scans recursively).
- **The Windows parity fixes are split back down the PR stack** — the `NOMINMAX`
  build fix onto `feat/slot-naming-ranges` (whose `std::min`/`std::max` provoke
  it) and the Windows engine preference onto `feat/dynamic-params` (beside the
  macOS counterpart). `feat/dynamic-params` also gained the explicit
  `<algorithm>` it had been missing — it uses `std::min`/`std::max` on ten lines
  while relying on transitive inclusion, which holds on libc++ but not MSVC.
  See [FORK-GUIDE.md](FORK-GUIDE.md).

## v3.0.0 — 2026-07-25 (branch `modernize-te`)

First fork release. Major version because the slot rename below is **breaking
for saved compositions and OSC/MIDI maps** — see
[Migration notes](FORK-GUIDE.md#migration-notes--featslot-naming-ranges-breaking).
Verified live on macOS (arm64) and Windows (x64); the in-binary FFGL
`PluginInfo` version moves 1.000 → 3.000 in both plugins, so the host now
reports the real version.

### Added
- **Newest-engine preference on Windows (parity with macOS)**: the
  `/Applications` scan was macOS-only, leaving Windows on TouchEngine's own
  engine resolution — the gap that silently kills the echo channel when TE
  picks an install whose engine exposes only texture outputs. Windows now scans
  for TouchDesigner installs and calls `TEInstanceSetPreferredEnginePath` with
  the newest one (Windows takes the installation *directory*, not a bundle).
  The build number cannot come from the directory name here — the newest
  install is normally the unsuffixed `TouchDesigner` folder — so it is read
  from `bin\TouchDesigner.exe`'s version resource, with candidates gathered
  from `HKLM\SOFTWARE\Derivative\TouchDesigner` (covers non-default install
  locations) and `%ProgramFiles%\Derivative`. Failures to set the preferred
  engine, and the no-install-found case, are now logged instead of silent.
  Verified on Windows: every load logs the 2025.33070 install.
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
- **Windows build broke on the float-range code**: `std::min`/`std::max` in the
  range remap hit the `min`/`max` macros from `windows.h` (6 sites,
  `C2589`/`C2059`). `NOMINMAX` is now defined — and `WIN32_LEAN_AND_MEAN` moved
  *above* the `windows.h` include, where it had never been — with both also set
  as compile definitions in CMake for translation units that reach `windows.h`
  through Spout first. `<algorithm>` is now included explicitly (it arrived
  transitively on libc++ but not MSVC). Windows had not been built since the
  range work landed.
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
