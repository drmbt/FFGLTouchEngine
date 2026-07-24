# FFGLTouchEngine — Issues Audit (as of 2026-07-23)

Audit of all 28 issues + 6 PRs on medcelerate/FFGLTouchEngine, with closure-quality
assessment. Focus: which closed issues were actually fixed vs. hand-waved.

## Open issues

### #27 — Broken Parameters (OrdinaireX, opened 2024-11-27)
- Integer, Header, and XY parameters broken; random floats not working.
- Header-param fix shipped in v1.1.1 and was **confirmed by reporter**.
- Int cap of 1000 raised "in a later release" (no commit ref).
- **Still outstanding:** XY parameters don't retain default values (load as 0 instead
  of 1); newest report (video-device-input tox params don't appear) is unanswered.
- drmbt commented here: pulse→toggle, momentary→pulse, header breaks everything,
  and float ranging as "the last major super important thing." Maintainer replied
  that 0–1 UI range is an FFGL SDK limit but under-the-hood remapping is possible.
  (Note: current code actually uses `SetParamRange` with real min/max — see
  codebase-notes.md — so this claim deserves retesting.)

### #28 — RFE: Dynamic value update (drmbt, opened 2025-01-05) ← THE BIG ONE
- Request: push TD-side parameter changes back to Resolume (preset recall,
  interpolation, randomize) using the FFGL SDK's value-change Events
  (Resolume 7.4.0+, see resolume/ffgl Events example).
- Maintainer's only reply: "It may be possible, but most likely very tricky due to
  how parameters are stored currently in relation to TD. I would recommend making
  a branch and see if you can implement it."
- Dormant since January 2025. No branch or PR exists.
- Implementation angle: `linkCallback` in TouchEnginePluginBase.cpp currently
  ignores `TELinkEventValueChange` — that event + `RaiseParamEvent` /
  FFGL param events is the natural hook.

## Closed issues — verified fixed (no action needed)

| # | Title | Evidence |
|---|-------|----------|
| #6 | Parameter name in Resolume | commit 2bfbc50, confirmed via #8 thread |
| #22 | Only first two params work in Arena | commit b607696, reporter confirmed "Verified resolved" |
| #8 | Min/max/default (regression half) | beta3 regression fixed, confirmed by two users. The min/max feature request half was closed as "FFGL SDK limitation" — standing product gap, see #24/#27 |

User error / self-resolved / legitimately out of scope: #5, #10, #14, #20, #25 (OSC out of scope), #26 (non-commercial TD license unsupported).

## Closed issues — NEED RE-VERIFICATION

Priority order:

1. **#34 — Unload/Clear Instance/Reload duplicates params (plugin 1.15)** — closed
   with bare claim "resolved in 2.0.2". No commit, no PR, reporter never confirmed.
   This is a regression of PR #15 functionality — it has re-broken before.
2. **#33 — Pulse executes like momentary (drmbt)** — maintainer claims v2.0.3 fix
   ("send 1:0 immediately"). No commit ref, no reporter confirmation. Second half
   of thread explicitly deferred and NEVER resolved: momentary registers as pulse,
   all momentaries share one OSC address, nondeterministic parameter ordering
   affects OSC addresses.
3. **#12 — 32-bit float RGBA textures render black** — acknowledged real limitation
   ("interop... limited to 8bit on the input"), closed same day with a "future"
   promise. No commit ever addressed bit depth. Maintainer assumed "resolume runs
   only in 8 bit maybe" — questionable (see resolume-bitdepth notes). Test asset
   exists: `Example/NoiseOutOnly32Bit.tox`. See codebase-notes.md for the exact
   code sites that force 8-bit.
4. **#17 — absTime.seconds runs ~2x fast on 144Hz displays** — reporter diagnosed
   root cause (TE initialized at hardcoded 60fps; still true in current code:
   `TEInstanceSetFrameRate(instance, 60, 1)`). Closed with NO code fix. Almost
   certainly still reproducible on any non-60Hz setup.
5. **#32 — Audio broken with TD Experimental 2025.30960** — closed 8.5 months later
   on speculation ("with updated headers it should work"). Zero evidence. Note:
   codebase has NO CHOP/FloatBuffer handling at all (TELinkTypeFloatBuffer falls
   through silently), so "audio support" is structurally absent.
6. **#13 — Header parameter hides subsequent params** — probably genuinely fixed via
   the #27/v1.1.1 work, but this thread itself has no confirmation. Quick retest.
7. **#31 — Black output on dual-GPU laptops** — workaround only (pin both apps to
   one GPU in NVIDIA Control Panel). "Failed to create interop" on mixed-GPU
   systems is an unfixed, undocumented limitation. Deserves a README note.
