# Backlog — durable, cross-session

**This is the long-term list.** Everything here outlives a session. Dated
`test-results-*.md` files are snapshots of what was true on a given day and are
never edited afterwards; `HANDOFF.md` was a point-in-time handoff and is now
historical. When a session finishes an item, update it *here* and note the
evidence in that session's test-results file.

Status: `OPEN` · `INVESTIGATING` · `BLOCKED` · `DONE (date)`.

## Context: the tour

A touring show is being built on these plugins and will use them heavily.
**macOS is the primary machine, Windows is the backup**, so anything measured or
fixed on one platform needs confirming on the other before it can be relied on —
the two teardown paths are known to differ (see item 3). Resource behaviour and
load timing matter more than usual here: a set that ratchets memory upward all
night, or that stalls for 25–40 s when a clip is first fired, is a real
operational problem rather than a curiosity.

---

## 1. Frame rate is hardcoded to 60 fps — `OPEN` (upstream #17)

`TEInstanceSetFrameRate(instance, 60, 1)` in `LoadTEFile`. The reporter of #17
diagnosed this exactly: on a non-60Hz display, TouchDesigner's `absTime.seconds`
inside the tox runs at the wrong rate (~2× fast on 144Hz). Closed upstream with
no code change.

**Currently worked around by forcing Resolume to 60fps host-side.** That is a
real mitigation, not a fix — it constrains the whole show to 60fps.

**To do:** drive the rate from the host instead of the constant. FFGL does not
hand the plugin a frame rate directly, so the likely approach is to measure it
(EMA over `ProcessOpenGL` intervals) and call `TEInstanceSetFrameRate` when it
settles, or expose it as a parameter with 60 as the default. Test explicitly at
144Hz with a tox that animates from `absTime.seconds`, and confirm the tox's
motion matches wall-clock.

**Test it when the 60fps host lock is lifted** — that is the whole point of this
entry.

## 2. High-bit-depth textures — `INVESTIGATING` (upstream #12)

### What is already possible today, with no code change

If the high-precision work happens **inside the tox** — 32-bit noise driving a
displacement, depth-map math, etc. — none of it crosses the FFGL boundary.
TouchDesigner computes at full precision internally and only the *final* image
is handed to Resolume, which can be 8-bit. **This use case works now.** The
bit-depth limits below only bite when the high-precision data itself has to
reach Resolume, or when the *input* Resolume gives the FX needs to be >8-bit.

### What is broken

`NoiseOutOnly32Bit.tox` renders **black on Windows** (verified 2026-07-25,
Arena 7.27.1 / engine 2025.33070 — the tox loads, engine resolves, nothing is
logged, output is black). On macOS the same tox renders corrupted speckle.
Both match upstream #12, which was closed with a "future" promise and no commit.

### Why (exact sites, from codebase-notes.md)

- **Windows**: `InitializeGlTexture` hardcodes the unsized `GL_RGBA` internal
  format, so even a 16/32-bit TE output lands in an 8-bit GL texture.
  `GlToDXFromat` maps only RGBA8/RGB8/RGBA16 — no 16F/32F. `GetGlType(DXGI_FORMAT)`
  now has a `default:` that returns `GL_UNSIGNED_BYTE`, which silently downgrades
  rather than failing loudly — that default is why nothing is logged.
- **macOS**: BGRA8 is hard-forced everywhere (IOSurface bytes-per-element, Metal
  pixel format, `CGLTexImageIOSurface2D` type, FX input `TETextureFormatBGRA8Unorm`).
- TE itself supports `TETextureFormatRGBA16F` / `RGBA32F`.

### Plan

1. Decide the goal first: *pass-through* of high-precision data to Resolume, or
   just *correct rendering* of a tox that happens to output 32-bit (clamped on
   the way out). The second is much cheaper and probably what a displacement
   workflow actually needs — and would turn "black" into "correct 8-bit image".
2. Minimum for (2): give `InitializeGlTexture` a sized internal format chosen
   from the TE format, and extend `GlToDXFromat`/`GetGlType` to cover 16F/32F.
3. For real pass-through, add the host-side probe: FFGL has no texture-format
   contract, so what Resolume actually accepts must be determined empirically
   (see `resolume-bitdepth.md` — 7.24+ runs a 16bpc pipeline).
4. The FX **input** path (Resolume → TD) is separately 8-bit; a displacement
   effect that displaces *Resolume* content is limited by that, not by the noise.

## 3. Resource lifetime — `MITIGATED (2026-07-26, v3.3.0)`

**`Release On Idle` (on by default) now hands the engine back** ~20 s after a
clip stops rendering, and arming the clip brings it straight back. The
measurements below are what motivated it and still describe the behaviour with
the toggle *off*.

