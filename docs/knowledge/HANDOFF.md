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

## Priority work queue (agreed direction)

1. ✅ **DONE & LIVE-VERIFIED (2026-07-24).**
   Busy-frame persistence on macOS (flicker fix): redraw cached last frame on
   busy/not-ready, return FF_SUCCESS. Also fixed the missing ScopedShaderBinding
   on the input base-pass draw. See test-results-2026-07-24.md.
2. ✅ **DONE & LIVE-VERIFIED (2026-07-24).**
   Enumeration robustness: skip+log+continue instead of aborting the walk;
   read-values-before-register (no half-registered params); `default:` case for
   unknown TELinkTypes; TE load errors surfaced via FFGLLog + TEResultGetDescription.
   FOUND & DEFERRED: Menu2 collision (see 07-24 doc) — belongs with item 3/4.
2.5. **PROMOTED: mutex around Parameters/ParameterMap* + TE lifecycle** (was
   item-5 prereq). Live-verified 2026-07-24: Reload on a playing FX clip
   segfaults Arena (render-thread texture push into a freed TE link), and
   failed loads leave params spamming "Failed to set double value" per frame.
3. **Unique slot names** ("Pulse1..40", "Color1R/G/B/A"...) → distinct OSC
   addresses. (Breaking change for saved comps — do it on the fork, note in README.)
4. **Float ranges**: wire stays 0–1; remap to TD min/max in SetFloatParameter/
   PushParameters (and inverse in GetFloatParameter); implement
   GetParameterDisplay to show real values. Int range widening similarly.
5. **#28 dynamic values**: handle TELinkEventValueChange in linkCallback →
   update stored value → RaiseParamEvent(FF_EVENT_FLAG_VALUE) (Resolume 7.4.0+),
   with echo-loop guard. Prereq: add a mutex around Parameters/ParameterMap*
   (TE callbacks race the render thread — likely also fixes reload spam).
6. A/B the modern-framework build (swap build-modern bundles into Extra Effects,
   restart Arena, rerun the col 1–7 matrix; watch the testNewPars enumeration).
7. Still unverified (needs probe tox ground truth from Vincent: par list of
   testNewPars.tox, or scroll Arena's clip panel and screenshot): header,
   momentary vs pulse semantics, XY defaults (#27), two-menu collision.

## Kickoff prompt for a fresh session

```
Continue the FFGLTouchEngine fork work on branch modernize-te. First read
docs/knowledge/HANDOFF.md and docs/knowledge/test-results-2026-07-24.md.

State: items 1–2 (macOS flicker fix + enumeration robustness) are implemented,
built, and installed; the modern bundle is live in Resolume Arena (already
restarted, new binary confirmed loading). The Resolume MCP is now installed and
should be loaded in THIS session — confirm its tools are available (else the REST
API is at http://localhost:8080/api/v1). The prior live rig was unsaved and is
gone; the Compositions/test_MCP_TouchEngine.avc reopens empty.

Do this:
1. Using the Resolume MCP, build a minimal verification rig in the current comp:
   - a TouchEngine SOURCE clip loading tests/engine-2025.33070/testNewPars.tox
     (the TouchEngine symlink in that folder pins the 2025.33070 engine),
   - a second clip with a heavy/slow tox for the flicker check (or reuse an
     Example tox and drive it hard).
2. Verify item 2 (enumeration): testNewPars ground truth = 11 params, NO
   int/bool/string types; correct enumeration should expose ~18 FFGL slots
   (10 standard + 4 color + 2 event + 2 option). KNOWN-REMAINING bug: the second
   menu (Menu2) collides and drops — do NOT treat that as a regression. Tail
   ~/Library/Logs/Resolume Arena/Resolume Arena log.txt for new
   "FFGL: skipping parameter …" lines, and confirm reload no longer spams
   "FFGL: Failed to set double value".
3. Verify item 1 (flicker): on the heavy tox, confirm no transparent-frame
   flicker while TE is mid-cook (should hold the last frame). This is visual —
   ask me to watch.
4. A/B vs the baseline build in ~/Documents/Resolume Arena/_plugin-backups/
   _backup-baseline-build-2026-07-24/ (swap into Extra Effects + restart Arena;
   backups are OUTSIDE the scanned tree on purpose — keep them there).
5. If verified, the changes are still uncommitted on modernize-te — offer to
   commit. Next queue items: 3 (unique slot/OSC names — also fixes Menu2),
   4 (float/int UI ranges), 5 (#28 dynamic values + mutex).
```
