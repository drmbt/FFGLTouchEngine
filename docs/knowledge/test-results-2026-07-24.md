# Session Results — 2026-07-24

Continues [test-results-2026-07-23.md](test-results-2026-07-23.md) and
[HANDOFF.md](HANDOFF.md). Branch: `modernize-te`.

## Code changes implemented (priority-queue items 1–2)

### Item 1 — macOS busy-frame persistence (FX flicker fix)
`src/plugins/FFGLTouchEngineFX/TouchEngineFX.cpp`, `ProcessOpenGL`:
- **Busy/not-ready path** (was: draw nothing + `FF_FAIL` outside `#ifdef _WIN32`
  → transparent frame every busy frame). Added an `#ifdef __APPLE__` block that
  redraws the cached IOSurface-backed last frame (`OutputTextureGL` via
  `rectShader`, same as the normal-path draw) and returns `FF_SUCCESS` when a
  cached frame exists. Falls through to `FF_FAIL` only when there is no cached
  frame yet (first load). Windows path left unchanged (can't build/test here).
- **Input base-pass draw** (~old line 189–192): the `shader.Set(...)/quad.Draw()`
  ran with **no program and no texture bound** (no-op/garbage on both platforms).
  Wrapped it in `ScopedShaderBinding` + `ScopedSamplerActivation` +
  `Scoped2DTextureBinding(pGL->inputTextures[0]->Handle)` so it actually draws the
  input; TE output is drawn over it, so it only shows until TE's first frame.

### Item 2 — enumeration robustness
`src/plugins/shared/TouchEnginePluginBase.cpp`:
- Added file-scope `LogLinkSkip(identifier, reason)` helper.
- `GetAllParameters`: the per-group `TEInstanceLinkGetChildren` failures (input
  **and** output scope) were `return` (aborted the **whole** walk → dropped every
  parameter after a bad group). Changed to **log + `continue`** (skip that group
  only). Also log the top-level `TEInstanceGetLinkGroups` failure and each
  `TEInstanceLinkGetInfo` skip.
- `CreateIndividualParameter`: **rewrote so every branch reads all TE values
  first, then registers** (`Parameters`/`ActiveParams`/`ParameterMap*`). A failed
  read now logs + returns *before* any partial registration, so no half-built
  param is left for `PushParametersToTouchEngine` to trip on (this was a source of
  the "Failed to set double value" reload spam). Added a **`default:` case** that
  logs and skips unknown/future `TELinkType`s (graceful handling of par types from
  newer TD builds) instead of silently doing nothing.
- `CreateParametersFromGroup`: fixed dead code (`continue;` before a log line);
  now logs group-children and per-child failures and keeps walking. Renamed the
  shadowing inner `linkInfo` to `childLinkInfo`.
- **Load-error surfacing**: `LoadTEFile` now logs `TEResultGetDescription(result)`
  on `TEInstanceConfigure` / `SetFrameRate` / `Load` failure (the "engine older
  than the tox's authoring build" case). `eventCallback` logs a description on a
  non-success `TEEventInstanceDidLoad`.

**Deliberately NOT changed (out of item-1/2 scope):** the menu ParamID scheme —
see remaining bug below.

## Build / install / environment state
- Rebuilt `build-modern` (CMake+Xcode, arm64, Release) — clean, 10 pre-existing
  `-Wswitch` warnings, no errors. Both bundles embed `TouchEngine.framework`
  (self-contained). Modern framework md5 `c47332b5…` (vs baseline `a310fbd8…`).
- **Installed** modern bundles into `~/Documents/Resolume Arena/Extra Effects/`.
- **Backups relocated OUT of the Extra Effects scan tree** to
  `~/Documents/Resolume Arena/_plugin-backups/` (Resolume recursively scans
  Extra Effects subfolders, so `_backup-*` folders had been loading duplicate FFGL
  IDs "TE"/"TEFX"). Now contains `_backup-baseline-build-2026-07-24/` (the
  locally-built baseline bundles that were installed) and `_backup-v2.0.4-release/`
  (pristine release). Post-restart log confirms a single load of each plugin.
- **Resolume MCP installed for all sessions**: the official server ships at
  `/Applications/Resolume Arena/mcp/resolume_arena_mcp_server.mcpb`. Extracted to
  `~/.claude/mcp-servers/resolume-arena/` and registered user-scope via
  `claude mcp add --scope user resolume_arena` (status: Connected). It wraps the
  same REST API. NOTE: a server added mid-session does not load into that session
  — the tools are available starting from the **next** Claude Code session.
- The "Resolume MCP" referenced in prior handoffs = the **REST API on
  `http://localhost:8080/api/v1`** (still the driving surface for the current
  session). Product: Arena 7.27.1 rev 15990.

## testNewPars.tox — ground truth (from live TD `/project1/testNewPars`)
11 logical custom params on one page, **no int/bool/string params at all**:

| Param(s) | TD style | Expected FFGL mapping |
|---|---|---|
| Header | Header | separator → skipped |
| Float, Floatunranged | Float | 2 standard-float slots |
| Xyzw (4), Xy (2) | XYZW | vector → 6 standard slots |
| Resolutionwh (2) | WH | vector → 2 standard slots |
| Rgba (4) | RGBA | 4 color slots (R/G/B/A) |
| Momentary, Pulse | Momentary/Pulse | 2 event slots |
| Menu, Menu2 | Menu | 2 option slots |

**Reframes the 2026-07-23 "int/bool/string regions ENTIRELY MISSING" finding:**
those types are not in this tox. The "11 floats + 1 Color + 1 menu + 1 pulse" the
prior session saw ≈ 10 standard slots (Float+Floatunranged+Xyzw+Xy+WH) + color +
**one** menu. Correct enumeration should yield **18 FFGL slots** (10 std + 4 color
+ 2 event + 2 option).

## Remaining bug found (deferred — belongs with queue items 3–4)
**Second menu (Menu2) collides / is dropped.** In `CreateIndividualParameter`
the `TELinkTypeInt`-with-choices branch computes
`ParamID = ParameterMapInt.size() + Offset + MaxParamsByType*5` but **never inserts
into `ParameterMapInt`**, so two menus both resolve to the same ParamID and the
second overwrites the first. Fixing it (store the menu value in `ParameterMapInt`)
also shifts int-scalar IDs (they share `ParameterMapInt.size()`), so it needs the
slot-ID/OSC-address rework, not a spot patch. This is the concrete "menu ID
collision, base cpp:705" from the audit.

## Verification status
- New binary **confirmed loading** after the Arena restart (single clean load,
  correct modern framework md5, no duplicate IDs).
- **Behavioral verification of items 1–2 is still pending.** The previous live rig
  (`test_MCP_TouchEngine`: L4 TouchEngine sources + L3 Gradient/FX) was in-memory
  only and never written to `Compositions/test_MCP_TouchEngine.avc`, so the restart
  reopened an empty grid. Resolume's public REST API has no clean "add source by
  idstring" call, so rebuilding the rig is a job for the Resolume MCP (next
  session) or a one-off manual source-assign in Arena (then verify via REST +
  `FFGL:` log lines).

## Live verification (2026-07-24, second session, via Resolume MCP)

Rig: `test_MCP_TouchEngine.avc` — L1C1 TouchEngine source +
`tests/engine-2025.33070/NoiseOutOnly5Param.tox`; L2C1 Gradient +
TouchEngineFX + `InputOutput5Param.tox`; L2C3 Gradient + TouchEngineFX +
`Example/probe.tox` (= testNewPars re-saved as an **FX** tox — testNewPars is
an effect, not a source; loading it in the source plugin fails with
"An action was cancelled").

- **Item 1 (flicker) VERIFIED.** With a 1-beat 0→1 sweep animating Parameter4
  (TE cooking continuously), Vincent confirmed no transparent-frame flicker;
  output holds the last frame.
- **Item 2 (enumeration) VERIFIED.** probe.tox exposes 17/18 expected slots
  (10 floats = Parameter4–11/18/19, menu Parameter204, 4 color slots + 2 event
  slots that REST collapses by name to one "Color"/"Pulse"); only the known
  Menu2 collision is missing. NoiseOutOnly5Param enumerates all 5 types
  (float/int/bool/string/pulse). No "skipping parameter" lines, and **zero
  "Failed to set double value" during normal load/play** across 4 tox loads.
- **Menu2 collision confirmed live**: only one menu slot shows and turning it
  changes BOTH Menu and Menu2 in TD (both TE identifiers bound to one ParamID).
- **Momentary/Pulse**: both function correctly, but both buttons are labeled
  "Pulse" (every event slot registers under the hardcoded name) — item 3.
- **Float clamp confirmed**: defaults > 1.0 load clamped at 1 (Floatunranged,
  Resolutionwh 1920/1080 → 1) — item 4.
- **NEW CRITICAL: Reload segfault.** Pulsing Reload on a live FX clip crashed
  Arena instantly: Render Thread, `ProcessOpenGL → TEInstanceLinkSetTextureValue
  → TPC::LinkValue::setValue → LinkBase::getType()` on a dangling link
  (KERN_INVALID_ADDRESS 0xc). The per-frame texture push raced the reload's
  link invalidation. This is the item-5 mutex prerequisite — promote it.
  Crash report: `~/Library/Logs/DiagnosticReports/Arena-2026-07-24-012737.ips`.
- **"Failed to set double value" spam persists on FAILED loads only**: ~8.8k
  lines at ~55/sec while instances whose TEInstanceLoad was cancelled kept
  receiving per-frame pushes. Normal operation is clean; the failed-load path
  still leaves registered params pushing into a dead instance.
- **Cosmetic**: both plugins log `TEInstanceLoad failed for ''` at
  instantiation (empty-path load attempt) — needs an `if (FilePath.empty())`
  guard before the load/log.

## Next-session kickoff
```
Resolume MCP is now installed (user scope) and loads this session. On modernize-te
the macOS flicker fix + enumeration robustness fix are built and installed; the new
binary is live in Arena. Rebuild a minimal rig via the Resolume MCP: a TouchEngine
source clip loading tests/engine-2025.33070/testNewPars.tox (33070 engine), plus a
heavy/slow tox for the flicker check. Verify: (a) enumeration yields ~18 slots incl.
both menus? — note Menu2 collision is a KNOWN remaining bug; (b) tail the Arena log
for new "FFGL: skipping parameter …" lines and confirm no "Failed to set double
value" spam on reload; (c) watch a heavy tox for transparent-frame flicker (should
now hold the last frame). Then A/B vs the baseline bundles in
~/Documents/Resolume Arena/_plugin-backups/. Ground truth + details:
docs/knowledge/test-results-2026-07-24.md.
```

## #28 echo-channel session (2026-07-24 evening)

Hard-won facts, verified against instrumented builds + probe toxes:

1. **TE input links are host-authoritative** (the #28 wall): comp-internal
   writes to root custom pars fire no `ValueChange` AND are invisible to
   `TEInstanceLinkGet*Value` (a 2s poll returned the host value while the par
   sat at a different value TD-side). Reflection must ride OUTPUT links.
2. **Echo convention implemented**: Par CHOP→Out CHOP (FloatBuffer) and/or
   Par DAT→Out DAT (StringData). Channel/row names are TD par-component
   script names (`Rgbar`, `Floatunranged`, `Resolutionwhw`); the plugin maps
   them via `EchoNameToParamID` built at enumeration (vector children =
   par name + lowercased RGBA/XYZW/WH suffix). Menus: DAT rows carry the
   TOKEN (mapped via `TEInstanceLinkGetChoiceValues`), CHOP channels carry
   the INDEX. Verified live: preset-recall writes appear in Resolume.
3. **Engine build gates output-link types**: the unpinned `Example/` folder
   resolved an engine that exposes only the texture output (og/out → op/out1
   alone, no Added events ever). Pinning 2025.33070 via the folder symlink
   exposed all three (out1 texture, out2 FloatBuffer, out3 StringData).
   ALWAYS pin the engine for echo toxes.
4. **Menu self-echo race**: when the echoed par set includes a par the host
   just pushed (the preset menu itself), a stale cook's echo can arrive after
   the push cleared the dirty flag and revert the host's set. Vincent's
   tox-side fix: Select out the menu from the echo. Planned in-code fix:
   per-param grace window after a push during which non-matching echo values
   are ignored.
5. Lag-smoothing the ECHO (probe9) is the wrong place — it fights the grace
   semantics and only smooths the report, not the par. Correct approach:
   lag/filter the writes into `parent().par.*` inside the tox (planned).
6. TD "Multiple New Plugins Detected" spam: approvals persist to a json in
   the Custom OP Plugins dir — a root-owned dir silently blocks the write
   (fixed with chown).
7. Skill TODO: extend `.claude/skills/ffgl-tox-effect` with the echo-channel
   authoring convention (Par CHOP/DAT → Out, engine symlink pinning, menu
   exclusion or grace-window reliance) — "prepare a component as an Engine
   effect" checklist.
