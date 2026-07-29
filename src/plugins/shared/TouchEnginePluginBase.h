#pragma once

#ifdef _WIN32
// Both must precede windows.h. NOMINMAX suppresses the min/max macros, which
// otherwise swallow the std::min/std::max calls in the parameter-range code.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_4.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#include <wrl.h>
#include <shobjidl.h>
#endif

#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <OpenGL/CGLIOSurface.h>
#include <IOSurface/IOSurface.h>
#import <Metal/Metal.h>
#include <TouchEngine/TEMetal.h>
#endif

#include "FFGL/FFGLSDK.h"
#include <array>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include "TouchEngine/TouchObject.h"
#include "PresetMorph.h"

#ifdef _WIN32
#include "TouchEngine/TED3D11.h"
#include "SpoutGL/SpoutSender.h"
#endif

FFResult FailAndLog(std::string message);
std::string GetSeverityString(TESeverity severity);
std::string GenerateRandomString(size_t length);

#ifdef _WIN32
DXGI_FORMAT GlToDXFromat(GLint format);
#endif

GLenum GetGlType(GLint format);

#ifdef _WIN32
GLenum GetGlType(DXGI_FORMAT format);
#endif

typedef struct {
	std::string identifier;
	uint8_t count;
	FFUInt32 children[4];
} VectorParameterInfo;

class FFGLTouchEnginePluginBase : public CFFGLPlugin
{
public:
	FFGLTouchEnginePluginBase();
	~FFGLTouchEnginePluginBase() override;

	// //CFFGLPlugin
	// FFResult InitGL(const FFGLViewportStruct* vp) override;
	// FFResult ProcessOpenGL(ProcessOpenGLStruct* pGL) override;
	FFResult DeInitGL() override;
	
	FFResult SetFloatParameter(unsigned int dwIndex, float value) override;
	FFResult SetTextParameter(unsigned int dwIndex, const char* value) override;

	float GetFloatParameter(unsigned int index) override;
	char* GetTextParameter(unsigned int index) override;
	char* GetParameterDisplay(unsigned int index) override;

protected:
	FFResult InitializeDevice();
	FFResult InitializeShader(const std::string& vertexShaderCode, const std::string& fragmentShaderCode);

	void InitializeGlTexture(GLuint& texture, uint16_t width, uint16_t height, GLenum type);

	FFResult PushParametersToTouchEngine();

	bool LoadTEGraphicsContext(bool Reload);
	bool LoadTEFile();

	virtual void LoadTouchEngine();
	virtual void ResumeTouchEngine() = 0;
	virtual void ClearTouchInstance() = 0;

	void ConstructBaseParameters();
	virtual void ResetBaseParameters();
	void GetAllParameters();
	void CreateIndividualParameter(const TouchObject<TELinkInfo>& linkInfo);
	void CreateParametersFromGroup(const TouchObject<TELinkInfo>& linkInfo);

	virtual void HandleOperatorLink(const TouchObject<TELinkInfo>& linkInfo) = 0;

	// ---- Idle engine release -------------------------------------------------
	// A TouchEngine process costs ~1.3-1.5 GB and, left alone, is held for the
	// lifetime of the clip: measured on Windows, ejecting a layer frees nothing,
	// and only Clear Instance or deleting the clip gives it back. For a
	// timecode-driven set where each effect fires for one track, that ratchets
	// memory upward all night.
	//
	// There is no host callback to hang this on. Resolume fires FFGL's Connect()
	// when a clip is armed but does NOT fire Disconnect() when it is ejected
	// (verified with an instrumented build), so deactivation has to be inferred
	// from the render loop going quiet.
	FFUInt32 ReleaseIdleParamID = 0;
	bool ReleaseWhenIdle = true;

