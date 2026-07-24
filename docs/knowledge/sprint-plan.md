# FFGLTouchEngine Fork — Sprint Plan (drafted 2026-07-23)

> **STATUS 2026-07-23 (end of session 1)** — see HANDOFF.md for full pickup state.
> - Sprint 0: ~80% done. Fork created (drmbt/FFGLTouchEngine, remote `fork`),
>   baseline + modern-framework builds green on macOS arm64, baseline installed
>   in Extra Effects, test rig live. REMAINING: install/A-B the modern bundles,
>   Windows CI pass, commit docs/tests to the fork.
> - Sprint 1 probes: floats/ranges DONE (prototype-static confirmed — values
>   flow real, sliders clamp to 0–1); fps probe (#17) not run; FBO-format probe
>   not run; param-order probe not run; defaults probe partially (defaults >1
>   display OK per Vincent).
> - Sprint 2 verification: #34 not reproducible (macOS); #12 = corrupted (not
>   black) on macOS; #33 pulse-fires-clean but OSC collision structural
>   (all event slots named "Pulse", cpp:441); header/momentary/XY still blocked
>   on probe-tox ground truth.
> - NEW bugs found (jump the queue): macOS FX busy-frame transparency flicker
>   (TouchEngineFX.cpp:164-174); enumeration aborts on new 2025 par types +
>   silent failure when engine older than tox; reload "Failed to set double
>   value" spam; ~2.5 min enumeration on complex tox.
> - Next session: HANDOFF.md priority queue items 1–2, then 6 (A/B modern build).

