---
name: ffgl-tox-effect
description: Author or debug a TouchDesigner .tox component so it works correctly as an FFGL effect/source in Resolume via FFGLTouchEngine. Use when creating a new tox effect for Resolume, converting an existing TD component, or diagnosing why a tox's parameters/textures misbehave in Resolume.
---

# Building a .tox effect for FFGLTouchEngine (Resolume)

FFGLTouchEngine ships two generic FFGL plugins — **TouchEngine** (source/generator,
FFGL ID `TE01`) and **TouchEngineFX** (effect, ID `TEFX`). One dll/bundle serves all
toxes: the user points the plugin's "Tox File" parameter at a `.tox` at runtime.
There is no per-effect build step — "making an effect" means authoring the tox to
the wrapper's conventions.

## Hard requirements

1. **TOP naming (exact):**
   - Generator (source): output TOP named `out1` inside the component (any texture
     out is accepted in practice, but use `out1`).
   - Effect (FX): input TOP **must** be named `in1` (strictly matched) and output
     `out1`.
2. **License:** the machine running Resolume needs a TouchDesigner
   Commercial/Pro/Educational license. Non-commercial is not supported.
3. **Same GPU:** TouchEngine and Resolume must run on the same GPU. On dual-GPU
   laptops, pin both `Arena.exe`/`Avenue.exe` and `TouchEngine.exe` to the same GPU
   in the NVIDIA Control Panel or output stays black ("Failed to create interop").
