# FFGLTouchEngine Knowledge Base

Research snapshot from 2026-07-23 (repo master @ 494bd03). Companion skill:
`.claude/skills/ffgl-tox-effect` (authoring toxes for this wrapper).

- **[backlog.md](backlog.md)** — ⭐ **the durable cross-session TODO list.** Start
  here. Everything that outlives a session lives in it: hardcoded 60fps, the
  high-bit-depth/displacement investigation, engine resource lifetime, the
  logger parameter, and the upstream issues worth re-checking. Dated
  `test-results-*.md` files are immutable snapshots; this one gets edited.
- **[logger-param-design.md](logger-param-design.md)** — design note for the
  diagnostic string parameter (feasibility, the per-frame trap, why one build
  rather than a debug variant).
- **[issues-audit.md](issues-audit.md)** — all 28 issues + 6 PRs audited: what's
  genuinely fixed, what was hand-waved closed, re-verification priority list,
  TD/Resolume version matrix from issue threads.
- **[codebase-notes.md](codebase-notes.md)** — architecture (Spout DX↔GL interop on
  Windows, Metal/IOSurface on macOS), parameter slot system and type mapping,
  exact 8-bit bottleneck locations for issue #12, 14 cataloged defects.
- **[ffgl-sdk-notes.md](ffgl-sdk-notes.md)** — FFGL float ranges (real values cross
  the ABI since 2.1; prototype-static range gotcha), dynamic parameter event
  system (FF_EVENT_FLAG_VALUE = the mechanism for issue #28), no texture format
  contract in FFGL.
- **[resolume-bitdepth.md](resolume-bitdepth.md)** — Resolume 7.24's 10-bit/16 bpc
  pipeline, release timeline through 7.27.1, blog catalog 2025–2026.
- **[sprint-plan.md](sprint-plan.md)** — prioritized sprint plan for the fork:
  modernize → probe → verify → fix → dynamic params (#28) → ranges → bit depth.

## Headline conclusions

1. **Floats need no 0–1 normalization** — FFGL passes real values when a range is
   declared (since v2.1 / Resolume 7.0.3). But FF_GET_RANGE reads the static
   prototype, so per-instance ranges set after tox load may not reach Resolume's
   UI — verify empirically; fallback is under-the-hood remap + GetParameterDisplay.
2. **Issue #28 (dynamic value updates) is implementable**: TouchEngine's
   TELinkEventValueChange (currently ignored in linkCallback) →
   RaiseParamEvent(FF_EVENT_FLAG_VALUE), Resolume 7.4.0+.
3. **Issue #12 (32-bit textures)**: wrapper forces 8-bit at specific, identified
   code sites; TE supports RGBA16F/32F; Spout supports 16-bit; Resolume 7.24+ runs
   16 bpc comps — but FFGL has no format contract, so host-side ceiling needs an
   empirical FBO-format probe.
4. **Tested baseline**: TD 2023.11880-era + Resolume 7.23.2 (all late-2025 reports);
   TD 2025 experimental known-broken for audio (no CHOP/FloatBuffer support at all).
5. **Top re-verification targets**: #34 (unload/reload dup params), #33 (pulse/
   momentary/OSC ordering), #12, #17 (hardcoded 60 fps — still in code), #32.