Goal: run the most modern TD + TouchEngine against the most modern Resolume
(7.27.1 as of now), verify the hand-waved closed issues on that stack, and land
dynamic parameters (#28). Test harness: Resolume MCP (7.26+) + TDZeroMQ/two-zero
templates, driven from Claude Code.

Platform note: primary dev/test machine is macOS (Metal/IOSurface path). Spout
interop bugs (#12 Windows path, per-frame interop recreation) need a Windows pass
later — tag Windows-only items ⊞.

---

## Sprint 0 — Modern baseline (unblocks everything)

The vendored TouchEngine is TD **2023.11780** (July 2024). Everything gets
re-baselined before any verification is meaningful.

1. Fork medcelerate/FFGLTouchEngine → drmbt/FFGLTouchEngine; branch `modernize`.
2. Update vendored TouchEngine library + headers to the current TD 2025/2026
   release (grab from the installed TD's TouchEngine SDK / Derivative release).
   Diff the TE API headers old→new; fix compile breaks.
3. Update vendored FFGL SDK to resolume/ffgl master (2.3). Bump declared API
   version in CFFGLPluginInfo from 2.1 → honest value.
4. Build both plugins on macOS (CMake/Xcode); fix bit-rot. ⊞ CI build for Windows
   via existing GitHub Actions.
5. Smoke test: Example/NoiseOutOnly.tox + InputOutput5Param.tox load in Resolume
   7.27.1, params appear, Reload/Unload/Clear work.
6. Record the new baseline matrix in issues-audit.md (TD build, TE lib version,
   Resolume version, OS).

Exit criteria: both plugins build clean and load a tox in Resolume 7.27.1 with
current TD installed.

## Sprint 1 — Empirical probes (cheap, answer the big unknowns)

Build tiny test toxes + use Resolume MCP to inspect what the host actually shows.

1. **Float range probe** — tox with floats ranged -10..500, 0..2, -1..1. Does
   Resolume's UI show real ranges (SetParamRange working per-instance) or 0..1?
   Answers the prototype-static FF_GET_RANGE question — decides Sprint 3 design
   (native ranges vs under-the-hood remap + GetParameterDisplay).
2. **Default-value probe** — out-of-[0,1] defaults (e.g. default 250 of 0..500):
   does the SetParamInfo clamp gotcha bite? Also XY default retention (#27).
3. **Host FBO format probe** — glGetFramebufferAttachmentParameteriv on HostFBO
   in an 8 bpc vs 16 bpc composition (Resolume 7.24+). Determines the real host
   ceiling for #12.
4. **Frame-rate probe** — absTime.seconds vs wall clock on this machine's
   refresh rate; confirm #17 still reproduces with hardcoded 60fps.
5. **Param order / OSC stability probe** — load same tox 5×; do param order and
   OSC addresses stay stable? (#33 second half.)

Exit criteria: written answers in the knowledge base for all five probes.

## Sprint 2 — Verify the hand-waved closed issues (on the modern stack)

Priority order, each gets: repro attempt → verdict → comment on upstream issue
(reopen case) or note as fixed.

1. **#34** Unload/Clear/Reload duplicating params (claimed 2.0.2, no commit).
2. **#33** Pulse-as-momentary (claimed 2.0.3) AND the never-fixed half:
   momentary registers as pulse, shared OSC address for momentaries.
3. **#12** 32-bit float tox → black output. Test with Example/NoiseOutOnly32Bit.tox
   at 8 bpc and 16 bpc comp depth. README claims 16-bit downsampling exists —
   code says otherwise; settle it.
4. **#32** Audio with modern TD (expect: structurally unsupported — no
   TELinkTypeFloatBuffer handling; reclassify as feature request, not bug).
5. **#13/#27** Header params + int caps + menus (two-dropdown ID collision is in
   the code — write the repro tox).
6. ⊞ **#31** dual-GPU / **#19** crash class — Windows session later.

Exit criteria: verdict table (fixed / still broken / never-was-fixed) added to
issues-audit.md; upstream comments drafted.

## Sprint 3 — Correctness & stability fixes (the foundation #28 needs)

1. **Thread safety**: TE event/link callbacks mutate Parameters/maps + call FFGL
   Set* with zero locking — add a mutex or marshal to render thread. (Prereq for
   dynamic params; likely fixes the #34/#19 crash family.)
2. FF_FALSE(==FF_SUCCESS) returned on error paths → FF_FAIL.
3. Menu param ID collision + unstored menu current-value.
4. Float slot indexing uses all-types count → per-type count (overflow into int
   region past 40 params).
5. Deterministic parameter ordering (sort by TD par order/page) → stable OSC
   addresses (#33b).
6. Unload doesn't clear isTouchEngineLoaded/Ready flags.
7. ⊞ Per-frame Spout interop recreation (broken GLFormat resize check).
8. ⊞ D3D immediate-context double Release; AcquireSync(INFINITE) → timeout.
9. Defaults: apply the quickstart defaultFloatVal workaround if probe 1.2 bit.

Exit criteria: fixes merged on fork; soak test — 50 load/unload/reload cycles
without crash or param duplication.

## Sprint 4 — Dynamic parameters (#28) — the headline feature

1. Handle `TELinkEventValueChange` in linkCallback (currently ignored): read new
   TE value → update stored FFGL param value → RaiseParamEvent(FF_EVENT_FLAG_VALUE).
2. Guard against echo loops (host-set → TE → value-change event → host).
3. Cover all mapped types (float, int, bool, string, menu, color/vector groups).
4. Test with the real use case: TD-side preset recall / interpolation /
   randomize while Resolume MCP reads back parameter values live.
5. Stretch: `TELinkEventAdded` → activate a hidden slot at runtime (visibility +
   display-name events) for params added after load.

Exit criteria: changing a value inside TD updates the Resolume slider in ≤1 frame
tick; demo tox + video.

## Sprint 5 — Float ranges done right (#8/#24/#27 legacy)

Design depends on Sprint 1 probe:
- If per-instance SetParamRange works → keep native ranges; fix defaults; done.
- If prototype-static → implement under-the-hood remap: wire value 0..1, scale to
  TD min/max before TEInstanceLinkSetDoubleValue, and implement
  GetParameterDisplay so Resolume shows the real value as text.
- Either way: respect TD clamp settings, handle inverted/zero-width ranges,
  int ranges beyond ±10000.

## Sprint 6 — Bit depth (#12 fix proper)

1. Windows ⊞: extend GlToDXFormat/GetGlType with 16F/32F entries + sized internal
   formats in InitializeGlTexture (Spout supports 16-bit since Resolume 7.24 era).
2. macOS: parametrize CreateIOSurface bytes/element, Metal pixel formats, CGL
   format/type, FX-input TE format; fix .bgra swizzle for RGBA float surfaces.
3. Minimum viable: never render black — detect float TE output and convert down
   gracefully even if host is 8 bpc.
4. Validate at 16 bpc comp depth per Sprint 1 probe 3 results.

## Sprint 7 — Modernization & release hygiene

1. Host-framerate adaptation: use FFGL hostTime/SetBeatInfo or measured cadence
   instead of hardcoded 60 (kills #17). Expose fps as a plugin param fallback.
2. README truth pass: fix the "16-bit downsampling" overpromise, param limit
   30-vs-40, dual-GPU note (#31), license requirements, version matrix.
3. Versioned releases with real release notes; CI artifacts for both OSes.
4. Stretch backlog: CHOP/FloatBuffer input (audio-reactive toxes, #32-as-feature),
   TELinkTypeStringData (DAT), drag-and-drop tox, multi-instance hardening
   (Spout mutex names ⊞, rand() seeding), 16k texture guard (#21).

---

## Standing test rig (set up once, Sprint 0/1)

- Empty Resolume comp template + empty TD template (user-provided, next step).
- Probe toxes: ranges, defaults, every param type, 32-bit output, pulse/momentary,
  10+ params of one type, two menus.
- Resolume MCP drives Arena (load comp, set params, read state); TDZeroMQ mirrors
  TD-side values; Claude Code orchestrates both and diffs.
