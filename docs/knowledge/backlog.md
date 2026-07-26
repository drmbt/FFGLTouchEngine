# Backlog — durable, cross-session

**This is the long-term list.** Everything here outlives a session. Dated
`test-results-*.md` files are snapshots of what was true on a given day and are
never edited afterwards; `HANDOFF.md` was a point-in-time handoff and is now
historical. When a session finishes an item, update it *here* and note the
evidence in that session's test-results file.

Status: `OPEN` · `INVESTIGATING` · `BLOCKED` · `DONE (date)`.

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

## 3. Resource lifetime: engines are not released on eject — `OPEN`

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

**Open sub-question, not yet measured:** whether opening a *saved* composition
instantiates every TE plugin (and therefore spawns every engine) eagerly at load,
or lazily on first use. The 2026-07-25 measurements all started from clips
created in-session, so this was never isolated. It matters a lot for
session-open time and for peak memory. Measure by counting engines immediately
after opening a comp with several TE clips, before touching anything.

## 4. Debug/logging string parameter — `OPEN` (feature request)

Idea: an empty string parameter the plugin writes diagnostics into (severe
warnings, load times), surfaced in the Resolume UI and over REST/OSC, so
problems are visible without tailing the Arena log.

Feasible — the machinery already exists (`SetTextParameter`/`GetTextParameter`
plus the `FF_EVENT_FLAG_VALUE` raise added for #28). Design constraints and open
questions are in **[logger-param-design.md](logger-param-design.md)**.

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

## 6. Upstream issues worth re-checking

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