8. **#19 — Instant crash on Win11** — stale-closed, blamed on Resolume DLL loading.
9. **#21 — ~16k-wide textures return null from TE** — deflected upstream, never
   followed up. Affects large-stage use.
10. #2, #7, #16, #18, #23(perf half) — deflections / bare claims, lower priority.

## Version matrix from issue threads

| Issue | TouchDesigner | Resolume | Plugin |
|---|---|---|---|
| #8 | 2023.11760 | Avenue 7.19 | 0.2-beta1/3 |
| #17 | 2023.17700 | Win11 vanilla | — |
| #19 | latest TD advised | 7.20.1 / 7.21.0 | 0.2-beta9 |
| #22 | 2023.11880 | Arena 7.13.2 | — |
| #23 | 2023.11880 | Arena 7.21.3 | 1.0-beta |
| #24 | latest | 7.16.0 | — |
| #32 | Experimental 2025.30960 (broken) | 7.23.2 | 1.4 |
| #33/#34 | — | 7.23.2 | 1.15 |

**Effective tested baseline: TouchDesigner 2023.11880-era + Resolume 7.23.2 (late-2025
reports, plugin 1.15/2.x). TD 2025 experimental known-broken for audio. TD
commercial/pro/educational license required (#5, #26, #31).**

**Hard version pins (from binaries/git, 2026-07-23):**
- Vendored TouchEngine library = **TD 2023.11780** (dll PE ProductVersion; committed
  2024-07-05, never updated). The engine process itself comes from the user's
  installed TD, so lib/headers (2023-era) vs newer TD installs is a mismatch risk.
- Latest release **v2.0.4 (2026-04-03)**; v2.0.2–v2.0.4 release notes are EMPTY
  (changelog links only) — the claimed #33/#34 fixes have no documented commits.
- Vendored FFGL SDK = 2.3-era master (upstream dormant since June 2023). Plugins
  declare API 2.1 but use 2.3 features → effectively requires Resolume 7.4.1+.
  No release states a tested Resolume version; issue-template placeholder 7.16.1;
  community reports cluster on 7.23.2. Never validated on 7.24+ (16 bpc era).

## PRs

| # | Title | Author | State |
|---|---|---|---|
| 1 | Added thumbnail | yannicksengstock | merged 2024-06-17 |
| 3 | Change TOPs terminology | yannicksengstock | closed unmerged |
| 9 | Texture interop now works | medcelerate | merged 2024-07-07 |
| 15 | Unloading | medcelerate | merged 2024-07-09 (later regressed, #34) |
| 29 | CMake conversion + rearrange | t3kt | merged 2025-01-08 |
| 30 | Consolidate shared plugin code | t3kt | merged 2025-01-11 |

Only substantial external contributor: t3kt (build system + shared-base refactor).
The claimed 2.0.2/2.0.3 fixes for #32/#33/#34 have **no corresponding PRs** — they
went in as direct commits; verify against release tags, not PRs.

## Repo activity since July 2025

- Aug 2025: issue templates, README update, FUNDING.yml (housekeeping only)
- Apr 2026: macOS support (Metal + IOSurface), null-pointer callback crash fix,
  Windows build fix, pulse param fixes, color picker, color param sliders
- Jul 2026: README marks macOS support complete
