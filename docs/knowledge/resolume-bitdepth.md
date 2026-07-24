# Resolume — Bit Depth, Releases & Blog Knowledge Base (researched 2026-07-23)

Current version: **Arena/Avenue/Wire/Alley 7.27.1 (rev 15990), released 2026-07-17.**

## The bit-depth story (relevant to issue #12)

**Resolume 7.24 (2026-01-27) added 10-bit colour output.**
Blog: https://www.resolume.com/blog/32999
Support article: https://resolume.com/support/en/10-bit-color-output

Key facts:
- Display-driven feature: 10-bit output to LED processors/projectors, less banding
  on gradients/beams/fades. Positioned for broadcast/virtual production.
- **Explicitly NOT HDR** — still SDR color space.
- To use it: Composition → Settings → Color Depth must be raised from 8 bpc to
  **16 bpc** ("ensures Resolume processes colors with enough precision"). 16 bpc
  rendering is significantly heavier on GPU.
- Codecs: ProRes 422/4444 carry 10-bit; **DXV is 8-bit** and not designed for a
  10-bit pipeline. Stills: 16-bit PNG/TIFF.
- Capture: all capture devices can input/output 10-bit (NDI 6.1.1, Blackmagic
  Desktop Video 12.1).
- **Texture sharing: NDI and Spout support 16-bit; Syphon does not.** (Directly
  relevant to FFGLTouchEngine's Spout-based DX↔GL interop on Windows.)
- Hardware: DP 1.2+/HDMI 2.0+/TB3+; NVIDIA 30-bit SDR + 10bpc; macOS automatic.

**Implications for FFGLTouchEngine issue #12 (32-bit toxes render black):**
1. The maintainer's 2024 rationale ("resolume runs only in 8 bit maybe") is now
   outdated: since 7.24 the composition pipeline can run at 16 bpc.
2. BUT the FFGL SDK itself has no texture-format contract (no format field, no
   bit-depth caps; see ffgl-sdk-notes.md) and got zero commits since June 2023 —
   no SDK-level bit-depth API accompanied 7.24. Nothing published says FFGL
   plugins receive/return 16-bit textures in a 16 bpc composition.
3. Empirical test needed: in a 16 bpc comp, query the bound HostFBO's attachment
   format (glGetFramebufferAttachmentParameteriv) and the input texture's internal
   format from inside an FFGL plugin. If Resolume hands 16F attachments, the
   wrapper could preserve >8-bit precision end-to-end via Spout (16-bit capable).
4. Regardless of host ceiling, the wrapper-side fix stands on its own: handle TE's
   RGBA16F/RGBA32F outputs in the interop and convert, instead of rendering black.

## FFGL-relevant changes in recent Resolume releases

- 7.27: "#24065 Remove FFGL context locking"; "#25116 Remove extra render pass
  from Wire Generator, Effect and Mixer" (perf; watch for threading behavior
  changes affecting plugins). Source: resolume.com/update/releaseNotes_updates_wire.html
- 7.26 (2026-04-28): **local MCP servers for Arena/Avenue and Wire** (AI tools can
  build comps/patches), Wire REST API v2.
- FFGL SDK repo dormant since June 2023; FFGL 2.3 features (display names, value
  events 7.4.0+, dynamic elements 7.4.1+) remain the newest plugin-facing API.

## Release timeline (Arena/Avenue 7.x)

| Version | Date | Headlines |
|---|---|---|
| 7.21 | 2024-07-22 | Transform widget, Wire param grouping |
| 7.22 | ~2024-12-06 | Multi-column ops, autopilot, LUTs, Wire resources |
| 7.23.0 | 2025-07-15 | Parameter animation presets, performance (only major 2025 release) |
| 7.24 | 2026-01-27 | **10-bit output, 16 bpc composition**, 10-bit capture, faster comp loading, CRT effect, REST API |
| 7.25 | 2026-03-19 | Per-clip transitions, Slice Transform scale/position; "smaller, more frequent releases" policy |
| 7.26 | 2026-04-28 | **MCP servers**, Wire REST API v2 |
| 7.27.1 | 2026-07-17 | Autopilot transition compensation, Alley play modes, Wire Smooth node; FFGL context-locking removed. Current. |

## Blog post catalog (2025 → Jul 2026)

| Date | Title | URL |
|---|---|---|
| 2025-07-15 | 7.23 Release — Parameter Animation Presets, Performance, Wire Nodes | resolume.com/blog/30807 |
| ~2025 | New footage packs (several) | /27805 /27806 /28929 /28930 /28931 /30694 |
| ~2025-12 | A VJ's Guide to Copyright | /31360 |
| 2026-01-27 | **7.24 Release — 10-bit Colour Output** | /32999 |
| 2026-03-19 | 7.25 Release — Clip Transitions, Slice Transform | /34172 |
| 2026-04-28 | 7.26 Release — MCP Servers, REST API | /34484 |
| 2026-07-16 | 7.27 Release — Autopilot Compensation, Alley Play Modes | /34561 |
| ~2026 | Footage packs | /33393 /34543 |

(Resolume blog pages don't display dates; dates reconstructed from download page,
third-party coverage, and Wayback captures.)
