# Live Test Results — 2026-07-23

Rig: Arena 7.27.1 (rev 15990), macOS arm64, baseline bundle (v2.0.4 source,
2023.11780 framework, locally built), comp `test_MCP_TouchEngine`, driven via
Resolume MCP. Engines: TD 2023.11170 and TD 2025.33070, pinned per-tox via
`TouchEngine` symlink folders (`tests/engine-*/`).

## Infrastructure findings

- **Per-tox engine pinning WORKS**: lsof confirms cols 1–2 hosted by
  `TouchDesigner.11170.app/.../TouchEngine`, col 3+ by `TouchDesigner.33070.app`.
  Both engines ran CONCURRENTLY in one Arena session without conflict.
- **2023.11780-framework bundle drives the TD 2025.33070 engine fine** for
  video-only toxes (params enumerate, renders correctly). #32-era audio not
  exercised (no audio support in plugin at all).
- Generator instance loads params in seconds; FX instance took noticeably longer
  (~1–2 min before params appeared via API). Worth profiling.
- Plugin log lines land in `~/Library/Logs/Resolume Arena/Resolume Arena log.txt`
  (prefix `FFGL:`).

## Verdict table

| Claim / feature | Verdict | Evidence |
|---|---|---|
| Engine symlink selection | ✅ works | lsof per-process TD paths |
| Generator pipeline (both engines) | ✅ works | params + noise render, string overlay |
| FX pipeline (input→TE→out) | ✅ works | Gradient clip + TouchEngineFX renders tox output |
| String param flow | ✅ works | set "MCP LIVE" → rendered in output |
| Toggle (bool) | ✅ works | mono flip visually confirmed |
| Menu (single) | ✅ works | 3 labels intact, choice switch OK |
| Pulse via API | ✅ fires, no latch | event fired repeatedly, reads back as bare event |
| Pulse TD-side one-shot (#33 half 1) | ❓ needs probe tox | no visible one-shot artifact in example tox |
| Momentary / Header / XY (#33/#27/#13) | ❓ not testable | example toxes contain none of these types |
| Reload | ✅ no duplication | same 5 params, same IDs; values reset to defaults; NOTE: 2× "FFGL: Failed to set double value" in log during reload (stale map push?) |
| Unload → different tox (#34) | ✅ NOT reproducible | clean 1-param list after 5-param tox, no ghosts (macOS, 2.0.4 source) |
| Float/int UI ranges (#8/#24/#27) | ❌ BROKEN as suspected | float shows 0–1, int shows −10000..10000 — EXACTLY the pre-allocated placeholder ranges, not the tox ranges. Per-instance SetParamRange after load does NOT reach Resolume → prototype-static FF_GET_RANGE confirmed empirically |
| 32-bit tox (#12) | ❌ BROKEN (new symptom) | NOT black on macOS: renders corrupted blue-grey speckle (float texture wrapped as BGRA8) on BOTH engines — plugin interop bug, engine-independent |
| Param OSC/API identity | ❌ BROKEN by design | REST identity = slot names ("Parameter4", "Parameter44"…), ALL pulse slots named "Pulse" (base cpp:441), all color slots "Color" (cpp:454-457) → #33 shared-OSC-address confirmed as structural |

## Answers to the headline questions

1. **Floats not ranged 0–1:** the plugin does NOT rescale values (real values cross
   the wire — int 239 arrived intact), but Resolume's UI/API range stays at the
   slot placeholder (0–1 for floats, ±10000 for ints) because FF_GET_RANGE reads
   the static prototype. A TD float ranged 0..500 is therefore UI-clamped to 0–1
   in Resolume. Fix options: per-instance range dispatch patch (host may not
   re-query), or wire-0..1 + internal remap + GetParameterDisplay.
2. **Pulse "fixed in 2.0.3":** plausible — auto-reset code is real, API firing
   works, nothing latches. Full confirmation needs a probe tox with a visible
   one-shot effect. The OSC-address half of #33 is definitively NOT fixed
   (all event slots share the name "Pulse").
3. **#34 unload/reload:** could not reproduce on this build (macOS). Original
   report was Windows plugin 1.15 — retest on Windows before closing the book.
4. **#12:** macOS manifests as corruption (not black). Reopen-worthy with new
   evidence; fix is the format plumbing catalogued in codebase-notes.md.

## Needed: ProbeParams.tox (blocks remaining verifications)

Build in TD with out1 TOP rendering param state visibly (text/color feedback):
- Float `Floatrange` min -10 max 500 default 250 (range + default-clamp probe)
- Float `Floatnorm` 0..1 (control)
- XY par `Xypar` defaults (1.0, 1.0) (#27 XY default bug)
- Header `Headertest` between params (header breakage claim)
- Toggle, Pulse (visible one-shot counter), Momentary (distinct visible effect)
- Two Menu pars with different option sets (menu ID collision bug, cpp:705)
- RGBA color par (color picker mapping)
- 2nd string par
Place in both tests/engine-*/ folders.

## Round 2 — testNewPars.tox probe (late session)

- **UI display names WORK**: Arena UI shows TD labels ("Float", "Float Unranged")
  while REST/OSC identity remains slot names (Parameter4/5...). FFGL 2.3
  display-name channel functions; serialization/OSC identity does not follow it.
- **Float range mechanics fully characterized** (user-confirmed + REST):
  - Values OUTSIDE 0–1 pass through and display: Parameter18=1920, Parameter19=1080,
    Parameter5=145 — all shown against an advertised (0-1) range.
  - Defaults >1 display correctly after load.
  - BUT touching the fader clamps to 1.0 max — the host slider uses the prototype
    range (0–1). Values flow real; UI interaction is caged. Matches the
    prototype-static FF_GET_RANGE mechanism exactly.
- **testNewPars.tox (authored in TD 2025, new par types):**
  - 2023.11170 engine: NEVER loads (no params, black) — engine older than
    authoring build; silent failure, no user-visible error (bad UX — should
    surface TE load errors).
  - 2025.33070 engine: loads but takes ~2.5 min (vs seconds for simple toxes),
    and enumeration is PARTIAL: 11 floats (with index gaps 12–17), 1 menu,
    1 pulse, a single "Color" row; int/bool/string regions ENTIRELY MISSING
    (they worked in NoiseOutOnly5Param). New 2025 par types appear to abort
    per-group enumeration mid-walk (error paths `return` mid-loop).
  - Reload triggered "FFGL: Failed to set double value" ×12 — stale float-map
    entries pushed against the new instance state.
- **FX flicker to transparent (user report, root cause found):**
  TouchEngineFX.cpp:164-174 — the busy/not-ready early-exit redraws the last
  frame only under `#ifdef _WIN32`; on macOS it draws nothing and returns
  FF_FAIL → transparent frame whenever TE is mid-cook. Heavy toxes flicker
  constantly. Fix: persist last output texture + redraw on busy, return SUCCESS.
- Comp color depth verified 8bpc (Arena title bar).

## Misc observations

- Param values reset to tox defaults on Reload (not preserved).
- "Failed to set double value" ×2 in Arena log at reload time — pushed stale
  float map entries against the new instance; harmless-looking but confirms
  push-loop iterates stale state during transitions.
- Arena UI label verification (display names) blocked: screencapture returns
  black without Screen Recording permission for the terminal. Grant permission
  or check visually: params should show TD labels in UI while OSC/REST shows
  Parameter4/Pulse/etc.
