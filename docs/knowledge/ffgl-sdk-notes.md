# FFGL SDK (resolume/ffgl) — Findings (researched 2026-07-23)

SDK repo: https://github.com/resolume/ffgl — dormant since mid-2023 (HEAD b1afaf9,
2023-06-30). Tags: v2.1 (2019, Resolume 7.0.3+), v2.2 (2020-10-20, Resolume 7.3.0+).
Master = unreleased "FFGL 2.3": dynamic display names + value events (Resolume
7.4.0+), dynamic option elements (7.4.1+), GL 4.6/glew. No FFGL_VERSION macro;
plugins report APIMajor/Minor via CFFGLPluginInfo (examples declare 2.1).

## 1. Float parameters: NO 0–1 normalization required — real values cross the ABI

- Since commit 10e7e39 (2019-09-05, in v2.1): "host-plugin now communicate in
  parameter values when ranges are involved" — when a plugin declares a range via
  `SetParamRange(index, min, max)`, the host sends/receives **real** values (e.g.
  237.5 for a -10..500 param), not normalized ones. FF_GET_RANGE = 41 opcode fills
  a RangeStruct {min,max}. FF_TYPE_INTEGER = 13 for stepped int ranges.
- So a TD float -10..500 can be exposed with its true range and passed straight to
  TouchEngine with no reranging. **Every Resolume 7.0.3+ supports this.**

### Gotchas (both directly relevant to FFGLTouchEngine)

1. **Default clamp:** `SetParamInfo(float)` clamps FF_TYPE_STANDARD defaults into
   [0,1] (FFGLPluginManager.cpp:279-285). The quickstart lib works around it by
   writing `ParamInfo::defaultFloatVal` directly after the fact (FFGLPlugin.cpp:271-284).
   FFGLTouchEngine must do the same or out-of-range defaults are wrong.
2. **CRITICAL — ranges are per-DLL prototype, not per-instance:** the stock
   `plugMain` dispatch for FF_GET_RANGE ignores the instance ID and always consults
   the static prototype `s_pPrototype` (FFGL.cpp:518-520). There is NO
   range-changed event — `FF_EVENT_FLAG_RANGE` is marked "Not supported yet"
   (FFGL.h:450-452).
   **Consequence for FFGLTouchEngine:** since each instance loads a different .tox
   at runtime, per-instance `SetParamRange` calls made after tox load are likely
   invisible to Resolume — the host reads ranges from the prototype, which was
   built before any tox existed. This reconciles the contradiction between the
   code (which calls SetParamRange with real TD ranges) and user reports of 0–1
   sliders (#8/#24/#27). NEEDS EMPIRICAL VERIFICATION in Resolume.
   Workaround if confirmed: keep wire values 0–1 and remap to the TD range inside
   the plugin ("under-the-hood remapping", as Joris/Resolume suggested in
   resolume/ffgl#15/#52), using `GetParameterDisplay` to show the real value as
   text. Or patch plugMain/getParamRange to dispatch per-instance (host still may
   cache ranges at param creation — test).

## 2. Dynamic parameters: set is FIXED per DLL; per-instance mutation via events

- Param count/names/types/defaults/ranges are queried from a single static
  prototype created at FF_INITIALISE_V2. No add/remove-parameter call, no
  count-changed event. Hence FFGLTouchEngine's pre-allocated hidden slot grid —
  that IS the sanctioned workaround.
- What CAN change per instance at runtime (FFGL 2.3 event system, pull-based —
  host polls FF_GET_PARAMETER_EVENTS = 46 each update loop; plugin calls
  `RaiseParamEvent()`):
  - `FF_EVENT_FLAG_VISIBILITY` — SetParamVisibility (per-instance via opcode 45)
  - `FF_EVENT_FLAG_DISPLAY_NAME` — SetParamDisplayName (opcode 51, per-instance;
    Resolume 7.4.0+)
  - **`FF_EVENT_FLAG_VALUE` — plugin-driven value changes, Resolume 7.4.0+.
    See Events example (FFGLEvents.cpp:173-217). THIS IS THE MECHANISM FOR
    ISSUE #28** (TD-side preset recall/interpolation/randomize reflected in
    Resolume): hook TouchEngine's `TELinkEventValueChange` in linkCallback
    (currently ignored), update the stored FFGL value, RaiseParamEvent(VALUE).
  - `FF_EVENT_FLAG_ELEMENTS` — SetParamElements for FF_TYPE_OPTION; element
    dispatches DO pass the instance pointer, so combo contents can differ per
    instance (Resolume 7.4.1+).
- Cannot change per instance: a slot's TYPE (float vs text vs option) and its
  RANGE (prototype-static, no event).

## 3. Texture bit depth: FFGL has NO format negotiation

- `FFGLTextureStruct` = {Width, Height, HardwareWidth, HardwareHeight, GLuint
  Handle} — no format field. Plugin renders into the host's bound FBO
  (ProcessOpenGLStruct.HostFBO); input/output formats are entirely the host's
  choice and invisible to the API.
- No bit-depth caps (FreeFrame 1.x FF_CAP_16/24/32BITVIDEO are retired). Zero hits
  for GL_RGBA16F/32F in the SDK lib. Only float-format touchpoint is the
  plugin-internal ffglex::FFGLFBO helper (default GL_RGBA8, accepts any internal
  format) — for your own intermediates, not the host contract.
- Resolume side: output pipeline is 8bpc; "High Quality" preference raises internal
  processing precision only; staff say no HDR — output converts to 8bpc. 10-bit
  exists only at the display-output stage (resolume.com/support/en/10-bit-color-output).
- **Consequence for issue #12:** a correct plugin is format-agnostic — sample the
  input handle, draw into HostFBO. The fix for 32-bit toxes is NOT to deliver float
  textures to Resolume (impossible via FFGL today); it is to make the wrapper's
  interop handle TE's float formats and convert down without rendering black.
  Precision loss at the Resolume boundary is inherent to FFGL.

Forum refs: resolume.com/forum t=15058 (HDR/High Quality), t=21650 (HDR request).