	// Seconds of no rendering before the engine is handed back, exposed per
	// clip as "Idle Seconds". 20 s by default: long enough that a transition, a
	// beat-synced retrigger or a brief cut away does not pay the reload cost,
	// short enough to reclaim between tracks. The failure is asymmetric —
	// releasing too eagerly stalls a return for 25-40 s, holding on too long
	// only costs memory already being held — so it errs towards holding on.
	//
	// 0 means "as soon as this can safely be detected" — see IdleSecondsFloor,
	// which is what actually applies. Low values are legitimate but fragile:
	// any gap in rendering longer than the threshold costs a full reload, and
	// a dropped-frame hitch is indistinguishable from a clip going away.
	FFUInt32 IdleSecondsParamID = 0;
	double IdleReleaseSeconds = 20.0;
	static constexpr double IdleSecondsDefault = 20.0;
	static constexpr double IdleSecondsMax = 300.0;
	// Hard floor on the effective threshold. Below roughly one watchdog tick
	// the test stops distinguishing "stopped" from "currently rendering" and
	// starts killing live clips — see the watchdog for the full reasoning.
	static constexpr double IdleSecondsFloor = 1.0;

	// Stamped by ProcessOpenGL on both plugins. Atomic so the render thread
	// never blocks on the watchdog.
	std::atomic<long long> LastRenderTick{ 0 };
	std::atomic<bool> WatchdogStop{ false };
	std::thread WatchdogThread;
	// True once the watchdog has handed the engine back, so the next render
	// knows to reload rather than sitting on an empty instance.
	bool EngineReleasedIdle = false;

	// Parameter values captured at idle release and re-applied after the tox
	// comes back, so the round trip does not reset the clip to tox defaults.
	std::unordered_map<FFUInt32, double> RetainedFloat;
	std::unordered_map<FFUInt32, int32_t> RetainedInt;
	std::unordered_map<FFUInt32, bool> RetainedBool;
	std::unordered_map<FFUInt32, std::string> RetainedString;
	bool RetainedValuesValid = false;

	void NoteRendered();
	void StartIdleWatchdog();
	void StopIdleWatchdog();
	// Releases ONLY the TE instance (and with it the engine process). Does not
	// touch GL or the Spout interop: the watchdog has no GL context, and those
	// resources are cheap to keep. Non-virtual and self-contained so it is safe
	// to call while the object is being torn down.
	void ReleaseEngineForIdle();
	// Brings the tox back when the clip is armed. Covers both an idle release
	// and a manual Clear Instance — in either case the instance is gone and a
	// trigger should restore it.
	void ReloadIfEngineAbsent();

	// FFGL activation hooks. Resolume drives Connect() when a clip is armed.
	unsigned int Connect() override;
	unsigned int Disconnect() override;

	virtual void eventCallback(TEEvent event, TEResult result, int64_t start_time_value, int32_t start_time_scale, int64_t end_time_value, int32_t end_time_scale);
	virtual void linkCallback(TELinkEvent event, const char* identifier);
	virtual void statisticsCallback(const struct TEInstanceStatistics* statistics);

	// ---- Diagnostic Log slot -------------------------------------------------
	// A read-only text slot carrying the state you would otherwise have to tail
	// the host log for: why a load failed, how long it took, and what the engine
	// is costing. It sits AFTER every pre-allocated family so adding it moves no
	// existing slot index, and it is the one slot that stays visible with no tox
	// loaded — a failed load is exactly when it is needed, and at that point no
	// tox parameters exist.
	//
	// Deliberately NOT written from the render thread. The per-frame
	// "Releasing texture" line removed in v3.1.0 produced 702 host-log entries
	// in one short session; this slot only moves on lifecycle events and on
	// TE's own statistics callback (roughly 1 Hz, on TE's schedule, not ours).
	FFUInt32 LogParamID = 0;
	// Last significant event — an error, or the load result. Survives until the
	// next one, so a failure stays on screen.
	std::string LogStatus;
	// Composed on demand from LogStatus + the newest statistics.
	std::string LogText;
	void SetLogStatus(const std::string& status);
	void RefreshLogText();