Design notes worth keeping, both established by instrumented builds:

- **Resolume never calls FFGL `Disconnect()` on eject** — only `Connect()` on
  arm. There is no host deactivation callback, so idleness is inferred from the
  render loop going quiet.
- **Reload must come from `Connect()`, never from rendering.** Previewing a clip
  renders continuously and is frame-for-frame indistinguishable from playback,
  so a render-driven reload made every clip selection a 25–40 s engine start and
  put a selected-but-stopped clip into an endless release/reload loop.
- **A clip left selected in the UI keeps preview-rendering and never goes
  idle.** Click away for it to release. Not a bug, but surprising.

Still open here: the Windows `DeInitGL` remains a no-op for the non-engine
resources (Spout interop, D3D textures/context) where macOS frees its
Metal/IOSurface objects. The idle release deliberately does not touch them — the
watchdog has no GL context — so that parity gap is unchanged.

### Original measurements (behaviour with the toggle off)

Measured 2026-07-25 (Windows, engine 2025.33070), by counting `TouchEngine.exe`
processes and their working set:

| Action | Engines | Total WS |
|---|---|---|
| TouchEngineFX added, **no tox set** | 2 (baseline) | 3.0 GB |
| **Tox File set** — clip *not* triggered | **3 (+1)** | 4.3 GB |
| Second FX, tox set, not triggered | **4 (+1)** | 5.8 GB |
| Clip triggered | 4 | — |
| **Layer ejected (clicked away to empty slot)** | **4 — nothing freed** | 6.0 GB |
| **`Clear Instance` pulsed** | **3 (−1)** | 4.5 GB |
| **Clip deleted** | **2 (−1)** | 3.0 GB |

**Findings.** An engine spawns when the *tox path is set*, not when the clip
plays — so a comp holds one engine per TE clip with a tox assigned, ~1.3–1.5 GB
each, whether or not anything is playing. Ejecting frees nothing. Only
`Clear Instance` or destroying the clip reclaims it.

That is arguably correct for live use (re-arming a clip is instant, no reload
stall mid-set), but it is not documented and it scales badly: ten TE clips is
~15 GB resident.

**Root cause in code:** `DeInitGL` on Windows only suspends+unloads the TE
instance and clears the (always-empty) `TextureMutexMap`. It does **not**
`instance.reset()`, does not clean up the Spout interop, and does not release
the D3D textures or context — the macOS path *does* free its Metal/IOSurface
resources. Reclamation only happens via `~FFGLTouchEnginePluginBase`, which does
reset the instance.

**To do:** (a) document this behaviour in the README — it is a real operational
constraint for large sets; (b) consider an opt-in "release engine when
deactivated" parameter for people who would rather trade re-arm latency for
memory; (c) bring the Windows `DeInitGL` up to parity with macOS so the
non-engine resources at least get freed.

**Session-open behaviour — RESOLVED (Vincent, 2026-07-25, observed on Windows):**
opening a saved composition does **not** spawn an engine for every TE instance in
it. Engines start on **first play** — and *preview does not count*. So a large
show file is cheap to open; cost accrues as clips are actually fired, and then
stays (see above: only `Clear Instance` or deleting the clip gives it back).

That reconciles with the table: assigning a tox to an **already-live** plugin
instance spawns the engine immediately, but a saved comp does not instantiate
the plugin until the clip is first played.

**Practical consequence for a show:** peak memory tracks *how many distinct TE
clips get fired over the night*, not how many exist in the file — and it only
ever ratchets upward. For a long set, budget for the worst case of every TE clip
having been touched at least once, or plan explicit `Clear Instance` pulses on
material that is done.

**Cold-start load time is significant.** Measured on Windows with the new `Log`
slot: **39.3s** for the first tox load in a session, **25.0s** for a subsequent
one (`NoiseOutOnly5Param.tox`, a trivial tox — this is engine spawn, not tox
complexity). Pre-warming anything needed mid-set is not optional at these
numbers. Worth re-measuring on the Mac and with realistic show toxes.

### Re-run this whole test on macOS — `OPEN`

The tour runs **Mac primary, PC backup**, and the numbers above are Windows-only.
The teardown paths are known to differ: the macOS `DeInitGL` *does* free its
Metal/IOSurface resources where the Windows one frees nothing, so the eject
behaviour may genuinely differ rather than merely being untested.

Repeat on macOS and record alongside: engine count/RSS after opening a saved
comp; after first play; after ejecting; after `Clear Instance`; after deleting
the clip; plus cold and warm load times. `TouchEngine` is a separate process
there too, so `ps`/Activity Monitor gives the same measurement. Then compare the
two platforms in one table so the backup machine's behaviour is known before it
is needed.

