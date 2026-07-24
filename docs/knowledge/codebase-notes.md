# FFGLTouchEngine — Codebase Notes (master @ 494bd03, 2026-07-23)

## Architecture

Two FFGL plugins from one shared base:

- `src/plugins/shared/TouchEnginePluginBase.{h,cpp}` — `FFGLTouchEnginePluginBase : CFFGLPlugin`.
  Owns TEInstance, param discovery/mapping, TE callbacks, device init, mac IOSurface helpers.
- `src/plugins/FFGLTouchEngine/TouchEngine.cpp` — generator ("TouchEngine", FFGL ID `TE01`, FF_SOURCE, 0 inputs)
- `src/plugins/FFGLTouchEngineFX/TouchEngineFX.cpp` — effect ("TouchEngineFX", ID `TEFX`, FF_EFFECT, 1 input)

One generic dll/bundle serves all .tox files — the tox is a runtime FF_TYPE_FILE param
(index 0), params 1/2/3 are Reload/Unload/Clear Instance events. No per-effect codegen.

**Windows render path:** TE renders on a plugin-created D3D11 device →
keyed-mutex sync (`TEInstanceGetTextureTransfer` + `AcquireSync`) → `CopyResource` →
**Spout used as an in-process DX↔GL bridge** (`WriteTexture` → `ReadGLDXtexture`) →
GL quad draw. FX plugin pushes host input the reverse way (`WriteGLDXtexture` →
`ReadTexture` → `TED3D11TextureCreate` → `TEInstanceLinkSetTextureValue`).

**macOS render path:** Metal + TEMetalContext; output blitted (blocking
`waitUntilCompleted`) into IOSurface-backed `MTLPixelFormatBGRA8Unorm` texture →
`CGLTexImageIOSurface2D` → GL_TEXTURE_RECTANGLE drawn with Y-flip + `.bgra` swizzle
shader. FX input: host GL → scratch FBO → IOSurface → `glFinish()` →
`TEIOSurfaceTextureCreate(..., TETextureFormatBGRA8Unorm, ...)`.

**Cooking model:** `TETimeExternal`, hardcoded 60fps (`TEInstanceSetFrameRate(instance, 60, 1)`,
base cpp:264; `TEInstanceStartFrameAtTime(instance, FrameCount, 60, false)`).
`isTouchFrameBusy` atomic gates one frame in flight; output fetched before kicking
next cook (≥1 frame latency). **The hardcoded 60 is the root cause of issue #17**
(absTime runs fast on 144Hz).

## Parameters — how mapping works

FFGL can't add params after instantiation → constructor pre-allocates hidden slots
(`ConstructBaseParameters`, base cpp:415-463): `OffsetParamsByType=4`, `MaxParamsByType=40`.

| Region | Indices | FFGL type |
|---|---|---|
| reserved | 0–3 | FILE + 3× EVENT |
| floats | 4–43 | FF_TYPE_STANDARD |
| ints | 44–83 | FF_TYPE_INTEGER (default range −10000..10000) |
| booleans | 84–123 | FF_TYPE_BOOLEAN |
| strings | 124–163 | FF_TYPE_TEXT |
| pulses | 164–203 | FF_TYPE_EVENT |
| menus | 204–243 | option params, max 10 choices |
| colors | 244–283 | groups of 4 (RED/GREEN/BLUE/ALPHA) |

Enumeration: `GetAllParameters()` (cpp:483-571) runs **once per tox load** (from
`TEEventInstanceDidLoad` → `ResumeTouchEngine`). Walks link groups, activates slots
via `SetParamDisplayName(..., true)` + `SetParamVisibility(..., true, true)` +
`RaiseParamEvent` (relies on FFGL 2.3 dynamic display names).

**`linkCallback` explicitly ignores `TELinkEventAdded` and `TELinkEventValueChange`
(cpp:995-1006) — this is the hook point for issue #28 (dynamic value updates).**

### Float ranging — NO 0–1 normalization in current code

```cpp
// base cpp:684-694
TEInstanceLinkGetDoubleValue(instance, id, TELinkValueUIMaximum, &max, 1);
TEInstanceLinkGetDoubleValue(instance, id, TELinkValueUIMinimum, &min, 1);
SetParamRange(ParamID, min, max);
```
Host value stored verbatim and sent unscaled via `TEInstanceLinkSetDoubleValue`.
Range handling is delegated to the host via FFGL `SetParamRange` (FFGL ≥2.2).
So TD float ranges ARE preserved — the "0–1 FFGL limitation" claims in issues
#8/#24/#27 predate or contradict this code path; retest before believing them.

