# Session Handoff — 2026-07-23

Paste-able kickoff prompt is at the bottom. Everything below is durable state a
fresh session needs.

## Repo / build state

- Working dir: `/Users/vincentnaples/Documents/github/FFGLTouchEngine`
- Fork: **drmbt/FFGLTouchEngine** (git remote `fork`); `origin` = medcelerate upstream.
- Branches: `master` = upstream v2.0.4 + one README commit (no unreleased code
  upstream). **`modernize-te`** (current) = master + TouchEngine.framework updated
  to Derivative TouchEngine-macOS @ c3ceb1a (2025-06-13). Both build clean:
  CMake + Xcode 26.3, arm64 (`build-baseline/` from master, `build-modern/` from
  modernize-te; `cmake -B <dir> -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64`).
- **Installed in Resolume** (`~/Documents/Resolume Arena/Extra Effects/`): as of
  2026-07-24, the MODERN build (framework c3ceb1a + items 1–2 fixes) is installed
  and confirmed loading after an Arena restart. Backups now live OUTSIDE the
  scanned Extra Effects tree at `~/Documents/Resolume Arena/_plugin-backups/`
  (`_backup-baseline-build-2026-07-24/` = the baseline build that was installed;
  `_backup-v2.0.4-release/` = pristine release) — they were moved out because
  Resolume recursively scans Extra Effects subfolders and was loading duplicate
  FFGL IDs. See [test-results-2026-07-24.md](test-results-2026-07-24.md).
- **Resolume MCP** installed user-scope (all sessions) from
  `/Applications/Resolume Arena/mcp/resolume_arena_mcp_server.mcpb` → extracted to
  `~/.claude/mcp-servers/resolume-arena/`. Loads from the NEXT session on. In-session
  driving is still the REST API at `http://localhost:8080/api/v1`.
- Knowledge base: `docs/knowledge/` — README (index), issues-audit,
  codebase-notes, ffgl-sdk-notes, resolume-bitdepth, sprint-plan,
  test-results-2026-07-23, this file. Skill: `.claude/skills/ffgl-tox-effect`.
  None of docs/tests/.claude/builds are committed to git yet (untracked).

## Test rig

- `tests/engine-2023.11170/` and `tests/engine-2025.33070/` — each contains the
  Example toxes + `testNewPars.tox` + a `TouchEngine` symlink to the matching
  `/Applications/TouchDesigner.*.app`. **The symlink next to the loaded .tox
  pins which TD hosts the engine** (verified; both engines can run concurrently).
- TD installed: 11170 (2023), 32280/32820/33070 (2025). Resolume Arena 7.27.1 +
  MCP server. Comp `test_MCP_TouchEngine` (UNSAVED test state: TouchEngine
  source clips on layer 4 cols 1–7, Gradient+TouchEngineFX on layer 3 col 1).
- Permissions granted to the host app: Screen Recording AND Accessibility
  (screencapture + osascript/System Events both work; a CGEvent scroll utility
  attempt timed out compiling — retry `swiftc` with longer timeout if UI
  scrolling is needed).
- Arena log: `~/Library/Logs/Resolume Arena/Resolume Arena log.txt` (plugin
  lines prefixed `FFGL:`).
- No TouchDesigner MCP tools were available in the previous session — if tox
  editing is needed, confirm the TD MCP server is connected, else ask Vincent.

## Verified findings (full detail in test-results-2026-07-23.md)

1. **Float/int ranges**: values flow UNSCALED both ways (1920/1080/145 observed),
   but the host slider/range is the pre-allocated prototype placeholder (floats
   0–1, ints ±10000) — FF_GET_RANGE reads the static prototype (ffgl-sdk-notes).
   Fader interaction clamps floats to 1.0. UI shows TD display names; OSC/REST
   shows slot names.
2. **All pulse slots are named "Pulse"** (TouchEnginePluginBase.cpp:441), colors
   "Color" → OSC address collision (#33 second half) is structural.
3. **macOS FX flicker**: TouchEngineFX.cpp:164-174 busy path draws nothing and
   returns FF_FAIL outside `#ifdef _WIN32` → transparent frames whenever TE is
   mid-cook. Confirmed live by Vincent.
4. **Enumeration fragility**: testNewPars.tox (TD-2025-authored, new par types)
   on 33070 engine loads slowly (~2.5 min) and drops whole type regions
   (int/bool/string missing, color truncated); on the 2023.11170 engine it
   silently never loads (engine older than authoring build — no error surfaced).
5. **#34 (unload/reload dup)**: NOT reproducible on macOS 2.0.4 build. #12:
   32-bit tox renders CORRUPTED speckle on macOS (not black), engine-independent.
   #17: hardcoded 60fps still in code. Reload emits "Failed to set double value"
   spam (stale float-map push).
6. Working: generator + FX pipelines on both engines, toggle, string, single
   menu, pulse-via-API (no latch), Unload/Reload/Clear, display names.

## Priority work queue (updated 2026-07-24 night)

DONE & LIVE-VERIFIED: items 1-5 plus the full #28 arc — flicker fix,
enumeration robustness, unique slot names (Menu2 fixed), float range remap +
GetParameterDisplay, TE lifecycle mutex, Reload crash root-cause fix
(InstanceReady semantics), dirty-only push, par-state echo channel
(Out CHOP/DAT), echo settling window, newest-engine preference, queued
superseding loads. CHANGELOG.md is the authoritative delta list.

DONE (2026-07-24, branch-split session): the three stacked branches exist
locally and each builds Release/arm64 — `fix/stability` (framework update,
flicker, enumeration, InstanceReady, mutex, R/B swap, empty-path guard, reload
queue), `feat/slot-naming-ranges` (unique names, per-family IDs, range remap —
BREAKING), `feat/dynamic-params` (dirty push, echo channel, settling window,
engine preference). Acceptance held: `git diff feat/dynamic-params modernize-te
-- src/` is empty; the only remaining delta is docs, which stay on
`modernize-te`. The Menu2 collision fix had to move up into
`feat/slot-naming-ranges` (its fix IS the per-family counters) — noted in
FORK-GUIDE.md and to be called out in PR #1's description. `FORK-GUIDE.md` at
the repo root is the decoder (branch map, change→branch table, migration notes,
echo-authoring convention, verification matrix). **Nothing is pushed and no PRs
are open — the Windows pass gates that.**

Remaining queue:
1. **Windows pass** (Vincent): R/B swap presence, all fixes compile/behave;
   the macOS-only engine-preference scan needs a Windows equivalent
   (Program Files/Derivative scan).
2. Color-picker UI spike (RGBA renders as 4 faders; Arena may never group
   FFGL R/G/B/A into its native picker).
3. Int range remap (ints unscaled within +/-10000 prototype).
4. #17 hardcoded 60 fps; #12 32-bit corruption (macOS speckle).
5. Tox-side: lag/filter the parexec writes for smooth interpolated recalls
   (Vincent's next probe); echo skill — extend .claude/skills/ffgl-tox-effect
   into a "prepare a component as an Engine effect" checklist (Par DAT->Out
   DAT echo, naming rules, engine pinning vs newest-preference).
6. Known limitations (accepted/documented): unranged floats cannot
   exceed their load-time value from the host; TD-side changes within the
   30-frame settling window of a host push to the SAME par are dropped.