## 4. Debug/logging string parameter — `DONE (2026-07-25, v3.2.0)`

Shipped as the `Log` slot. Design rationale in
**[logger-param-design.md](logger-param-design.md)**; what it reports and how it
is kept off the render thread is in the CHANGELOG entry.

Remaining polish, if it proves wanted:

- A verbosity parameter (off / errors / verbose). Not added yet — the current
  content is already errors-plus-load-result-plus-stats, which is about the
  right volume. Add it only if the line proves noisy in practice.
- Per-instance history: it currently shows the *latest* status, not a scrollback.
  A short ring buffer was designed but not built, because a single line fits the
  Resolume row and a scrollback does not.
- **Verify on macOS** — the slot is in shared code, but the statistics callback
  and the numbers it reports have only been seen on Windows.

## 5. Smaller carried items

- **Colour picker**: RGBA renders as four separate faders rather than Resolume's
  native picker. May not be reachable through FFGL at all — needs a spike.
- **Int range remap**: ints are unscaled within the ±10000 prototype range;
  floats got the normalize/denormalize treatment, ints did not.
- **`AcquireSync(..., INFINITE)`** can hang the render thread if TE stalls. A
  finite timeout plus a dropped frame would be more robust for live use.
- **Dead code**: `TextureMutexMap` is declared and iterated but never populated;
  `CreateInputTexture`/`CreateOutputTexture` are commented-out blocks.
- **`README` 30-vs-40 parameter claim** — fixed 2026-07-25, listed here only so
  the audit's version of it is not re-reported.
- **Long soak**: nothing has run for hours. The refcount/interop changes in
  v3.1.0 are exactly the kind that surface over a full set.
- **macOS rebuild** after the v3.1.0 defect pass — the changes are shared-code or
  Windows-guarded, but that is inference, not verification.
- **Mixed multi-instance**: three simultaneous *sources* were verified; a
  source+FX+FX combination was not.

## 6. TEFX Presets variant needs its in-Arena smoke test — `OPEN`

v3.6.0 (branch `feat/te-fx-presets`) added `FFGLTouchEngineFXPresets` —
"TEFX Presets", ID `TEFP`: HSBA-typed color quads (native Resolume picker) plus
the drmbt-custom-fx preset-recall-with-morph block (`Preset`/`Morph`/`Recall`/
`Rescan`/`Snap`/`Curve` at indices 287–292). Built clean on Windows and passed
`probe_dll.ps1 -Params` (full 293-param layout matches design), but the probe
has no GL context and no host, so everything user-visible is unverified:

- [ ] Picker renders: load a tox with an RGBA par — the color param must show
      Arena's internal picker (PICK/HSB/RGB/Palette + alpha strip), not loose
      sliders. Verify picked color reaches TD correctly (esp. hue at gray:
      drag brightness to 0 and back — hue must survive).
- [ ] TD→host color echo (Par CHOP/DAT with `Colorr/g/b/a` channels) updates
      the picker live.
- [ ] Preset round trip: save presets from Arena's P. dropdown, `Rescan`, menu
      lists them; recall glides over `Morph` seconds with the chosen `Curve`;
      `Snap` jumps; recall must NOT reload the tox.
- [ ] Host-restore adoption: loading a comp / recalling a native P. preset with
      a stored `Preset` selection must adopt without a surprise recall (watch
      the log for exactly one "adopted without recall" line, per the
      ffgl-preset-morph skill's diagnostics).
- [ ] Existing TouchEngineFX/TouchEngine untouched: same comp loads, param
      layout identical (flags off — but confirm in-host).
- [ ] macOS build of the new target (CMake wiring mirrors the FX target,
      untested there).

Known naming constraint, not a bug: FFGL's PluginName is 16 chars, so the
display name is "TEFX Presets" (not "TouchEngineFX_presets") and the preset
folder is `Presets/Video Effects/TEFX Presets/`.

## 7. Upstream issues worth re-checking

From [issues-audit.md](issues-audit.md), with what is now known:

- **#34** (Unload/Clear/Reload misbehaviour) — plausibly fixed by the v3.1.0
  `Unload` flag fix. Worth re-reading the thread against current behaviour.
- **#33** second half — momentary still registers as pulse. The unique slot names
  fixed only the shared-OSC-address half.
- **#32** — the audit says CHOP/FloatBuffer handling is "structurally absent".
  **That is now out of date**: `TELinkTypeFloatBuffer` is handled for the echo
  channel. Re-read before citing.
- **#31** (dual-GPU black output) and **#21** (~16k textures) — unfixed and
  undocumented; at minimum a README note.
- **#27** — XY parameters reportedly do not retain defaults; not re-tested since
  the range work.