	// Newest values from TEInstanceStatistics, or <0 when not yet delivered /
	// unsupported by the TouchDesigner build. Guarded by TEStateMutex: TE
	// documents that statistics may arrive on any thread.
	int64_t StatMemGPU = -1;
	int64_t StatMemCPU = -1;
	double StatFPS = -1.0;
	// Per-frame cook cost in ms. Derived from frameTimeCPU, which is CPU time
	// SPENT rather than elapsed — so it answers "how much headroom", while
	// StatFPS (wall-clock derived) answers "how fast is it actually running".
	double StatCookMs = -1.0;
	int64_t StatFramesDropped = -1;
	std::chrono::steady_clock::time_point StatsLastDelivery{};
	// Set when TEInstanceConfigure is issued, so DidLoad can report elapsed time.
	std::chrono::steady_clock::time_point LoadStartTime{};
	bool LoadTimerRunning = false;

	TouchObject<TEInstance> instance;
#ifdef _WIN32
	Microsoft::WRL::ComPtr<ID3D11Device> D3DDevice;
	TouchObject<TED3D11Context> D3DContext;
	Microsoft::WRL::ComPtr <ID3D11Texture2D> D3DTextureInput = nullptr;
	Microsoft::WRL::ComPtr <ID3D11Texture2D> D3DTextureOutput = nullptr;
	std::map<ID3D11Texture2D*, IDXGIKeyedMutex*> TextureMutexMap;

	DXGI_FORMAT DXFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
#endif
#ifdef __APPLE__
	id<MTLDevice> MetalDevice = nil;
	id<MTLCommandQueue> MetalCommandQueue = nil;
	TouchObject<TEMetalContext> MetalContext;

	// Creates an IOSurface-backed Metal texture for sharing with OpenGL
	id<MTLTexture> CreateIOSurfaceBackedMetalTexture(int width, int height, IOSurfaceRef* outSurface);
	// Copies a TE Metal texture into our IOSurface-backed texture via Metal blit
	void CopyMetalTexture(id<MTLTexture> src, id<MTLTexture> dst);
	// Creates an OpenGL texture backed by an IOSurface for zero-copy sharing
	GLuint CreateOpenGLTextureFromIOSurface(IOSurfaceRef surface, int width, int height);
	// Creates an IOSurface suitable for texture sharing
	IOSurfaceRef CreateIOSurface(int width, int height);
#endif
	GLint GLFormat = 0;

	// Serializes TE instance lifecycle (load/unload/reload), parameter map
	// mutation (enumeration on the TE callback thread), and the render thread's
	// per-frame TE section. TE callbacks race the render thread otherwise —
	// observed as a segfault when Reload invalidated links mid-frame
	// (TEInstanceLinkSetTextureValue on a freed link) and as per-frame pushes
	// into dead instances after failed loads. Recursive because lifecycle
	// paths nest (SetFloatParameter -> LoadTEFile, eventCallback ->
	// ResumeTouchEngine -> GetAllParameters -> ResetBaseParameters). All TE
	// load/unload calls under it are asynchronous, so worst-case hold time is
	// milliseconds. Never hold it while blocking on TE completion.
	std::recursive_mutex TEStateMutex;

	std::atomic_bool isTouchEngineLoaded;
	std::atomic_bool isTouchEngineReady;
	// True from a successful TEInstanceLoad until its TEEventInstanceDidLoad
	// arrives. Reload/Unload pulses are ignored while set: configuring a second
	// load onto an in-flight one makes TE free the first load's links after
	// we've already enumerated them and gone ready — the render thread then
	// pushes a dead link identifier and the host segfaults inside TE.
	std::atomic_bool isLoadPending{ false };
	// A load/reload request arrived while a load was in flight. Rather than
	// dropping it (which silently left the OLD tox running after a path
	// change), it is replayed as soon as the in-flight load completes.
	std::atomic_bool isReloadQueued{ false };
	std::atomic_bool isGraphicsContextLoaded;
	std::atomic_bool isTouchFrameBusy;
	std::atomic_bool isBeingDestroyed;
	uint64_t FrameCount = 0;