### Type coverage

- Vector doubles (ColorRGBA / PositionXYZW / SizeWH intents) → per-component params,
  recombined on send. Color intents use the RED/GREEN/BLUE/ALPHA slots (added Apr 2026).
- Int → FF_TYPE_INTEGER, UI min/max via SetParamRange; menus via
  `TEInstanceLinkGetChoiceLabels` + `SetParamElements` (int links only).
- Pulse/momentary intents → FF_TYPE_EVENT; pulse IDs auto-reset to false after send.
- String → FF_TYPE_TEXT.
- **Unhandled:** TELinkTypeFloatBuffer (CHOP/audio!), TELinkTypeStringData (DAT),
  TELinkTypeComplex, TELinkTypeSequence — silent fall-through. No audio support
  despite declared-but-unused `TEAudioInFloatBuffer1/2` members (relevant to #32).

## Texture formats / bit depth (issue #12)

**Windows:** default `DXFormat = DXGI_FORMAT_B8G8R8A8_UNORM` (base h:107). Interop and
staging textures nominally use TE's own format, BUT:
- `GlToDXFromat` (cpp:36-54): only maps RGBA8/RGB8/RGBA16 — no 16F/32F; no default
  return (UB).
- `InitializeGlTexture` hardcodes unsized `GL_RGBA` internal format (cpp:188) — even a
  16/32-bit TE output lands in an 8-bit GL texture.

**macOS:** hard-forced BGRA8 everywhere — IOSurface 4 bytes/element 'BGRA'
(cpp:1054-1059), `MTLPixelFormatBGRA8Unorm` (cpp:1073 + per-plugin literals),
`CGLTexImageIOSurface2D` GL_RGBA/GL_BGRA/UNSIGNED_INT_8_8_8_8_REV (cpp:1027-1032),
FX input `TETextureFormatBGRA8Unorm` (TouchEngineFX.cpp:478).

**To support 16F/32F:** extend GlToDXFromat + GetGlType, use sized internal formats
(GL_RGBA16F/32F) in InitializeGlTexture; on macOS parametrize CreateIOSurface
bytes-per-element, Metal pixel formats, CGL format/type, FX input TE format; fix the
`.bgra` shader swizzle for RGBA-ordered float surfaces. TE itself supports
`TETextureFormatRGBA16F/RGBA32F` (include/TouchEngine/TETexture.h). Test asset:
`Example/NoiseOutOnly32Bit.tox`. Host-side ceiling depends on Resolume's FFGL
texture handoff — see ffgl-sdk-notes.md / resolume-bitdepth.md.

## Known defects (by severity)

1. `FF_FALSE` (== 0 == FF_SUCCESS) returned on error paths → silent success
   (TouchEngine.cpp:165,233; TouchEngineFX.cpp:205,266).
2. Windows generator recreates Spout interop **every frame**: resize test compares a
   GL type enum to `GLFormat`, which the generator never assigns (TouchEngine.cpp:200-204).
3. `GetGlType(GLint)` mixes type/format domains, falls off switch with no return → UB
   (cpp:57-64); `GlToDXFromat` same no-default problem.
4. Menu param ID collision: choices branch computes ID from `ParameterMapInt.size()`
   but never inserts → two dropdowns both get ID 204; current value read but not stored
   (cpp:705,722).
5. Float slot overflow: float ParamIDs use `Parameters.size()` (all types) not a
   per-type count (cpp:640,666) → ≥40 earlier params pushes a float into the int region.
   Only guard is global 240 cap.
6. D3D immediate-context double release (explicit `Release()` + ComPtr dtor)
   (TouchEngine.cpp:236-247; TouchEngineFX.cpp:270-283).
7. TE callback thread mutates `Parameters`/maps + calls FFGL SetParam*/RaiseParamEvent
   with NO locking vs. host render/param threads (cpp:975-1006). Only atomics are the
   bool flags. Likely source of load/reload crashes (#34, #19 class of bugs).
8. Spout named access mutexes are constants ("mutex","mutex1","mutex2") — multiple
   instances collide even though sender names are randomized.
9. `Unload` event doesn't clear `isTouchEngineLoaded/Ready` flags (cpp:304-311).
10. FX passthrough draw sets shader uniforms without binding the shader
    (TouchEngineFX.cpp:189-192).
11. `AcquireSync(..., INFINITE)` can hang render thread if TE stalls; mac paths block
    with waitUntilCompleted/glFinish every frame (perf).
12. `rand()` for Spout sender names; generator never seeds; %-off-by-one drops 'z'.
13. Dead code: commented CreateInput/OutputTexture blocks, unused audio buffers,
    unused MutexMap, unreachable FFGLLog.
14. README says 30-param limit; code allows 40 per type.

## Version facts

- FFGL SDK vendored at `src/lib/FFGL`, header history through **FFGL 2.3** (dynamic
  display names); code depends on 2.2/2.3 features but plugins declare API 2.1 in
  CFFGLPluginInfo (TouchEngine.cpp:7-8). FFGL 2.3 features ⇒ Resolume 7.3.1+.
- **Vendored TouchEngine = TD 2023.11780** — TouchEngine.dll PE ProductVersion is
  `2023.11780`; headers + dll committed 2024-07-05 ("Updated touchengine to
  2023.11780") and NEVER updated since. The mac TouchEngine.framework (added
  2025-01-09 by t3kt, universal x86_64+arm64, Xcode 14.3.1 / macOS 13.3 SDK, min
  10.15) carries no build number in its metadata (generic 1.0) but is the same
  2023-era library. Note: the TE library is a loader — the actual engine process
  comes from the user's installed TouchDesigner; the 2023.11780 lib/headers vs. a
  TD 2025 install is the likely root of #32-class breakage. README: works with TD
  2023; early 2023 breaks int menus.
- **README overpromise (2024-07-22 commit 0b48425):** line 68 claims "8, 16, and 32
  bit textures out of TouchDesigner are supported... downsampled to the max
  resolume supports (16 bit)" — added two weeks after #12 was closed, but the code
  still lands output in an unsized GL_RGBA texture on Windows and forces BGRA8 on
  macOS. No commit ever implemented 16-bit downsampling.
- Windows: VS v143, SDK 10.0.19041.0, C++17, /MT. CI: windows-latest, CMake -A x64.
- macOS: deployment target 13.0, Xcode generator, ad-hoc codesign (cmake/sign_bundle.sh).
- vcpkg: GLEW 2.1.0, libpng, zlib.

## Effect authoring workflow (for the skill)

1. TD component with output TOP named `out1` (generator) and input TOP `in1` (FX —
   name strictly required, TouchEngineFX.cpp:663-670; generator accepts any texture out).
2. Expose custom params on the component; ≤40 per type (README claims 30); supported
   types: float, int, menu(int, ≤10 choices), bool, pulse/momentary, string, color
   (RGBA intent), XY/XYZ/WH vectors. No CHOP/DAT/audio.
3. Needs TD Commercial/Pro/Edu license on the machine; TE + Resolume on same GPU.
4. Install: Windows — FFGLTouchEngine.dll + FFGLTouchEngineFX.dll + TouchEngine.dll
   together in Resolume plugins dir. macOS — .bundle files (TouchEngine.framework
   embedded + ad-hoc signed).
5. In Resolume: add TouchEngine source or TouchEngineFX effect, point "Tox File"
   param at the .tox. Params appear after TE load. Reload/Unload/Clear manage instance.

## macOS deployment & TD version selection (verified 2026-07-23)

- Bundles embed TouchEngine.framework in Contents/Frameworks — nothing extra to
  install (unlike Windows' side-by-side TouchEngine.dll).
- Ad-hoc signed only → downloaded bundles need
  `xattr -dr com.apple.quarantine <bundle>` or Resolume silently won't load them.
- TD install selection (no path config in the plugin):
  1. `TEInstanceSetPreferredEnginePath()` exists (TEInstance.h:531-538) but the
     plugin NEVER calls it — worth adding as a plugin parameter on the fork.
  2. User override: `TOUCHENGINE_APP_PATH` env var (string in libTouchEngine.dylib)
     or a file-system link.
  3. Default: auto-discovers installed TDs via Launch Services (bundle ID
     `ca.derivative.TouchDesigner`), spawns that install's
     Contents/Helpers/TouchEngine.app as the engine process.
- License enforcement in libTouchEngine.dylib: fails if no TD/TouchPlayer key, if
  key doesn't permit TouchEngine, or if the key's update date is older than the
  TouchEngine build. Errors also if the engine TD is older than the TD that saved
  the .tox.
- This machine (2026-07-23): TD builds .32280/.32820/.33070 installed (2025-gen)
  vs vendored 2023.11780 framework — incoherent stack until Sprint 0 modernizes.
