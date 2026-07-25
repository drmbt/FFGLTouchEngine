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
#include <map>
#include <mutex>
#include <string>
#include "TouchEngine/TouchObject.h"

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

	virtual void eventCallback(TEEvent event, TEResult result, int64_t start_time_value, int32_t start_time_scale, int64_t end_time_value, int32_t end_time_scale);
	virtual void linkCallback(TELinkEvent event, const char* identifier);

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

	// TD-side [min,max] per FF_TYPE_STANDARD slot. The FFGL wire and the host
	// slider stay at the 0-1 prototype range (there is no FFGL range-change
	// event, so the host can never learn a different one); ParameterMapFloat
	// holds real TD values and these ranges convert at the Get/Set boundary.
	// The range is widened to include the initial value so out-of-range
	// defaults (e.g. an unranged TD float at 145) survive the round trip.
	std::unordered_map<FFUInt32, std::pair<double, double>> ParameterRanges;
	double NormalizeToHost(FFUInt32 paramID, double realValue);
	double DenormalizeFromHost(FFUInt32 paramID, double hostValue);
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
};