	//Touch file capabilities
	bool hasVideoInput = false;
	bool hasVideoOutput = false;
	bool isVideoFX = false;

	//TouchEngine parameters
	uint32_t MaxParamsByType = 0;
	uint32_t OffsetParamsByType = 0;
	std::set<FFUInt32> ActiveParams;
	std::vector<std::pair<std::string, FFUInt32>> Parameters;
	std::unordered_map<FFUInt32, FFUInt32> ParameterMapType;
	std::unordered_map<FFUInt32, int32_t> ParameterMapInt;
	std::unordered_map<FFUInt32, double> ParameterMapFloat;
	std::unordered_map<FFUInt32, std::string> ParameterMapString;
	std::unordered_map<FFUInt32, bool> ParameterMapBool;
	std::set<FFUInt32> PulseParameters;
	// ParamIDs the HOST has modified since the last push. Pushing only these
	// (instead of every parameter every frame) is what lets TD-initiated value
	// changes survive (#28): a blanket push stomped them one frame later, and
	// it doubles as the echo guard — values arriving FROM TouchEngine via
	// linkCallback are stored without dirtying, so they are never pushed back.
	std::set<FFUInt32> DirtyParams;

	// Per-family slot counters. Each family owns a contiguous pre-allocated
	// region of MaxParamsByType slots; IDs are familyBase + counter. These must
	// never be derived from ParameterMap*.size() — those maps mix families (and
	// gain entries on host interaction), which is how two menus ended up sharing
	// one ParamID.
	uint32_t FloatParamCount = 0;
	uint32_t IntParamCount = 0;
	uint32_t BoolParamCount = 0;
	uint32_t StringParamCount = 0;
	uint32_t EventParamCount = 0;
	uint32_t MenuParamCount = 0;
	uint32_t ColorParamCount = 0;

	std::set<FFUInt32> ActiveVectorParams;
	std::vector<VectorParameterInfo> VectorParameters;

	// ---- Native color picker (HSBA quads) ------------------------------------
	// Resolume renders its internal color picker (PICK/HSB/RGB/Palette tabs +
	// alpha strip) for a consecutive run of HSBA-TYPED params — HUE, SATURATION,
	// BRIGHTNESS, ALPHA, in that order. RGBA-typed runs only ever render as four
	// loose gradient sliders (verified empirically in Arena 7.27.1; see the
	// ffgl-color-picker skill in drmbt-custom-fx). With this flag on, the
	// pre-allocated color family is declared as HSBA quads and TD-side RGBA
	// parameters are surfaced through them.
	//
	// The host wire then carries HSBA while TouchEngine keeps speaking straight
	// RGBA: ParameterMapFloat for the four children stays RGBA (so the existing
	// vector push/echo paths are untouched) and ColorQuadHsba holds the
	// host-authoritative HSBA state per quad. HSBA is authoritative host-side
	// because RGB->HSB is lossy exactly where a picker lives (hue is undefined
	// for grays); deriving HSBA from RGBA on every host read would snap hue back
	// to red whenever brightness or saturation hits 0.
	//
	// Off by default: converting an existing RGBA-typed layout to HSBA keeps the
	// indices but changes their meaning, so saved comps would reinterpret stored
	// red 1,0,0 as hue 1/sat 0/bright 0 = black. Only new plugin variants
	// (FFGLTouchEngineFXPresets) turn it on.
	bool UseHsbaColorQuads = false;
	// Key = quad head ParamID; value = { hue, saturation, brightness, alpha }.
	std::unordered_map<FFUInt32, std::array<float, 4>> ColorQuadHsba;
	FFUInt32 ColorFamilyBase() const { return OffsetParamsByType + MaxParamsByType * 6; }
	bool IsColorFamilySlot(FFUInt32 ParamID) const;
	FFUInt32 ColorQuadHead(FFUInt32 ParamID) const;
	// Host wrote one HSBA channel: update the quad, re-derive the RGBA children,
	// and dirty them so the whole vector is pushed to TE.
	void SetHsbaChannel(FFUInt32 ParamID, float value);
	// TE wrote the RGBA children (enumeration/echo/ValueChange): re-derive the
	// quad's HSBA, preserving hue/saturation where they are undefined.
	void RefreshQuadHsbaFromRgba(FFUInt32 headParamID);

