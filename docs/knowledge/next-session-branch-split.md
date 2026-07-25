# Next session: branch split for upstream PRs + FORK-GUIDE

Goal: restructure the fork so the upstream maintainer (medcelerate) can
cherry-pick cleanly, while Vincent keeps one working branch with everything.
Current state: `modernize-te` = master + 12 commits that interleave concerns;
CHANGELOG.md is the authoritative delta list.

## Target branch layout (all off `master`)

1. **`fix/stability`** — uncontroversial fixes, PR-ready first:
   - TouchEngine.framework update to c3ceb1a (prerequisite; commit 7bacc42
     cherry-picks clean)
   - macOS FX flicker fix (busy path draws cached frame) + input base-pass
     bindings
   - Enumeration robustness (log+skip, read-before-register, default case,
     load-error surfacing, empty-path guard)
   - InstanceReady mis-semantics fix (Reload segfault root cause) + failed
     loads don't resume + isLoadPending debounce + queued superseding loads
     + TELinkEventRemoved safety net
   - TE lifecycle mutex (+ FrameDidFinish atomic fast path)
   - macOS red/blue swap fix (remove .bgra swizzle)
   - Post-enumeration display refresh (value-event re-raise sweep)
   NOTE: Menu2 collision fix is entangled with per-family counters — it goes
   in the naming branch even though it is a fix, and the PR text should say so.
2. **`feat/slot-naming-ranges`** — BREAKING for saved comps, stacked on
   fix/stability:
   - Unique static slot names (Float1-40, Int, Toggle, Text, Menu1-40,
     Color1R..A; events stay uniform "Pulse" by design)
   - Per-family ParamID counters (fixes Menu2 collision; menus store initial
     value; GetFloatParameter handles FF_TYPE_OPTION)
   - Float range remap (normalized wire, ParameterRanges, EffectiveRange,
     GetParameterDisplay override)
3. **`feat/dynamic-params`** — additive #28 stack, stacked on both above:
   - Dirty-only push (+ pulse falling-edge re-dirty)
   - ValueChange handler (echo absorb) + par-state echo channel
     (EchoNameToParamID, MenuTokens, HandleEchoChop/Dat, ApplyEchoValue)
   - Settling window (LastPushFrame / EchoSettleFrames)
   - Newest-engine preference (macOS scan + TEInstanceSetPreferredEnginePath
     + configured-engine logging)
4. **`modernize-te`** stays Vincent's working branch = merge of all three
   (verify it diffs empty against the current tree except docs).

## Method (patch surgery, not cherry-pick)

Commits mix concerns; reconstruct by building each branch from file states:
- Start `fix/stability` from master; apply the framework commit; then port
  hunks from the current tree, EXCLUDING naming/range/echo code. Practical
  approach: check out current TouchEnginePluginBase.* / TouchEngine.cpp /
  TouchEngineFX.cpp, then strip: per-family counters (keep upstream ParamID
  math EXCEPT it must keep compiling — where the fix requires the counter
  (Menu2), leave upstream behavior and note it), unique names, ranges,
  echo/dirty members and handlers. Build after every excision.
- `feat/slot-naming-ranges`: branch from fix/stability, port naming + range
  hunks. Build.
- `feat/dynamic-params`: branch from that, port the rest — the diff vs
  modernize-te src/ should be empty at the end (use
  `git diff feat/dynamic-params modernize-te -- src/` as the acceptance test).
- Each branch: `cmake --build build-modern --config Release` must succeed
  (reconfigure per branch or use a scratch build dir per branch).

## FORK-GUIDE.md (new, repo root)

Source-of-truth decoder for the fork. Contents:
- One-paragraph fork purpose + branch map (which branch for which audience)
- Table: every behavioral change → category (fix / breaking / additive) →
  branch → CHANGELOG anchor → upstream issue it addresses (#28 #33 #34 ...)
- Migration notes for the breaking branch (slot renames: saved comps + OSC
  maps; normalized float wire: OSC senders must send 0-1)
- Echo-channel authoring convention for tox builders (Par DAT -> Out DAT
  preferred, CHOP optional; component naming rules; settling-window
  semantics; engine version requirement — newest-engine preference covers
  unpinned folders, TouchEngine symlink pins)
- Verification status matrix (macOS verified live / Windows untested)

## Kickoff prompt for the next session

```
Read docs/knowledge/next-session-branch-split.md and CHANGELOG.md first.
On drmbt/FFGLTouchEngine, branch modernize-te holds 12 commits over master
mixing fixes and features. Execute the branch split exactly as the plan
describes: build fix/stability (uncontroversial fixes), then
feat/slot-naming-ranges (breaking naming/ranges), then feat/dynamic-params
(#28 echo stack), each stacked on the previous, each verified with a
Release build. Acceptance: `git diff feat/dynamic-params modernize-te -- src/`
is empty. Then write FORK-GUIDE.md at the repo root per the plan's spec and
commit it on modernize-te. Do not push or open PRs yet — Vincent's Windows
verification pass gates that. If a hunk resists clean separation, prefer
keeping fix/stability conservative (move the entangled piece up a branch)
and note it in FORK-GUIDE.md.
```