4. **Pixel formats: keep every TOP at 8-bit** ("Use Input" format). 16/32-bit float
   TOPs at the output render black in Resolume (interop is 8-bit only — issue #12).
   If the network needs float precision internally (feedback/sims), convert to 8-bit
   with a final Null/Out TOP set to RGBA 8-bit fixed. (Resolume 7.24+ can run 16 bpc
   compositions, but the wrapper's interop is still 8-bit — see
   docs/knowledge/resolume-bitdepth.md.)
5. **Resolution:** set it inside the tox (or expose as parameters). The wrapper does
   not force the comp to Resolume's composition size.
6. **Timing:** TouchEngine is cooked externally at a hardcoded 60 fps. Do not rely
   on `absTime.seconds` for rate-critical animation on non-60Hz rigs (known bug:
   runs fast on 144 Hz monitors — issue #17). Prefer `me.time.frame`-derived or
   parameter-driven time.

## Custom parameters — what maps and what doesn't

Expose parameters on the component (Component Editor "customize"). Per-type slot
limit is 40 (README says 30 — stay ≤30 to be safe). Global cap 240.

> **On drmbt's fork (`modernize-te` and later), several rows below are out of
> date** — slot names, menu behavior, and the Resolume→TD-only direction all
> changed. See [FORK-GUIDE.md](../../../FORK-GUIDE.md) for the current,
> authoritative behavior (slot naming, float range remap, and the echo-channel
> authoring convention). This table describes upstream v2.0.4 unless noted.

| TD parameter | Maps to | Notes |
|---|---|---|
| Float | FFGL float slider | Upstream: UI min/max passed via SetParamRange. **On the fork:** the wire is a normalized 0-1 position remapped against the TD range in both directions — see FORK-GUIDE.md migration notes. |
| Int | FFGL integer | UI min/max respected; historic 1000 cap raised |
| Menu (int) | FFGL dropdown | max 10 choices; string-typed menus are NOT choice-mapped. Upstream: a second menu on the same tox collides with the first (shared ParamID) — **fixed on the fork** (per-family ParamID counters, `feat/slot-naming-ranges`). |
| Toggle | FFGL boolean | |
| Pulse | FFGL event button | auto-resets after send |
| Momentary | FFGL event button | historically buggy (registered as pulse; shared OSC address — issue #33); prefer Pulse |
| String | FFGL text field | |
| RGBA (color intent) | FFGL color picker | added Apr 2026; uses 4 slots per color |
| XY / XYZ / WH vectors | one float per component | XY default-value bug: may load as 0 instead of default (issue #27) |
| CHOP / DAT / audio in | **NOT supported as an input.** On the fork, an **output** FloatBuffer/StringData link is used deliberately — see echo channel below. | Upstream drops these link types silently either direction. |
| Header | separator | historically broke all following params (fixed v1.1.1); avoid if targeting old plugin builds |

Parameter gotchas:
- Parameter order in Resolume (and its OSC addresses) has been nondeterministic
  across loads — don't build OSC mappings that assume stable ordering (issue #33).
  **On the fork**, every pre-allocated slot has a unique static name
  (`Float1`…`Float40`, `Menu1`…, `Pulse1`…, `Color1R`…), so this is fixed for
  the fork's own slots — but is still true of parameter *insertion order within
  a family*, which follows enumeration order, not a stable per-tox identity.
- **Upstream:** values only flow Resolume → TD; TD-side changes are not pushed
  back (open RFE #28). **On the fork:** dirty-only push lets TD-side changes
  (preset recall, interpolation, randomize) survive, and a tox can reflect its
  own parameter state back to Resolume via a Par CHOP/DAT → Out CHOP/DAT — see
  "Echo channel (fork only)" below.
- Defaults are captured at load; test Reload after changing defaults in the tox.

## Echo channel (fork only, `feat/dynamic-params`+)

TouchEngine cannot reflect writes to **input** parameters back to the host —
comp-internal writes to root custom pars are invisible to the plugin. To
expose TD-side parameter state (e.g. after a preset recall) in the Resolume
UI/REST, publish it on an **output** instead:

- Preferred: a Par DAT pointed at the component → **Out DAT**
  (`name`/`value` rows, header optional). Handles menus (token → index) and
  strings, not just numbers.
- Optional: a Par CHOP → **Out CHOP** (numeric only; menus arrive as a raw index).
- Channel/row names must be the TD par name, lowercase-suffixed for vector
  components (an RGBA par `Rgba` → `Rgbar`/`Rgbag`/`Rgbab`/`Rgbaa`).
- A host-driven change wins for ~30 frames after each push (settling window),
  so echoing your own just-set value back doesn't revert it.
- Needs a modern TouchEngine build behind the tox — an old install may expose
  texture output only, silently breaking this. The fork's engine-preference
  logic (macOS only) steers around that for unpinned folders.

Full detail: FORK-GUIDE.md → "Echo-channel authoring convention".

## Authoring checklist

1. Base COMP with `in1` (FX only) and `out1` TOPs; promote extensions off; nothing
   references paths outside the component (fully self-contained; external file
   references must be absolute or params).
2. All TOPs 8-bit; final `out1` explicitly RGBA 8-bit.
3. Custom params: ≤30 per type, meaningful ranges + defaults, labels set (Resolume
   shows the label, not the parameter name).
4. No reliance on realtime flag, audio devices, video device inputs (parameters from
   device-input toxes have failed to appear — issue #27 tail), or UI/panel COMPs.
5. Save as `.tox`. Test locally in TouchDesigner in Perform mode first.

## Deploying and testing

- **Windows:** `FFGLTouchEngine.dll`, `FFGLTouchEngineFX.dll`, and `TouchEngine.dll`
  together in `Documents\Resolume Arena\Extra Effects` (or the configured plugin
  dir). TouchDesigner 2023.11880+ recommended; TD 2025 experimental has known audio
  breakage with the plugin. **This repo's Windows TouchEngine SDK is still the
  2023.11780-era headers/lib/dll** — the fork's macOS framework update
  (c3ceb1a, 2025-06-13) has no Windows counterpart yet, and the macOS-only
  engine-preference scan means an unpinned tox on Windows may still resolve to
  whatever TD build TE picks by default. Verify against FORK-GUIDE.md's Windows
  checklist before trusting a build here.
- **macOS:** `FFGLTouchEngine.bundle` / `FFGLTouchEngineFX.bundle` (TouchEngine
  .framework embedded, ad-hoc signed) into the Resolume plugin folder. macOS 13+.
- In Resolume (7.23.x tested): Sources → TouchEngine (or Effects → TouchEngineFX),
  set "Tox File". Params appear once TE loads. Use the Reload event param after
  swapping the tox on disk; note Unload/Reload has a param-duplication history
  (issue #34) — restart Resolume if params look doubled.

## Debugging quick table

| Symptom | Likely cause |
|---|---|
| Black/transparent output | float pixel format at out (use 8-bit); dual-GPU mismatch; missing TD license |
| Params missing after a Header | old plugin build (<v1.1.1) |
| Animation too fast | absTime + non-60Hz display (issue #17) |
| Pulse fires twice / behaves like momentary | pre-2.0.3 build (issue #33, unverified fix) |
| Duplicated params after reload | issue #34 upstream; on the fork, a Reload segfault root cause (InstanceReady mis-semantics) is fixed — see FORK-GUIDE.md |
| Only first params work | ancient build (fixed b607696) |
| Menu shows wrong choices with 2+ dropdowns | upstream param-ID collision bug (two menus share ID) — **fixed on the fork** |
| TD-side value changes don't show up in Resolume | upstream limitation (RFE #28) — **the fork adds an echo channel**, see above; needs a Par CHOP/DAT → Out CHOP/DAT in the tox |

Deep references: `FORK-GUIDE.md` (fork behavior, branch map, migration and echo
conventions), `docs/knowledge/codebase-notes.md`, `docs/knowledge/issues-audit.md`
in this repo.