	// ---- In-plugin preset recall with morph -----------------------------------
	// Ported from drmbt-custom-fx (ffgl-preset-morph skill). Resolume already
	// saves per-effect presets (its P. dropdown) as XML under
	// <Documents>/Resolume Arena/Presets/Video Effects/<effect display name>/.
	// This block reads those same files, so preset SAVING stays native host UI —
	// the plugin only adds recall-with-glide: a Preset menu built from a folder
	// scan, a Morph time, Recall/Rescan triggers, a Snap toggle and a Curve menu.
	// All six sit AFTER every pre-allocated family so no tox-driven slot moves.
	// PresetEffectName must match the plugin's display name exactly — it is the
	// preset folder's name.
	bool EnablePresetControls = false;
	std::string PresetEffectName;
	FFUInt32 PresetParamID = 0;
	FFUInt32 MorphParamID = 0;
	FFUInt32 RecallParamID = 0;
	FFUInt32 RescanParamID = 0;
	FFUInt32 SnapParamID = 0;
	FFUInt32 CurveParamID = 0;
	// Element 0 is always "None" (fresh instances never auto-recall); elements
	// 1..N are the sorted .xml stems. Index == element value.
	std::vector<std::string> PresetNames;
	std::vector<std::string> PresetPaths;
	int PresetSelection = 0;
	float MorphSecondsParam = 0.5f;
	bool SnapRecall = false;
	int CurveSelectionParam = drmbt::preset::kDefaultCurve;
	drmbt::Morpher PresetMorpher;
	// dt clock for the morph: steady_clock deltas between rendered frames,
	// clamped per step, so a bypassed/ejected clip pauses its glide instead of
	// silently finishing on wall time.
	std::chrono::steady_clock::time_point LastMorphTick{};
	bool MorphTickValid = false;
	// Host-restore adoption (the double-recall defense): Arena serializes the
	// Preset param into its preset files and comps, so a stored selection comes
	// back as a SetFloatParameter on P.-dropdown load and comp load and would
	// fire a surprise recall. Host restores arrive as a BURST of param writes,
	// while a user menu click sets only Preset — so writes to recall-set slots
	// stamp MorphFrameCounter, and a Preset change landing within a few frames
	// of one (or before the first render) adopts the selection without recall.
	uint64_t MorphFrameCounter = 0;
	uint64_t LastRecallSetWriteFrame = 0;
	bool HasRecallSetWrite = false;
	bool RenderedOnce = false;
	static constexpr uint64_t PresetAdoptWindowFrames = 15;

	void ConstructPresetParameters();
	void ScanPresetFolder();
	void ApplyPresetElements(bool raiseEvent);
	// Handle a host float write to one of the six preset controls. Returns true
	// when consumed. Sits AHEAD of the TE-ready guards: these are plugin-owned,
	// like the Log slot and the idle controls.
	bool HandlePresetSetFloat(unsigned int dwIndex, float value);
	// Read-back for the six controls; returns true when dwIndex is one of them.
	bool GetPresetFloat(unsigned int dwIndex, float& outValue);
	// Stamp writes that a recall could also produce — ONLY those may arm the
	// adoption guard (an event or host-animated slot would keep it permanently
	// armed and recall would silently never fire).
	void NoteRecallSetWrite(FFUInt32 ParamID);
	// Parse the selected preset XML into targets and start the glide. Values
	// only — the Tox File slot is deliberately never touched by a recall.
	void RecallSelectedPreset();
	void RescanPresets();
	// Advance the in-flight glide by one rendered frame. Called from
	// ProcessOpenGL under TEStateMutex; no-op unless EnablePresetControls.
	void StepPresetMorph();
	// The static (serialization/OSC) name of a pre-allocated slot — what Arena
	// writes into its preset XMLs ("Float1", "Toggle3", "Menu2", "Color1"...).
	std::string StaticSlotName(FFUInt32 ParamID) const;

	// TD-side [min,max] per FF_TYPE_STANDARD slot. The FFGL wire and the host
	// slider stay at the 0-1 prototype range (there is no FFGL range-change
	// event, so the host can never learn a different one); ParameterMapFloat
	// holds real TD values and these ranges convert at the Get/Set boundary.
	// The range is widened to include the initial value so out-of-range
	// defaults (e.g. an unranged TD float at 145) survive the round trip.
	std::unordered_map<FFUInt32, std::pair<double, double>> ParameterRanges;
	double NormalizeToHost(FFUInt32 paramID, double realValue);
	double DenormalizeFromHost(FFUInt32 paramID, double hostValue);

	// Host values that arrived while no tox was enumerated. Resolume preset
	// recall sends the Tox File path first (starting an async load) and then
	// every other slot's value immediately after; with nothing enumerated those
	// sets used to be dropped, so a preset recalled the tox but none of its
	// values. They are stashed RAW (FF_TYPE_STANDARD floats still 0-1
	// normalized — ParameterRanges does not exist yet) and applied at the end
	// of GetAllParameters in place of the tox's saved state. Pulse-family
	// slots are never stashed: a recalled preset must not fire events.
	std::unordered_map<FFUInt32, float> PendingFloatValues;
	std::unordered_map<FFUInt32, std::string> PendingTextValues;
	bool IsStashableSlot(FFUInt32 ParamID) const;
	void ApplyPendingHostValues();
	char DisplayBuffer[16] = { 0 };

	//Operator link identifiers (input is only set by FX-style toxes)
	std::string InputOpName;
	std::string OutputOpName;

	// TD->host state echo (#28 second half). TE input-link values are
	// host-authoritative, so comp-internal par changes are invisible on input
	// links — but TE DOES fire ValueChange for outputs. Convention: the tox
	// exposes its par state via an Out CHOP (Par CHOP: channels named like the
	// par components) and/or an Out DAT (Par DAT: name/value rows). These map
	// TD par/channel names ("Rgbar", "Float") to FFGL slots, and menu tokens
	// to option indices.
	std::unordered_map<std::string, FFUInt32> EchoNameToParamID;
	std::unordered_map<FFUInt32, std::vector<std::string>> MenuTokens;
	// Frame stamp of each slot's last host-push. Echo values for a slot are
	// ignored inside a short settling window after its push: out-link events
	// cooked BEFORE the push land after it and would revert the host's set
	// (observed live with a menu that was part of its own echo set).
	std::unordered_map<FFUInt32, uint64_t> LastPushFrame;
	static constexpr uint64_t EchoSettleFrames = 30;
	std::string EchoChopIdentifier;
	std::string EchoDatIdentifier;
	void ApplyEchoValue(FFUInt32 ParamID, double numeric, const char* text);
	void HandleEchoChop();
	void HandleEchoDat();

	int OutputWidth = 0;
	int OutputHeight = 0;

	std::string FilePath;

	ffglex::FFGLShader shader;  //!< Utility to help us compile and link some shaders into a program.
	ffglex::FFGLScreenQuad quad;//!< Utility to help us render a full screen quad.

	static void eventCallbackStatic(TEInstance* instance, TEEvent event, TEResult result, int64_t start_time_value, int32_t start_time_scale, int64_t end_time_value, int32_t end_time_scale, void* info);
	static void linkCallbackStatic(TEInstance* instance, TELinkEvent event, const char* identifier, void* info);
	static void statisticsCallbackStatic(TEInstance* instance, const struct TEInstanceStatistics* statistics, void* info);
};
