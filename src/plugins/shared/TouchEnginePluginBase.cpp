#include "TouchEnginePluginBase.h"
#include "TouchEngine/TEFloatBuffer.h"
#include "TouchEngine/TETable.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

FFResult FailAndLog(std::string message)
{
	FFGLLog::LogToHost(message.c_str());
	return FF_FAIL;
}

std::string GetSeverityString(TESeverity severity) {
	switch (severity) {
	case TESeverityWarning:
		return "Warning";
	case TESeverityError:
		return "Error";
	default:
		return "Unknown";
	}

}

// Log (but do not abort enumeration) when a single link cannot be read. Keeping
// the walk alive is the whole point of the enumeration-robustness work: a bad or
// unknown link must skip itself, never drop every parameter that follows it.
static void LogLinkSkip(const char* identifier, const char* reason) {
	std::string msg = std::string("FFGLTouchEngine: skipping parameter '") +
		(identifier ? identifier : "?") + "' — " + reason;
	FFGLLog::LogToHost(msg.c_str());
}

// TouchEngine treats a file-system link named "TouchEngine" beside the tox as a
// deliberate engine pin, and that pin overrides the preferred-engine path. When
// it cannot be used TE refuses the load with TEResultTouchEngineBadPath, whose
// description names neither the file nor the directory — so the failure reads
// as "this tox is broken" when in fact every tox in that folder will fail.
//
// The case that actually bites: a macOS symlink pin travelling to Windows
// through git or cloud sync, where it materialises as a small plain file
// holding the POSIX target path. Point straight at it.
static void LogEnginePinDiagnostic(const std::string& toxPath) {
	namespace fs = std::filesystem;
	std::error_code ec;

	fs::path directory = fs::path(toxPath).parent_path();
	if (directory.empty()) {
		return;
	}

	for (const char* pinName : { "TouchEngine", "TouchEngine.lnk" }) {
		fs::path pin = directory / pinName;
		if (!fs::exists(pin, ec)) {
			continue;
		}

		std::string msg = std::string("FFGLTouchEngine: '") + pin.string() +
			"' is being treated as an engine pin and could not be used — this is what "
			"failed the load, not the tox. Every tox in that folder will fail while it "
			"is there.";

		// A tiny regular file whose contents look like a path is the synced-symlink
		// case; quoting it makes the diagnosis unambiguous.
		if (fs::is_regular_file(pin, ec)) {
			auto size = fs::file_size(pin, ec);
			if (!ec && size > 0 && size < 512) {
				std::ifstream in(pin, std::ios::binary);
				std::string contents((std::istreambuf_iterator<char>(in)),
					std::istreambuf_iterator<char>());
				if (contents.find('/') != std::string::npos ||
					contents.find('\\') != std::string::npos) {
					msg += " It is a plain file containing '" + contents +
						"', i.e. a symlink that did not survive the trip to this machine. "
						"Remove it, or replace it with a real link, to load from this folder.";
				}
			}
		}

		FFGLLog::LogToHost(msg.c_str());
		return;
	}
}

// Spout sender names must be unique per plugin instance AND across processes:
// two Arena instances, or two clips in one Arena, otherwise fight over the same
// shared texture. rand() could not give that — it draws from a global sequence
// that FFGLTouchEngine never seeded (so every process produced the identical
// name) and that FFGLTouchEngineFX re-seeded from time(0) in its constructor
// (so two Arenas started in the same second matched, and an FX constructed
// between two generators could reset the sequence under them).
std::string GenerateRandomString(size_t length) {
	static const char charset[] =
		"0123456789"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz";
	// -1 for the terminating NUL: the distribution is inclusive, so this spans
	// exactly the 62 characters.
	static constexpr size_t charset_size = sizeof(charset) - 1;

	// thread_local: enumeration runs on the TE callback thread while the host
	// may construct plugins on another.
	thread_local std::mt19937 engine{ std::random_device{}() };
	std::uniform_int_distribution<size_t> pick(0, charset_size - 1);

	std::string str(length, 0);
	std::generate_n(str.begin(), length, [&]() { return charset[pick(engine)]; });
	return str;
}

#ifdef _WIN32
DXGI_FORMAT GlToDXFromat(GLint format) {

	switch (format) {
	case GL_RGBA8:
		return DXGI_FORMAT_B8G8R8A8_UNORM;

	case GL_RGB8:
		return DXGI_FORMAT_R8G8B8A8_UNORM;

	case GL_RGBA16:
		return DXGI_FORMAT_R16G16B16A16_UNORM;



	default:
		auto s = "Unsupported Format:: " + std::to_string(format);
		FFGLLog::LogToHost(s.c_str());
		// Falling off the end here was undefined behaviour: an unsupported
		// format returned whatever happened to be in the return register, and
		// that value went on to describe a D3D texture. B8G8R8A8_UNORM is the
		// format this plugin assumes everywhere else, so it is the safe guess.
		return DXGI_FORMAT_B8G8R8A8_UNORM;
	}
}
#endif

GLenum GetGlType(GLint format) {
	switch (format) {
	case GL_UNSIGNED_BYTE:
		return GL_RGBA;
	case GL_RGBA16:
		return GL_UNSIGNED_SHORT;
	default:
		// Same undefined-behaviour fall-through as above. GL_RGBA matches the
		// 8-bit path the rest of the plugin is built around.
		return GL_RGBA;
	}
}

#ifdef _WIN32
GLenum GetGlType(DXGI_FORMAT format) {
	switch (format) {
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		return GL_UNSIGNED_BYTE;
	case DXGI_FORMAT_R16G16B16A16_UNORM:
		return GL_UNSIGNED_SHORT;
	case DXGI_FORMAT_R32G32B32A32_FLOAT:
		return GL_FLOAT;
	default:
		return GL_UNSIGNED_BYTE;
	}
}
#endif

FFGLTouchEnginePluginBase::FFGLTouchEnginePluginBase()
	: CFFGLPlugin(),
	isTouchEngineLoaded(false),
	isTouchEngineReady(false),
	isGraphicsContextLoaded(false),
	isTouchFrameBusy(false),
	isBeingDestroyed(false)
{
	// Parameters
	SetParamInfof(0, "Tox File", FF_TYPE_FILE);
	SetParamInfof(1, "Reload", FF_TYPE_EVENT);
	SetParamInfof(2, "Unload", FF_TYPE_EVENT);
	SetParamInfof(3, "Clear Instance", FF_TYPE_EVENT);
	// Sits with the other always-present controls, directly beneath Clear
	// Instance and above every tox-driven slot, so the diagnostics read as part
	// of the plugin's own header rather than trailing the comp's parameters.
	//
	// Safe to place here even though it shifts every pre-allocated family up by
	// one index: Resolume serialises FFGL parameters BY NAME
	// (<Param name="Float1" .../> in the .avc), and OSC/REST addresses are
	// likewise name-derived, so no saved composition or map depends on these
	// indices. Verified against a real saved composition.
	LogParamID = 4;
	SetParamInfof(LogParamID, "Log", FF_TYPE_TEXT);

	// On by default: for a timecode-driven set most effects fire once for one
	// track, and holding ~1.4 GB per clip for the rest of the night is the
	// wrong trade. Turn it off for anything re-fired often enough that the
	// reload cost would be felt.
	// Name is 15 chars on purpose: FFGL's ParameterInfoStruct::Name is char[16],
	// so "Release When Idle" arrived in the host truncated to "Release When Idl".
	ReleaseIdleParamID = 5;
	SetParamInfo(ReleaseIdleParamID, "Release On Idle", FF_TYPE_BOOLEAN, true);

	// Per-clip, because one global value cannot fit both a workhorse effect
	// that gets cut back to and a one-shot fired once for a track. Seconds of
	// no rendering before the engine is handed back.
	IdleSecondsParamID = 6;
	// FF_TYPE_INTEGER, not FF_TYPE_STANDARD: SetParamInfo hard-clamps a
	// STANDARD default into [0,1] before any range is declared, so a default of
	// 20 silently became 1 and every clip released after one second. Integer
	// defaults are passed through untouched — and whole seconds is the right
	// granularity anyway.
	SetParamInfo(IdleSecondsParamID, "Idle Seconds", FF_TYPE_INTEGER,
		static_cast<float>(IdleSecondsDefault));
	SetParamRange(IdleSecondsParamID, 0.0f, static_cast<float>(IdleSecondsMax));
	IdleReleaseSeconds = IdleSecondsDefault;

	//This is the starting point for the parameters and is equal to the number of parameters above.
	OffsetParamsByType = 7;

	MaxParamsByType = 40;
}

FFGLTouchEnginePluginBase::~FFGLTouchEnginePluginBase()
{
	// Mark as destroying so event callbacks are ignored
	isBeingDestroyed = true;

	// Before anything else: the watchdog touches instance/flags/log state, all
	// of which are about to go away. ReleaseEngineForIdle is deliberately
	// non-virtual and GL-free, so stopping the thread here — after the derived
	// destructor has already run — is safe.
	StopIdleWatchdog();

	if (instance != nullptr) {
		if (isTouchEngineLoaded) {
			isTouchEngineLoaded = false;
			isTouchEngineReady = false;
			TEInstanceSuspend(instance);
			TEInstanceUnload(instance);
		}
		instance.reset();
	}
#ifdef __APPLE__
	MetalContext.reset();
	MetalCommandQueue = nil;
	MetalDevice = nil;
#endif
}

FFResult FFGLTouchEnginePluginBase::InitializeDevice()
{
#ifdef _WIN32
	// Create D3D11 device
	HRESULT hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		D3DDevice.GetAddressOf(),
		nullptr,
		nullptr
	);
	if (FAILED(hr)) {
		return FailAndLog("Failed to create D3D11 device");
	}
#endif
#ifdef __APPLE__
	if (MetalDevice == nil) {
		MetalDevice = MTLCreateSystemDefaultDevice();
		if (MetalDevice == nil) {
			return FailAndLog("Failed to create Metal device");
		}
		MetalCommandQueue = [MetalDevice newCommandQueue];
		if (MetalCommandQueue == nil) {
			return FailAndLog("Failed to create Metal command queue");
		}
	}
#endif

	return FF_SUCCESS;
}

FFResult FFGLTouchEnginePluginBase::InitializeShader(const std::string& vertexShaderCode, const std::string& fragmentShaderCode)
{
	if (!shader.Compile(vertexShaderCode, fragmentShaderCode)) {
		DeInitGL();
		return FailAndLog("Failed to compile shader");

	}

	// Initialize the quad
	if (!quad.Initialise()) {
		DeInitGL();
		return FailAndLog("Failed to initialize quad");
	}

	return FF_SUCCESS;
}

FFResult FFGLTouchEnginePluginBase::DeInitGL()
{
	return FF_SUCCESS;
}

void FFGLTouchEnginePluginBase::InitializeGlTexture(GLuint& texture, uint16_t width, uint16_t height, GLenum type) {
	if (texture != 0) {
		glDeleteTextures(1, &texture);
		texture = 0;
	}

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, type, NULL);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);
}

bool FFGLTouchEnginePluginBase::LoadTEGraphicsContext(bool reload) {
	if (isGraphicsContextLoaded && !reload) {
		return true;
	}

	if (instance == nullptr) {
		return false;
	}


	// Load the TouchEngine graphics context

#ifdef _WIN32
	if (D3DDevice == nullptr) {
		FFGLLog::LogToHost("D3D11 Device Not Available, You Probably Failed Somewhere...In Your Life");
	}

	TEResult result = TED3D11ContextCreate(D3DDevice.Get(), D3DContext.take());
	if (result != TEResultSuccess) {
		return false;
	}

	result = TEInstanceAssociateGraphicsContext(instance, D3DContext);
	if (result != TEResultSuccess) {
		return false;
	}
	isGraphicsContextLoaded = true;
#endif
#ifdef __APPLE__
	if (MetalDevice == nil) {
		FFGLLog::LogToHost("Metal Device Not Available");
		return false;
	}

	TEResult result = TEMetalContextCreate(MetalDevice, MetalContext.take());
	if (result != TEResultSuccess) {
		FFGLLog::LogToHost("Failed to create TEMetalContext");
		return false;
	}

	result = TEInstanceAssociateGraphicsContext(instance, MetalContext);
	if (result != TEResultSuccess) {
		FFGLLog::LogToHost("Failed to associate Metal graphics context");
		return false;
	}
	isGraphicsContextLoaded = true;
#endif
	return isGraphicsContextLoaded;
}

bool FFGLTouchEnginePluginBase::LoadTEFile()
{
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	// Load the tox file into the TouchEngine
	// 1. Create a TouchEngine object


	if (instance == nullptr) {
		return false;
	}

	// No tox assigned yet (fresh instance): nothing to load, and attempting it
	// would log a spurious "TEInstanceLoad failed for ''".
	if (FilePath.empty()) {
		return false;
	}

	if (isLoadPending) {
		// Queue instead of dropping: replayed from the in-flight load's DidLoad.
		FFGLLog::LogToHost("FFGLTouchEngine: load already in progress — queueing reload");
		isReloadQueued = true;
		return false;
	}

	isTouchEngineReady = false;

	// Time the load: TE load latency is the number most worth knowing before a
	// set, and it is otherwise invisible. Stamped here, reported at DidLoad.
	LoadStartTime = std::chrono::steady_clock::now();
	LoadTimerRunning = true;
	{
		// Just the filename — the full path is already on the Tox File slot.
		size_t slash = FilePath.find_last_of("/\\");
		SetLogStatus("loading " + (slash == std::string::npos ? FilePath : FilePath.substr(slash + 1)) + "...");
	}

	// 2. Load the tox file into the TouchEngine
	TEResult result = TEInstanceConfigure(instance, FilePath.c_str(), TETimeExternal);
	if (result != TEResultSuccess) {
		const char* desc = TEResultGetDescription(result);
		std::string msg = std::string("FFGLTouchEngine: TEInstanceConfigure failed for '") +
			FilePath + "' — " + (desc ? desc : "unknown error");
		FFGLLog::LogToHost(msg.c_str());
		LoadTimerRunning = false;
		SetLogStatus(std::string("ERROR configure: ") + (desc ? desc : "unknown error"));
		if (result == TEResultTouchEngineBadPath || result == TEResultTouchEngineNotFound) {
			LogEnginePinDiagnostic(FilePath);
			SetLogStatus("ERROR: a 'TouchEngine' file next to the tox is an unusable engine pin — remove it");
		}
		return false;
	}

	result = TEInstanceSetFrameRate(instance, 60, 1);

	if (result != TEResultSuccess) {
		const char* desc = TEResultGetDescription(result);
		std::string msg = std::string("FFGLTouchEngine: TEInstanceSetFrameRate failed — ") +
			(desc ? desc : "unknown error");
		FFGLLog::LogToHost(msg.c_str());
		LoadTimerRunning = false;
		SetLogStatus(std::string("ERROR frame rate: ") + (desc ? desc : "unknown error"));
		return false;
	}


	result = TEInstanceLoad(instance);
	if (result != TEResultSuccess) {
		const char* desc = TEResultGetDescription(result);
		std::string msg = std::string("FFGLTouchEngine: TEInstanceLoad failed for '") +
			FilePath + "' — " + (desc ? desc : "unknown error") +
			". The tox may have been authored in a newer TouchDesigner build than this engine.";
		FFGLLog::LogToHost(msg.c_str());
		LoadTimerRunning = false;
		SetLogStatus(std::string("ERROR load: ") + (desc ? desc : "unknown error") +
			" (tox may be from a newer TD build than this engine)");
		return false;
	}

	isLoadPending = true;

	return true;
}

// Newest installed TouchDesigner, per platform. Left to its own devices TE can
// select an old install whose engine exposes only texture output links (no
// CHOP/DAT outputs — silently breaking the par echo channel), so we steer it to
// the newest build. A TouchEngine file-system link next to the tox still
// overrides this (deliberate pinning).
//
// TEInstanceSetPreferredEnginePath wants the .app on macOS and the installation
// DIRECTORY on Windows.

#ifdef __APPLE__
// Build number comes straight off the bundle name ("TouchDesigner.33070.app").
static std::string FindNewestTouchDesignerInstall() {
	NSFileManager* fm = [NSFileManager defaultManager];
	NSArray<NSString*>* entries = [fm contentsOfDirectoryAtPath:@"/Applications" error:nil];
	long bestBuild = -1;
	NSString* bestPath = nil;
	for (NSString* entry in entries) {
		if (![entry hasPrefix:@"TouchDesigner"] || ![entry hasSuffix:@".app"]) {
			continue;
		}
		NSArray<NSString*>* parts = [entry componentsSeparatedByString:@"."];
		long build = 0;
		for (NSString* part in parts) {
			long v = [part integerValue];
			if (v > build) build = v;
		}
		if (build > bestBuild) {
			bestBuild = build;
			bestPath = [@"/Applications/" stringByAppendingString:entry];
		}
	}
	return bestPath != nil ? std::string([bestPath UTF8String]) : std::string();
}
#endif

#ifdef _WIN32
#pragma comment(lib, "version.lib")

// Comparable build key for one install, read from bin\TouchDesigner.exe's
// version resource (ProductVersion is major.minor.year.build — e.g.
// 0.99.2025.33070). The DIRECTORY NAME is not a usable source on Windows: the
// newest install is normally the unsuffixed "TouchDesigner" folder, which
// carries no build number at all, while older ones are "TouchDesigner.2025.32820".
// Returns 0 when the path is not a TouchDesigner install.
static unsigned long long TouchDesignerBuildKey(const std::wstring& installDir) {
	std::wstring exe = installDir + L"\\bin\\TouchDesigner.exe";
	DWORD ignored = 0;
	DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
	if (size == 0) {
		return 0;
	}
	std::vector<BYTE> buffer(size);
	if (!GetFileVersionInfoW(exe.c_str(), 0, size, buffer.data())) {
		return 0;
	}
	VS_FIXEDFILEINFO* info = nullptr;
	UINT infoLen = 0;
	if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoLen) || info == nullptr) {
		return 0;
	}
	// major/minor are identical across builds, so this orders by year then build.
	return (static_cast<unsigned long long>(info->dwProductVersionMS) << 32) | info->dwProductVersionLS;
}

static void AddInstallCandidate(std::vector<std::wstring>& candidates, const std::wstring& dir) {
	if (dir.empty()) {
		return;
	}
	for (const std::wstring& existing : candidates) {
		if (_wcsicmp(existing.c_str(), dir.c_str()) == 0) {
			return;
		}
	}
	candidates.push_back(dir);
}

static std::string FindNewestTouchDesignerInstall() {
	std::vector<std::wstring> candidates;

	// Derivative records every install directory under Path/Path_<n>. This is
	// also where TE itself looks, so it covers non-default install locations.
	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Derivative\\TouchDesigner", 0,
			KEY_READ, &key) == ERROR_SUCCESS) {
		for (DWORD i = 0;; ++i) {
			wchar_t name[256] = {};
			wchar_t value[MAX_PATH + 1] = {};
			DWORD nameLen = static_cast<DWORD>(std::size(name));
			DWORD valueBytes = static_cast<DWORD>(sizeof(value) - sizeof(wchar_t));
			DWORD type = 0;
			LSTATUS status = RegEnumValueW(key, i, name, &nameLen, nullptr, &type,
				reinterpret_cast<LPBYTE>(value), &valueBytes);
			if (status == ERROR_NO_MORE_ITEMS) {
				break;
			}
			// A single unreadable value (e.g. one too long for the buffer) must
			// not abandon the rest of the enumeration.
			if (status != ERROR_SUCCESS || type != REG_SZ) {
				continue;
			}
			if (_wcsnicmp(name, L"Path", 4) == 0) {
				AddInstallCandidate(candidates, value);
			}
		}
		RegCloseKey(key);
	}

	// Default install root, in case the registry is missing or stale.
	wchar_t programFiles[MAX_PATH] = {};
	DWORD written = GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH);
	if (written > 0 && written < MAX_PATH) {
		std::wstring root = std::wstring(programFiles) + L"\\Derivative\\";
		WIN32_FIND_DATAW found = {};
		HANDLE search = FindFirstFileW((root + L"TouchDesigner*").c_str(), &found);
		if (search != INVALID_HANDLE_VALUE) {
			do {
				if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
					AddInstallCandidate(candidates, root + found.cFileName);
				}
			} while (FindNextFileW(search, &found));
			FindClose(search);
		}
	}

	unsigned long long bestKey = 0;
	std::wstring best;
	for (const std::wstring& dir : candidates) {
		unsigned long long buildKey = TouchDesignerBuildKey(dir);
		if (buildKey > bestKey) {
			bestKey = buildKey;
			best = dir;
		}
	}
	if (best.empty()) {
		return std::string();
	}

	int bytes = WideCharToMultiByte(CP_UTF8, 0, best.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (bytes <= 1) {
		return std::string();
	}
	std::string utf8(static_cast<size_t>(bytes) - 1, '\0');
	WideCharToMultiByte(CP_UTF8, 0, best.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);
	return utf8;
}
#endif

void FFGLTouchEnginePluginBase::LoadTouchEngine() {

	if (instance == nullptr) {

		FFGLLog::LogToHost("Loading TouchEngine");
		TEResult result = TEInstanceCreate(eventCallbackStatic, linkCallbackStatic, this, instance.take());
		if (result != TEResultSuccess) {
			FFGLLog::LogToHost("Failed to create TouchEngine instance");
			instance.reset();
			return;
		}

		// Statistics are pushed by TE on its own cadence, so this costs nothing
		// per frame on our side. Registering unconditionally: the Log slot is
		// the only consumer and it is cheap to keep current.
		TEInstanceSetStatisticsCallback(instance, statisticsCallbackStatic);

		std::string newestTD = FindNewestTouchDesignerInstall();
		if (!newestTD.empty()) {
			result = TEInstanceSetPreferredEnginePath(instance, newestTD.c_str());
			if (result == TEResultSuccess) {
				FFGLLog::LogToHost((std::string("FFGLTouchEngine: preferred engine: ") + newestTD +
					" (a TouchEngine link next to the tox overrides this)").c_str());
			}
			else {
				FFGLLog::LogToHost((std::string("FFGLTouchEngine: could not prefer engine ") + newestTD +
					": " + TEResultGetDescription(result)).c_str());
			}
		}
		else {
			FFGLLog::LogToHost("FFGLTouchEngine: no TouchDesigner install found; "
				"leaving engine selection to TouchEngine");
		}

	}

}

// Effective TD-side range for a slot: the UI range widened to include the
// current value (unranged TD floats report a 0-1 UI hint while holding values
// far outside it), degenerate ranges opened up so normalization never divides
// by zero.
static std::pair<double, double> EffectiveRange(double uiMin, double uiMax, double value) {
	double lo = std::min(uiMin, value);
	double hi = std::max(uiMax, value);
	if (hi - lo < 1e-9) {
		hi = lo + 1.0;
	}
	return { lo, hi };
}

double FFGLTouchEnginePluginBase::NormalizeToHost(FFUInt32 paramID, double realValue) {
	auto it = ParameterRanges.find(paramID);
	if (it == ParameterRanges.end()) {
		return realValue;
	}
	return (realValue - it->second.first) / (it->second.second - it->second.first);
}

double FFGLTouchEnginePluginBase::DenormalizeFromHost(FFUInt32 paramID, double hostValue) {
	auto it = ParameterRanges.find(paramID);
	if (it == ParameterRanges.end()) {
		return hostValue;
	}
	return it->second.first + hostValue * (it->second.second - it->second.first);
}

FFResult FFGLTouchEnginePluginBase::SetFloatParameter(unsigned int dwIndex, float value) {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);

	if (dwIndex == 1 && value == 1) {
		LoadTouchEngine();
		LoadTEFile();
		return FF_SUCCESS;
	}

	if (dwIndex == 2 && value == 1) {
		if (isTouchEngineLoaded) {
			TEInstanceSuspend(instance);
			TEInstanceUnload(instance);
		}
		// Unload keeps the instance alive (unlike Clear, which resets it and is
		// therefore covered by ProcessOpenGL's `instance == nullptr` guard), so
		// these flags are the only thing between the render thread and a
		// suspended, unloaded instance. ResetBaseParameters() empties the
		// parameter maps but does not touch them, so the per-frame TE section
		// kept running after an Unload.
		isTouchEngineLoaded = false;
		isTouchEngineReady = false;
		// An Unload can land while a load is still in flight, in which case
		// DidLoad may never arrive to clear these. Left set, isLoadPending would
		// queue every later load behind one that can no longer complete.
		isLoadPending = false;
		isReloadQueued = false;
		ResetBaseParameters();
		return FF_SUCCESS;
	}

	if (dwIndex == 3 && value == 1) {
		isTouchEngineLoaded = false;
		isTouchEngineReady = false;
		isLoadPending = false;
		isReloadQueued = false;
		// A manual Clear is a full reset, not an idle release — do not let the
		// render loop resurrect the tox behind the user's back.
		EngineReleasedIdle = false;
		ResetBaseParameters();
		ClearTouchInstance();
		SetLogStatus("cleared");
		return FF_SUCCESS;
	}

	if (ReleaseIdleParamID != 0 && dwIndex == ReleaseIdleParamID) {
		ReleaseWhenIdle = (value != 0.0f);
		// Turning it on mid-set should not strand an already-idle clip holding
		// an engine, so restart the idle clock rather than releasing instantly.
		if (ReleaseWhenIdle) {
			NoteRendered();
		}
		return FF_SUCCESS;
	}

	if (IdleSecondsParamID != 0 && dwIndex == IdleSecondsParamID) {
		// The host sends the real value for a slot with a declared range.
		// Clamp anyway — a stray OSC message should not disable the feature or
		// park an engine for an hour.
		double seconds = static_cast<double>(value);
		if (seconds < 0.0) seconds = 0.0;
		if (seconds > IdleSecondsMax) seconds = IdleSecondsMax;
		IdleReleaseSeconds = seconds;
		// Shortening it should not retroactively release a clip that has been
		// sitting idle under the old, longer value.
		NoteRendered();
		return FF_SUCCESS;
	}

	if (!isTouchEngineLoaded || !isTouchEngineReady) {
		return FF_SUCCESS;
	}

	if (ActiveParams.find(dwIndex) == ActiveParams.end()) {
		return FF_SUCCESS;
	}

	FFUInt32 type = ParameterMapType[dwIndex];


	if (type == FF_TYPE_INTEGER) {
		ParameterMapInt[dwIndex] = static_cast<int32_t>(value);
		DirtyParams.insert(dwIndex);
		return FF_SUCCESS;
	}

	if (type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT) {
		ParameterMapBool[dwIndex] = value;
		DirtyParams.insert(dwIndex);
		return FF_SUCCESS;
	}

	if (type == FF_TYPE_OPTION) {
		ParameterMapInt[dwIndex] = static_cast<int32_t>(value);
		DirtyParams.insert(dwIndex);
		return FF_SUCCESS;
	}


	ParameterMapFloat[dwIndex] = DenormalizeFromHost(dwIndex, value);
	DirtyParams.insert(dwIndex);

	return FF_SUCCESS;
}

FFResult FFGLTouchEnginePluginBase::SetTextParameter(unsigned int dwIndex, const char* value) {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	switch (dwIndex) {
	case 0:
		// Open file dialog
		FilePath = std::string(value);
		LoadTEFile();
		return FF_SUCCESS;
	}

	// The Log slot is a readout. Swallow host writes rather than letting them
	// fall through and be mistaken for a tox parameter.
	if (LogParamID != 0 && dwIndex == LogParamID) {
		return FF_SUCCESS;
	}

	if (!isTouchEngineLoaded || !isTouchEngineReady) {
		return FF_SUCCESS;
	}

	if (ActiveParams.find(dwIndex) == ActiveParams.end()) {
		return FF_SUCCESS;
	}
	ParameterMapString[dwIndex] = value;
	DirtyParams.insert(dwIndex);
	return FF_SUCCESS;
}

float FFGLTouchEnginePluginBase::GetFloatParameter(unsigned int dwIndex) {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);

	if (dwIndex == 1) {
		return 0;
	}
	// Ahead of the ready guard: this toggle is ours, not the tox's, and the
	// host must read it back correctly with nothing loaded — including while
	// the engine is released, which is exactly the state it controls.
	if (ReleaseIdleParamID != 0 && dwIndex == ReleaseIdleParamID) {
		return ReleaseWhenIdle ? 1.0f : 0.0f;
	}
	if (IdleSecondsParamID != 0 && dwIndex == IdleSecondsParamID) {
		return static_cast<float>(IdleReleaseSeconds);
	}
	if (!isTouchEngineLoaded || !isTouchEngineReady) {
		return 0;

	}

	if (ActiveParams.find(dwIndex) == ActiveParams.end()) {
		return 0;
	}

	FFUInt32 type = ParameterMapType[dwIndex];

	if (type == FF_TYPE_INTEGER || type == FF_TYPE_OPTION) {
		return static_cast<float>(ParameterMapInt[dwIndex]);
	}

	if (type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT) {
		return ParameterMapBool[dwIndex];
	}


	return static_cast<float>(NormalizeToHost(dwIndex, ParameterMapFloat[dwIndex]));
}

char* FFGLTouchEnginePluginBase::GetParameterDisplay(unsigned int index) {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	// Show real TD-side values for remapped slots; the host's own readout would
	// otherwise print the normalized 0-1 wire value.
	if (ActiveParams.find(index) != ActiveParams.end()) {
		auto typeIt = ParameterMapType.find(index);
		if (typeIt != ParameterMapType.end() && typeIt->second == FF_TYPE_STANDARD) {
			snprintf(DisplayBuffer, sizeof(DisplayBuffer), "%.6g", ParameterMapFloat[index]);
			return DisplayBuffer;
		}
	}
	return CFFGLPlugin::GetParameterDisplay(index);
}

char* FFGLTouchEnginePluginBase::GetTextParameter(unsigned int dwIndex) {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	if (dwIndex == 0) {
		return (char*)FilePath.c_str();
	}

	// Ahead of the loaded/ready guard on purpose: the Log slot earns its keep
	// when a load has FAILED, which is exactly when neither flag is set.
	if (LogParamID != 0 && dwIndex == LogParamID) {
		return (char*)LogText.c_str();
	}

	if (!isTouchEngineLoaded || !isTouchEngineReady) {
		return nullptr;
	}

	if (ActiveParams.find(dwIndex) == ActiveParams.end()) {
		return nullptr;
	}

	return (char*)ParameterMapString[dwIndex].c_str();
}

void FFGLTouchEnginePluginBase::ConstructBaseParameters() {
	// Every slot gets a unique static name ("Float1", "Pulse3", "Color2R", ...).
	// Hosts derive OSC/REST addresses and event-button captions from this name
	// and there is no FFGL event to change it later, so duplicates here mean
	// colliding OSC addresses no display-name event can repair. TD labels are
	// applied on top per-slot via SetParamDisplayName during enumeration.
	for (uint32_t i = OffsetParamsByType; i < MaxParamsByType + OffsetParamsByType; i++) {
		SetParamInfof(i, (std::string("Float") + std::to_string(i - OffsetParamsByType + 1)).c_str(), FF_TYPE_STANDARD);
		SetParamVisibility(i, false, false);
	}


	for (uint32_t i = MaxParamsByType + OffsetParamsByType; i < (MaxParamsByType * 2) + OffsetParamsByType; i++) {
		SetParamInfof(i, (std::string("Int") + std::to_string(i - MaxParamsByType - OffsetParamsByType + 1)).c_str(), FF_TYPE_INTEGER);
		SetParamRange(i, -10000, 10000);
		SetParamVisibility(i, false, false);
	}

	for (uint32_t i = (MaxParamsByType * 2) + OffsetParamsByType; i < (MaxParamsByType * 3) + OffsetParamsByType; i++) {
		SetParamInfof(i, (std::string("Toggle") + std::to_string(i - (MaxParamsByType * 2) - OffsetParamsByType + 1)).c_str(), FF_TYPE_BOOLEAN);
		SetParamVisibility(i, false, false);
	}


	for (uint32_t i = (MaxParamsByType * 3) + OffsetParamsByType; i < (MaxParamsByType * 4) + OffsetParamsByType; i++) {
		SetParamInfof(i, (std::string("Text") + std::to_string(i - (MaxParamsByType * 3) - OffsetParamsByType + 1)).c_str(), FF_TYPE_TEXT);
		SetParamVisibility(i, false, false);
	}


	// Event slots get unique names like every other family: the host keys
	// OSC/REST addresses on the static name, and identical names collapse to a
	// single entry — only the FIRST event slot was reachable via OSC/API. The
	// cost is button captions reading "Pulse1"/"Pulse2" (the caption follows
	// the static name and FFGL has no rename event); the row label still shows
	// the TD name via the display-name mechanism.
	for (uint32_t i = (MaxParamsByType * 4) + OffsetParamsByType; i < (MaxParamsByType * 5) + OffsetParamsByType; i++) {
		SetParamInfof(i, (std::string("Pulse") + std::to_string(i - (MaxParamsByType * 4) - OffsetParamsByType + 1)).c_str(), FF_TYPE_EVENT);
		SetParamVisibility(i, false, false);
	}

	for (uint32_t i = (MaxParamsByType * 5) + OffsetParamsByType; i < (MaxParamsByType * 6) + OffsetParamsByType; i++) {
		SetOptionParamInfo(i, (std::string("Menu") + std::to_string(i - (MaxParamsByType * 5) - OffsetParamsByType + 1)).c_str(), 10, 0);
		SetParamVisibility(i, false, false);
	}

	// Pre-allocate color picker slots (groups of 4: R, G, B, A)
	// Each color uses 4 consecutive param slots
	uint32_t colorBase = (MaxParamsByType * 6) + OffsetParamsByType;
	for (uint32_t i = 0; i < MaxParamsByType; i += 4) {
		std::string colorName = std::string("Color") + std::to_string(i / 4 + 1);
		SetParamInfo(colorBase + i,     (colorName + "R").c_str(), FF_TYPE_RED,   0.0f);
		SetParamInfo(colorBase + i + 1, (colorName + "G").c_str(), FF_TYPE_GREEN, 0.0f);
		SetParamInfo(colorBase + i + 2, (colorName + "B").c_str(), FF_TYPE_BLUE,  0.0f);
		SetParamInfo(colorBase + i + 3, (colorName + "A").c_str(), FF_TYPE_ALPHA, 1.0f);
		SetParamVisibility(colorBase + i,     false, false);
		SetParamVisibility(colorBase + i + 1, false, false);
		SetParamVisibility(colorBase + i + 2, false, false);
		SetParamVisibility(colorBase + i + 3, false, false);
	}

	// Registered in the constructor at index 4 (see there). Kept visible
	// unconditionally, unlike every family above: a failed load is precisely
	// when it has something to say, and no tox parameters exist at that point.
	SetParamVisibility(LogParamID, true, false);
	SetParamVisibility(ReleaseIdleParamID, true, false);
	SetParamVisibility(IdleSecondsParamID, true, false);
	LogStatus = "idle — no tox loaded";
	RefreshLogText();
	StartIdleWatchdog();
}

// Composes the visible line from the last significant event plus whatever
// statistics TE has most recently delivered. Callers must hold TEStateMutex.
void FFGLTouchEnginePluginBase::RefreshLogText() {
	std::string composed = LogStatus;

	if (StatMemGPU >= 0 || StatMemCPU >= 0 || StatFPS >= 0.0 || StatCookMs >= 0.0) {
		char buffer[128];
		if (StatMemGPU >= 0 || StatMemCPU >= 0) {
			snprintf(buffer, sizeof(buffer), "  |  GPU %.0f MB  CPU %.0f MB",
				StatMemGPU >= 0 ? StatMemGPU / 1048576.0 : 0.0,
				StatMemCPU >= 0 ? StatMemCPU / 1048576.0 : 0.0);
			composed += buffer;
		}
		if (StatFPS >= 0.0) {
			snprintf(buffer, sizeof(buffer), "  %.1f fps", StatFPS);
			composed += buffer;
		}
		if (StatCookMs >= 0.0) {
			snprintf(buffer, sizeof(buffer), "  cook %.1f ms", StatCookMs);
			composed += buffer;
		}
		// Only mention drops when there are any — a permanent "0 dropped" is
		// noise, but the moment it moves it is the most important number here.
		if (StatFramesDropped > 0) {
			snprintf(buffer, sizeof(buffer), "  (%lld dropped)",
				static_cast<long long>(StatFramesDropped));
			composed += buffer;
		}
	}

	if (composed == LogText) {
		return;
	}
	LogText = composed;
	// Tell the host to re-query. Statistics arrive on TE's cadence (~1 Hz), and
	// lifecycle events are rare, so this is nowhere near a per-frame raise.
	if (LogParamID != 0) {
		RaiseParamEvent(LogParamID, FF_EVENT_FLAG_VALUE);
	}
}

unsigned int FFGLTouchEnginePluginBase::Connect() {
	// Arming a clip is the ONLY thing that brings a released engine back.
	//
	// Rendering deliberately does not: selecting a clip to preview it renders
	// continuously and is indistinguishable from playback at the frame level,
	// so a render-driven reload turned every preview into a 25-40s engine
	// start — and, for a clip left selected but not playing, into an endless
	// release/reload cycle. Observed exactly that.
	//
	// This also covers a manually Cleared plugin, not just an idle release:
	// firing the clip reloads it, while merely previewing or selecting leaves
	// it cleared.
	NoteRendered();
	ReloadIfEngineAbsent();
	return FF_SUCCESS;
}

unsigned int FFGLTouchEnginePluginBase::Disconnect() {
	// Resolume does NOT call this on eject (verified with an instrumented
	// build) — the idle watchdog is what actually detects deactivation. Kept
	// as a correct implementation for hosts that do call it.
	if (ReleaseWhenIdle) {
		ReleaseEngineForIdle();
	}
	return FF_SUCCESS;
}

void FFGLTouchEnginePluginBase::NoteRendered() {
	LastRenderTick.store(
		std::chrono::steady_clock::now().time_since_epoch().count(),
		std::memory_order_relaxed);
}

void FFGLTouchEnginePluginBase::StartIdleWatchdog() {
	if (WatchdogThread.joinable()) {
		return;
	}
	WatchdogStop.store(false, std::memory_order_relaxed);
	WatchdogThread = std::thread([this]() {
		while (!WatchdogStop.load(std::memory_order_relaxed)) {
			// Short sleep so shutdown is responsive; the idle test itself is
			// against IdleReleaseSeconds, not this interval.
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			if (WatchdogStop.load(std::memory_order_relaxed)) {
				return;
			}
			if (!ReleaseWhenIdle) {
				continue;
			}
			long long last = LastRenderTick.load(std::memory_order_relaxed);
			if (last == 0) {
				// Never rendered — nothing has been armed yet, so there is
				// nothing to reclaim and no idle period to measure.
				continue;
			}
			std::chrono::steady_clock::time_point lastRender{
				std::chrono::steady_clock::duration(last) };
			double idle = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - lastRender).count();
			// >= so an Idle Seconds of 0 means "release as soon as rendering
			// stops", honoured within one 500 ms tick.
			if (idle >= IdleReleaseSeconds) {
				ReleaseEngineForIdle();
			}
		}
	});
}

void FFGLTouchEnginePluginBase::StopIdleWatchdog() {
	WatchdogStop.store(true, std::memory_order_relaxed);
	if (WatchdogThread.joinable()) {
		WatchdogThread.join();
	}
}

void FFGLTouchEnginePluginBase::ReleaseEngineForIdle() {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	if (instance == nullptr || EngineReleasedIdle) {
		return;
	}

	// Deliberately narrow: suspend, unload, drop the instance. That is what
	// ends the engine process and returns the memory. The Spout interop, the
	// D3D/Metal context and the GL textures are left alone — the watchdog has
	// no GL context to tear them down on, and they are small compared to an
	// engine. Parameter maps are kept too, so values survive the round trip and
	// are re-pushed when the tox comes back.
	// Snapshot the values so the reload can put them back — enumeration will
	// otherwise reset every slot to the tox's defaults.
	RetainedFloat = ParameterMapFloat;
	RetainedInt = ParameterMapInt;
	RetainedBool = ParameterMapBool;
	RetainedString = ParameterMapString;
	RetainedValuesValid = true;

	if (isTouchEngineLoaded) {
		TEInstanceSuspend(instance);
		TEInstanceUnload(instance);
	}
	instance.reset();

	isTouchEngineLoaded = false;
	isTouchEngineReady = false;
	isGraphicsContextLoaded = false;
	isLoadPending = false;
	isReloadQueued = false;
	EngineReleasedIdle = true;

	// Match what Clear Instance does: drop the tox parameters as well as the
	// engine. Unload is the shallower operation — it keeps the instance alive,
	// which is why Reload after Unload is instant — and it is not what this is
	// for. Values were snapshotted above and go back on when the tox returns.
	ResetBaseParameters();
	SetLogStatus("engine released after idle — fires again on next trigger");
}

void FFGLTouchEnginePluginBase::ReloadIfEngineAbsent() {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	if (instance != nullptr || FilePath.empty() || isLoadPending) {
		return;
	}
	EngineReleasedIdle = false;
	// Async from here: LoadTEFile only configures, and the graphics context is
	// rebuilt in the DidLoad callback exactly as on a first load.
	LoadTouchEngine();
	LoadTEFile();
}

void FFGLTouchEnginePluginBase::SetLogStatus(const std::string& status) {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	LogStatus = status;
	RefreshLogText();
}

void FFGLTouchEnginePluginBase::statisticsCallbackStatic(TEInstance* instance,
	const struct TEInstanceStatistics* statistics, void* info) {
	static_cast<FFGLTouchEnginePluginBase*>(info)->statisticsCallback(statistics);
}

void FFGLTouchEnginePluginBase::statisticsCallback(const struct TEInstanceStatistics* statistics) {
	if (statistics == nullptr || isBeingDestroyed) {
		return;
	}
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);

	StatMemGPU = statistics->memUsedGPU;
	StatMemCPU = statistics->memUsedCPU;
	StatFramesDropped = statistics->framesDropped;

	// frameTimeCPU is CPU time SPENT on those frames, not elapsed wall time —
	// dividing by it yields throughput capacity (a cooked-in-1.5ms frame reads
	// as "655 fps"), which is not the rate anyone means by fps. Actual rate has
	// to come from wall-clock between deliveries.
	auto now = std::chrono::steady_clock::now();
	if (statistics->frames > 0) {
		if (StatsLastDelivery.time_since_epoch().count() != 0) {
			double elapsed = std::chrono::duration<double>(now - StatsLastDelivery).count();
			if (elapsed > 0.0) {
				StatFPS = static_cast<double>(statistics->frames) / elapsed;
			}
		}
		// Cook cost per frame is the useful companion number: it says how much
		// headroom there is, independent of how fast TE is being driven.
		if (statistics->frameTimeCPU > 0) {
			StatCookMs = (static_cast<double>(statistics->frameTimeCPU) / 1e6)
				/ static_cast<double>(statistics->frames);
		}
	}
	StatsLastDelivery = now;

	RefreshLogText();
}

void FFGLTouchEnginePluginBase::ResetBaseParameters() {
	for (auto& ParamID : ActiveParams) {
		SetParamVisibility(ParamID, false, true);
	}

	// Drop the statistics with the instance they described — leaving the last
	// memory/fps figures on screen after an unload reads as if it were still
	// running. The status line is deliberately NOT cleared here: if it holds an
	// error, that is the thing worth keeping visible.
	StatMemGPU = -1;
	StatMemCPU = -1;
	StatFPS = -1.0;
	StatCookMs = -1.0;
	StatFramesDropped = -1;
	StatsLastDelivery = {};
	RefreshLogText();

	hasVideoOutput = false;
	ActiveParams.clear();
	ActiveVectorParams.clear();
	VectorParameters.clear();
	ParameterMapFloat.clear();
	ParameterMapInt.clear();
	ParameterMapString.clear();
	ParameterMapBool.clear();
	ParameterRanges.clear();
	DirtyParams.clear();
	EchoNameToParamID.clear();
	MenuTokens.clear();
	LastPushFrame.clear();
	EchoChopIdentifier.clear();
	EchoDatIdentifier.clear();
	PulseParameters.clear();
	FloatParamCount = 0;
	IntParamCount = 0;
	BoolParamCount = 0;
	StringParamCount = 0;
	EventParamCount = 0;
	MenuParamCount = 0;
	ColorParamCount = 0;
	Parameters.clear();
}

void FFGLTouchEnginePluginBase::GetAllParameters() {
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	ResetBaseParameters();

	TouchObject<TEStringArray> groupLinkInfo;

	if (instance == nullptr) {
		return;
	}

	TEResult result = TEInstanceGetLinkGroups(instance, TEScopeInput, groupLinkInfo.take());

	if (result != TEResultSuccess) {
		const char* desc = TEResultGetDescription(result);
		std::string msg = std::string("FFGLTouchEngine: failed to enumerate input link groups — ") +
			(desc ? desc : "unknown error");
		FFGLLog::LogToHost(msg.c_str());
		return;
	}

	for (int i = 0; i < groupLinkInfo->count; i++) {
		TouchObject<TEStringArray> links;
		result = TEInstanceLinkGetChildren(instance, groupLinkInfo->strings[i], links.take());

		if (result != TEResultSuccess) {
			// Skip only this group; keep walking the rest so one bad group can't
			// drop every parameter that follows it.
			LogLinkSkip(groupLinkInfo->strings[i], "failed to read group children");
			continue;
		}

		for (int j = 0; j < links->count; j++) {
			TouchObject<TELinkInfo> linkInfo;
			result = TEInstanceLinkGetInfo(instance, links->strings[j], linkInfo.take());

			if (result != TEResultSuccess) {
				LogLinkSkip(links->strings[j], "failed to read link info");
				continue;
			}


			if (linkInfo->domain == TELinkDomainParameter) {

				if (ActiveParams.size() >= MaxParamsByType * 7) {
					FFGLLog::LogToHost("Too many parameters, skipping");
					continue;
				}

				if (linkInfo->type == TELinkTypeGroup) {
					CreateParametersFromGroup(linkInfo);
				} else {
					CreateIndividualParameter(linkInfo);
				}
			} else if (linkInfo->domain == TELinkDomainOperator) {
				HandleOperatorLink(linkInfo);
			}

		}
	}

	// Get the texture output if it exists
	result = TEInstanceGetLinkGroups(instance, TEScopeOutput, groupLinkInfo.take());

	if (result != TEResultSuccess) {
		return;
	}

	for (int i = 0; i < groupLinkInfo->count; i++) {
		TouchObject<TEStringArray> links;
		result = TEInstanceLinkGetChildren(instance, groupLinkInfo->strings[i], links.take());

		if (result != TEResultSuccess) {
			LogLinkSkip(groupLinkInfo->strings[i], "failed to read output group children");
			continue;
		}

		for (int j = 0; j < links->count; j++) {
			TouchObject<TELinkInfo> linkInfo;
			result = TEInstanceLinkGetInfo(instance, links->strings[j], linkInfo.take());

			if (result != TEResultSuccess) {
				LogLinkSkip(links->strings[j], "failed to read output link info");
				continue;
			}

			if (linkInfo->domain == TELinkDomainOperator) {
				if (linkInfo->type == TELinkTypeTexture) {
					if (!hasVideoOutput) {
						OutputOpName = linkInfo->identifier;
						hasVideoOutput = true;
					}
				} else if (linkInfo->type == TELinkTypeFloatBuffer) {
					// Par-state echo channel (Out CHOP fed by a Par CHOP)
					if (EchoChopIdentifier.empty()) {
						EchoChopIdentifier = linkInfo->identifier;
						FFGLLog::LogToHost((std::string("FFGLTouchEngine: par echo CHOP registered: ") + EchoChopIdentifier).c_str());
					}
				} else if (linkInfo->type == TELinkTypeStringData) {
					// Par-state echo channel (Out DAT fed by a Par DAT)
					if (EchoDatIdentifier.empty()) {
						EchoDatIdentifier = linkInfo->identifier;
						FFGLLog::LogToHost((std::string("FFGLTouchEngine: par echo DAT registered: ") + EchoDatIdentifier).c_str());
					}
				}
			}
		}

	}

	// An idle release is meant to be invisible apart from the reload delay, but
	// enumeration has just overwritten every slot with the tox's own defaults.
	// Put the pre-release values back and mark them dirty so the normal
	// dirty-only push sends them to TE, otherwise a clip that idled out and
	// came back would silently lose every parameter tweak — and because the
	// host re-QUERIES on the sweep below rather than pushing, Resolume would
	// adopt the defaults too, losing them from the composition as well.
	if (RetainedValuesValid) {
		RetainedValuesValid = false;
		for (auto& ParamID : ActiveParams) {
			bool restored = false;
			auto f = RetainedFloat.find(ParamID);
			if (f != RetainedFloat.end() && ParameterMapFloat.count(ParamID)) {
				ParameterMapFloat[ParamID] = f->second; restored = true;
			}
			auto i = RetainedInt.find(ParamID);
			if (i != RetainedInt.end() && ParameterMapInt.count(ParamID)) {
				ParameterMapInt[ParamID] = i->second; restored = true;
			}
			auto b = RetainedBool.find(ParamID);
			if (b != RetainedBool.end() && ParameterMapBool.count(ParamID)) {
				ParameterMapBool[ParamID] = b->second; restored = true;
			}
			auto s = RetainedString.find(ParamID);
			if (s != RetainedString.end() && ParameterMapString.count(ParamID)) {
				ParameterMapString[ParamID] = s->second; restored = true;
			}
			if (restored) {
				DirtyParams.insert(ParamID);
			}
		}
		RetainedFloat.clear();
		RetainedInt.clear();
		RetainedBool.clear();
		RetainedString.clear();
	}

	// Re-raise value events now that the walk is complete. Enumeration runs on
	// the TE callback thread while the host UI polls in parallel, so the host
	// can consume a slot's event and cache a stale display string before that
	// slot's registration finished; a final sweep forces a fresh query of every
	// active parameter.
	for (auto& ParamID : ActiveParams) {
		RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
	}
}

void FFGLTouchEnginePluginBase::CreateIndividualParameter(const TouchObject<TELinkInfo>& linkInfo) {

	// Robustness contract for this whole function: every early-out must SKIP only
	// the current link (log + return), never leave a half-registered parameter
	// behind. To guarantee that, each branch reads all TE values FIRST and only
	// registers the parameter (Parameters / ActiveParams / ParameterMap*) once the
	// reads have succeeded. A read failure therefore leaves no dangling slot for
	// PushParametersToTouchEngine to trip over.
	switch (linkInfo->type) {
	case TELinkTypeTexture:
	case TELinkTypeGroup:
	case TELinkTypeSeparator:
	{
		// Not user-facing scalar parameters — nothing to enumerate here.
		return;
	}

	case TELinkTypeDouble:
	{
		TEResult result;
		if (linkInfo->intent == TELinkIntentColorRGBA || linkInfo->intent == TELinkIntentPositionXYZW || linkInfo->intent == TELinkIntentSizeWH) {
			std::string Suffix;
			switch (linkInfo->intent) {
			case TELinkIntentColorRGBA:
				Suffix = "RGBA";
				break;
			case TELinkIntentPositionXYZW:
				Suffix = "XYZW";
				break;
			case TELinkIntentSizeWH:
				Suffix = "WH";
				break;
			}

			double value[4] = { 0, 0, 0, 0 };
			result = TEInstanceLinkGetDoubleValue(instance, linkInfo->identifier, TELinkValueCurrent, value, linkInfo->count);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read vector value");
				return;
			}

			double max[4] = { 0, 0, 0, 0 };
			result = TEInstanceLinkGetDoubleValue(instance, linkInfo->identifier, TELinkValueUIMaximum, max, linkInfo->count);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read vector max");
				return;
			}
			double min[4] = { 0, 0, 0, 0 };
			result = TEInstanceLinkGetDoubleValue(instance, linkInfo->identifier, TELinkValueUIMinimum, min, linkInfo->count);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read vector min");
				return;
			}

			VectorParameterInfo info;
			info.count = linkInfo->count;
			info.identifier = linkInfo->identifier;



			static const FFUInt32 colorTypes[] = { FF_TYPE_RED, FF_TYPE_GREEN, FF_TYPE_BLUE, FF_TYPE_ALPHA };

			// Align to start of a group of 4 so R/G/B/A land on the correct pre-allocated types
			uint32_t colorGroupBase = (ColorParamCount / 4) * 4;
			if (ColorParamCount % 4 != 0) {
				colorGroupBase = ColorParamCount; // already mid-group shouldn't happen, but advance
			}

			if (linkInfo->intent == TELinkIntentColorRGBA) {
				if (colorGroupBase + 4 > MaxParamsByType) {
					LogLinkSkip(linkInfo->identifier, "no free color slots");
					return;
				}
			} else if (FloatParamCount + linkInfo->count > MaxParamsByType) {
				LogLinkSkip(linkInfo->identifier, "no free float slots");
				return;
			}

			for (uint32_t i = 0; i < linkInfo->count; i++) {
				uint32_t ParamID;

				if (linkInfo->intent == TELinkIntentColorRGBA && i < 4) {
					// Use pre-allocated color picker slots (aligned to groups of 4)
					ParamID = (MaxParamsByType * 6) + OffsetParamsByType + colorGroupBase + i;
					ParameterMapType[ParamID] = colorTypes[i];
				} else {
					ParamID = OffsetParamsByType + FloatParamCount++;
					ParameterMapType[ParamID] = FF_TYPE_STANDARD;
				}

				Parameters.push_back(std::make_pair(std::string(linkInfo->identifier) + (char)0x03 + std::to_string(i), ParamID));
				ActiveParams.insert(ParamID);
				ActiveVectorParams.insert(ParamID);
				info.children[i] = ParamID;
				if (linkInfo->name != nullptr && i < Suffix.size()) {
					// TD names vector components par-name + lowercase suffix
					// ("Rgba" -> "Rgbar"), which is how Par CHOP channels and
					// Par DAT rows refer to them.
					EchoNameToParamID[std::string(linkInfo->name) + (char)std::tolower(Suffix[i])] = ParamID;
				}

				ParameterMapFloat[ParamID] = value[i];
				if (!(linkInfo->intent == TELinkIntentColorRGBA && i < 4)) {
					// Color slots keep the native 0-1 wire; everything else
					// remaps between the 0-1 prototype and the TD range.
					ParameterRanges[ParamID] = EffectiveRange(min[i], max[i], value[i]);
				}
				SetParamDisplayName(ParamID, linkInfo->label + std::string(".") + Suffix[i], true);
				RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
				SetParamVisibility(ParamID, true, true);

			}

			// Advance color counter by a full group of 4 to keep alignment
			if (linkInfo->intent == TELinkIntentColorRGBA) {
				ColorParamCount = colorGroupBase + 4;
			}

			VectorParameters.push_back(info);

			return;
		}

		// Scalar double — read value + range before registering.
		double value = 0;
		result = TEInstanceLinkGetDoubleValue(instance, linkInfo->identifier, TELinkValueCurrent, &value, 1);
		if (result != TEResultSuccess) {
			LogLinkSkip(linkInfo->identifier, "failed to read double value");
			return;
		}
		double max = 0;
		result = TEInstanceLinkGetDoubleValue(instance, linkInfo->identifier, TELinkValueUIMaximum, &max, 1);
		if (result != TEResultSuccess) {
			LogLinkSkip(linkInfo->identifier, "failed to read double max");
			return;
		}
		double min = 0;
		result = TEInstanceLinkGetDoubleValue(instance, linkInfo->identifier, TELinkValueUIMinimum, &min, 1);
		if (result != TEResultSuccess) {
			LogLinkSkip(linkInfo->identifier, "failed to read double min");
			return;
		}

		if (FloatParamCount >= MaxParamsByType) {
			LogLinkSkip(linkInfo->identifier, "no free float slots");
			return;
		}
		uint32_t ParamID = OffsetParamsByType + FloatParamCount++;
		Parameters.push_back(std::make_pair(linkInfo->identifier, ParamID));
		ActiveParams.insert(ParamID);
		if (linkInfo->name != nullptr) EchoNameToParamID[linkInfo->name] = ParamID;
		ParameterMapType[ParamID] = FF_TYPE_STANDARD;
		ParameterMapFloat[ParamID] = value;
		ParameterRanges[ParamID] = EffectiveRange(min, max, value);
		SetParamDisplayName(ParamID, linkInfo->label, true);
		RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
		SetParamVisibility(ParamID, true, true);

		break;

	}
	case TELinkTypeInt:
	{
		TEResult result;
		if (TEInstanceLinkHasChoices(instance, linkInfo->identifier)) {
			TouchObject<TEStringArray> labels;
			result = TEInstanceLinkGetChoiceLabels(instance, linkInfo->identifier, labels.take());
			if (result != TEResultSuccess && !labels) {
				LogLinkSkip(linkInfo->identifier, "failed to read choice labels");
				return;
			}

			int32_t value = 0;
			result = TEInstanceLinkGetIntValue(instance, linkInfo->identifier, TELinkValueCurrent, &value, 1);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read menu value");
				return;
			}

			if (MenuParamCount >= MaxParamsByType) {
				LogLinkSkip(linkInfo->identifier, "no free menu slots");
				return;
			}
			uint32_t ParamID = (MaxParamsByType * 5) + OffsetParamsByType + MenuParamCount++;

			std::vector<std::string> labelsVector;
			std::vector<float> valuesVector;

			for (int k = 0; k < labels->count; k++) {
				labelsVector.push_back(labels->strings[k]);
				valuesVector.push_back(static_cast<float>(k));
			}

			SetParamElements(ParamID, labelsVector, valuesVector, true);

			Parameters.push_back(std::make_pair(linkInfo->identifier, ParamID));
			ActiveParams.insert(ParamID);
			if (linkInfo->name != nullptr) EchoNameToParamID[linkInfo->name] = ParamID;
			{
				// Menu tokens (choice VALUES, not labels) let the DAT echo map a
				// token like "red" back to its option index.
				TouchObject<TEStringArray> tokens;
				if (TEInstanceLinkGetChoiceValues(instance, linkInfo->identifier, tokens.take()) == TEResultSuccess && tokens) {
					std::vector<std::string> tokenVector;
					for (int k = 0; k < tokens->count; k++) {
						tokenVector.push_back(tokens->strings[k]);
					}
					MenuTokens[ParamID] = tokenVector;
				}
			}
			SetParamDisplayName(ParamID, linkInfo->label, true);
			ParameterMapType[ParamID] = FF_TYPE_OPTION;
			ParameterMapInt[ParamID] = value;

			RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
			SetParamVisibility(ParamID, true, true);
			break;
		} else {
			int32_t value = 0;
			result = TEInstanceLinkGetIntValue(instance, linkInfo->identifier, TELinkValueCurrent, &value, 1);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read int value");
				return;
			}
			int32_t max = 0;
			result = TEInstanceLinkGetIntValue(instance, linkInfo->identifier, TELinkValueUIMaximum, &max, 1);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read int max");
				return;
			}
			int32_t min = 0;
			result = TEInstanceLinkGetIntValue(instance, linkInfo->identifier, TELinkValueUIMinimum, &min, 1);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read int min");
				return;
			}

			if (IntParamCount >= MaxParamsByType) {
				LogLinkSkip(linkInfo->identifier, "no free int slots");
				return;
			}
			uint32_t ParamID = MaxParamsByType + OffsetParamsByType + IntParamCount++;
			Parameters.push_back(std::make_pair(linkInfo->identifier, ParamID));
			ActiveParams.insert(ParamID);
			if (linkInfo->name != nullptr) EchoNameToParamID[linkInfo->name] = ParamID;
			ParameterMapType[ParamID] = FF_TYPE_INTEGER;
			SetParamDisplayName(ParamID, linkInfo->label, true);
			ParameterMapInt[ParamID] = value;
			SetParamRange(ParamID, static_cast<float>(min), static_cast<float>(max));
			RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
			SetParamVisibility(ParamID, true, true);

			break;
		}
	}
	case TELinkTypeBoolean:
	{
		TEResult result;
		if (linkInfo->intent == TELinkIntentMomentary || linkInfo->intent == TELinkIntentPulse) {
			bool value = false;
			result = TEInstanceLinkGetBooleanValue(instance, linkInfo->identifier, TELinkValueCurrent, &value);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read event value");
				return;
			}

			if (EventParamCount >= MaxParamsByType) {
				LogLinkSkip(linkInfo->identifier, "no free event slots");
				return;
			}
			uint32_t ParamID = (MaxParamsByType * 4) + OffsetParamsByType + EventParamCount++;
			Parameters.push_back(std::make_pair(linkInfo->identifier, ParamID));
			ActiveParams.insert(ParamID);
			if (linkInfo->name != nullptr) EchoNameToParamID[linkInfo->name] = ParamID;
			ParameterMapType[ParamID] = FF_TYPE_EVENT;

			if (linkInfo->intent == TELinkIntentPulse) {
				PulseParameters.insert(ParamID);
			}

			SetParamDisplayName(ParamID, linkInfo->label, true);
			ParameterMapBool[ParamID] = value;
			RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
			SetParamVisibility(ParamID, true, true);
		} else {
			bool value = false;
			result = TEInstanceLinkGetBooleanValue(instance, linkInfo->identifier, TELinkValueCurrent, &value);
			if (result != TEResultSuccess) {
				LogLinkSkip(linkInfo->identifier, "failed to read boolean value");
				return;
			}

			if (BoolParamCount >= MaxParamsByType) {
				LogLinkSkip(linkInfo->identifier, "no free toggle slots");
				return;
			}
			uint32_t ParamID = (MaxParamsByType * 2) + OffsetParamsByType + BoolParamCount++;
			Parameters.push_back(std::make_pair(linkInfo->identifier, ParamID));
			ActiveParams.insert(ParamID);
			if (linkInfo->name != nullptr) EchoNameToParamID[linkInfo->name] = ParamID;
			ParameterMapType[ParamID] = FF_TYPE_BOOLEAN;
			SetParamDisplayName(ParamID, linkInfo->label, true);
			ParameterMapBool[ParamID] = value;
			RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
			SetParamVisibility(ParamID, true, true);
		}

		break;
	}
	case TELinkTypeString:
	{
		TouchObject<TEString> value;
		TEResult result = TEInstanceLinkGetStringValue(instance, linkInfo->identifier, TELinkValueCurrent, value.take());
		if (result != TEResultSuccess) {
			LogLinkSkip(linkInfo->identifier, "failed to read string value");
			return;
		}

		if (StringParamCount >= MaxParamsByType) {
			LogLinkSkip(linkInfo->identifier, "no free text slots");
			return;
		}
		uint32_t ParamID = (MaxParamsByType * 3) + OffsetParamsByType + StringParamCount++;
		Parameters.push_back(std::make_pair(linkInfo->identifier, ParamID));
		ActiveParams.insert(ParamID);
		if (linkInfo->name != nullptr) EchoNameToParamID[linkInfo->name] = ParamID;
		ParameterMapType[ParamID] = FF_TYPE_TEXT;
		SetParamDisplayName(ParamID, linkInfo->label, true);
		ParameterMapString[ParamID] = value->string;
		RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
		SetParamVisibility(ParamID, true, true);

		break;
	}
	default:
	{
		// Unknown / future TELinkType (e.g. par types added in newer TouchDesigner
		// builds). Ignore it gracefully instead of silently aborting the walk, and
		// log so the gap is visible rather than surfacing as "params just missing".
		std::string msg = std::string("FFGLTouchEngine: unsupported link type ") +
			std::to_string(static_cast<int>(linkInfo->type)) + " for '" +
			(linkInfo->identifier ? linkInfo->identifier : "?") + "' — skipping";
		FFGLLog::LogToHost(msg.c_str());
		return;
	}
	}

}

void FFGLTouchEnginePluginBase::CreateParametersFromGroup(const TouchObject<TELinkInfo>& linkInfo) {

	TouchObject<TEStringArray> links;
	TEResult result = TEInstanceLinkGetChildren(instance, linkInfo->identifier, links.take());

	if (result != TEResultSuccess) {
		LogLinkSkip(linkInfo->identifier, "failed to read group children");
		return;
	}

	for (int j = 0; j < links->count; j++) {
		TouchObject<TELinkInfo> childLinkInfo;
		result = TEInstanceLinkGetInfo(instance, links->strings[j], childLinkInfo.take());

		if (result != TEResultSuccess) {
			// Skip this child but keep walking the rest of the group.
			LogLinkSkip(links->strings[j], "failed to read group child link info");
			continue;
		}

		CreateIndividualParameter(childLinkInfo);
	}



}

FFResult FFGLTouchEnginePluginBase::PushParametersToTouchEngine()
{
	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
	if (instance == nullptr) {
		return FF_SUCCESS;
	}

	// Push only host-modified parameters (#28): a blanket push every frame
	// stomped TD-initiated value changes one frame after they happened, and
	// re-sending TE-originated values would echo them back. Each dirty flag is
	// consumed before its push attempt, so a dead link drops one value instead
	// of logging every frame.
	if (DirtyParams.empty()) {
		return FF_SUCCESS;
	}

	for (auto& param : Parameters) {
		if (ActiveVectorParams.find(param.second) != ActiveVectorParams.end()) {
			continue; // vector children are pushed whole-vector below
		}

		auto dirty = DirtyParams.find(param.second);
		if (dirty == DirtyParams.end()) {
			continue;
		}
		DirtyParams.erase(dirty);
		LastPushFrame[param.second] = FrameCount;

		FFUInt32 type = ParameterMapType[param.second];

		if (type == FF_TYPE_STANDARD) {
			TEResult result = TEInstanceLinkSetDoubleValue(instance, param.first.c_str(), &ParameterMapFloat[param.second], 1);
			if (result != TEResultSuccess) {
				isTouchFrameBusy = false;
				return FailAndLog("Failed to set double value");
			}
		}

		if (type == FF_TYPE_INTEGER || type == FF_TYPE_OPTION) {
			TEResult result = TEInstanceLinkSetIntValue(instance, param.first.c_str(), &ParameterMapInt[param.second], 1);
			if (result != TEResultSuccess) {
				isTouchFrameBusy = false;
				return FailAndLog("Failed to set int value");
			}
		}


		if (type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT) {
			TEResult result = TEInstanceLinkSetBooleanValue(instance, param.first.c_str(), ParameterMapBool[param.second]);
			if (result != TEResultSuccess) {
				isTouchFrameBusy = false;
				return FailAndLog("Failed to set boolean value");
			}
			// Auto-reset pulse parameters to false after sending, and mark
			// them dirty again so the falling edge is pushed next frame.
			if (ParameterMapBool[param.second] && PulseParameters.find(param.second) != PulseParameters.end()) {
				ParameterMapBool[param.second] = false;
				DirtyParams.insert(param.second);
			}
		}

		if (type == FF_TYPE_TEXT) {
			TEResult result = TEInstanceLinkSetStringValue(instance, param.first.c_str(), ParameterMapString[param.second].c_str());
			if (result != TEResultSuccess) {
				isTouchFrameBusy = false;
				return FailAndLog("Failed to set string value");
			}
		}

	}

	for (auto& param : VectorParameters) {
		bool dirty = false;
		for (uint8_t i = 0; i < param.count; i++) {
			if (DirtyParams.erase(param.children[i]) > 0) {
				dirty = true;
			}
		}
		if (!dirty) {
			continue;
		}
		for (uint8_t i = 0; i < param.count; i++) {
			LastPushFrame[param.children[i]] = FrameCount;
		}

		double values[4] = { 0,0,0,0 };

		for (uint8_t i = 0; i < param.count; i++) {
			values[i] = ParameterMapFloat[param.children[i]];
		}

		TEResult result = TEInstanceLinkSetDoubleValue(instance, param.identifier.c_str(), values, param.count);
		if (result != TEResultSuccess) {
			isTouchFrameBusy = false;
			return FailAndLog("Failed to set vector value");
		}

	}

	return FF_SUCCESS;
}



void FFGLTouchEnginePluginBase::eventCallback(TEEvent event, TEResult result, int64_t start_time_value, int32_t start_time_scale, int64_t end_time_value, int32_t end_time_scale) {

	// Ignore callbacks during destruction to prevent pure virtual calls
	if (isBeingDestroyed) {
		return;
	}

	// Atomic-only fast path: never let frame completion wait on the state
	// mutex while the render thread holds it through a frame section.
	if (event == TEEventFrameDidFinish) {
		isTouchFrameBusy = false;
		return;
	}

	std::lock_guard<std::recursive_mutex> lock(TEStateMutex);

	if (result == TEResultComponentErrors) {
		TouchObject<TEErrorArray> errors;
		TEResult result = TEInstanceGetErrors(instance, errors.take());
		if (result != TEResultSuccess) {
			return;
		}

		if (!errors) {
			return;
		}

		for (int i = 0; i < errors->count; i++) {
			std::string error =
				"TouchEngine Error: Severity: " +
				GetSeverityString(errors->errors[i].severity) +
				", Location: " + errors->errors[i].location +
				", Description: " + errors->errors[i].description;

			FFGLLog::LogToHost(error.c_str());
		}

		// The TouchEngine has encountered an error
		// You can get the error message with TEInstanceGetError
	}

	if (result == TEResultNoKey || result == TEResultKeyError || result == TEResultExpiredKey) {
		FFGLLog::LogToHost("TouchEngine License Error");
		return;
	}
	switch (event) {
	case TEEventInstanceDidLoad:
		isLoadPending = false;
		if (isReloadQueued) {
			// A newer load request (tox path change or reload pulse) arrived
			// while this load was in flight — supersede it immediately instead
			// of resuming a stale comp.
			isReloadQueued = false;
			LoadTEFile();
			break;
		}
		if (result != TEResultSuccess) {
			// Surface load failures (bad tox, or a tox authored in a newer TD build
			// than this engine) instead of silently showing nothing. Do NOT resume
			// or enumerate a failed load — that used to register parameters against
			// a dead instance and push into it every frame.
			const char* desc = TEResultGetDescription(result);
			std::string msg = std::string("FFGLTouchEngine: instance failed to load — ") +
				(desc ? desc : "unknown error") +
				". Check the tox path and that the TouchEngine build is new enough for this tox.";
			FFGLLog::LogToHost(msg.c_str());
			LoadTimerRunning = false;
			SetLogStatus(std::string("ERROR: instance failed to load — ") + (desc ? desc : "unknown error"));
			break;
		}
		{
			// Surface which engine actually loaded — the engine build silently
			// decides which output link types exist (old engines expose only
			// texture outputs, breaking the par echo channel).
			TouchObject<TEString> enginePath;
			if (TEInstanceGetConfiguredEnginePath(instance, enginePath.take()) == TEResultSuccess
				&& enginePath && enginePath->string[0] != '\0') {
				FFGLLog::LogToHost((std::string("FFGLTouchEngine: engine: ") + enginePath->string).c_str());
			}
		}
		if (LoadTEGraphicsContext(false)) {
			isTouchEngineLoaded = true;
			ResumeTouchEngine();
			// Render-ready only now: comp loaded, resumed, links enumerated.
			isTouchEngineReady = true;
			if (LoadTimerRunning) {
				LoadTimerRunning = false;
				double seconds = std::chrono::duration<double>(
					std::chrono::steady_clock::now() - LoadStartTime).count();
				char buffer[64];
				snprintf(buffer, sizeof(buffer), "loaded in %.2fs", seconds);
				SetLogStatus(buffer);
			}
			else {
				SetLogStatus("loaded");
			}
		} else {
			FFGLLog::LogToHost("Failed to load TE graphics context");
			LoadTimerRunning = false;
			SetLogStatus("ERROR: failed to create the graphics context");
		}
		break;
	case TEEventInstanceReady:
		// NOT render-readiness. Per TEInstanceConfigure docs this event means
		// "configure completed, ready to load" — and during a reload the
		// previously loaded comp has just been UNLOADED at this point. Treating
		// it as render-ready resumed per-frame texture pushes into links that
		// no longer exist and segfaulted the host inside TE (the Reload crash).
		break;
	case TEEventInstanceDidUnload:
		isTouchEngineLoaded = false;
		break;
	}
}

// Apply one TD-originated value to an FFGL slot (#28 echo channel). 'text' is
// non-null for DAT-sourced values (menu tokens, strings); numeric carries the
// parsed number for both sources. Values are stored WITHOUT dirtying so they
// are never pushed back (echo guard), and the host is only poked on change.
void FFGLTouchEnginePluginBase::ApplyEchoValue(FFUInt32 ParamID, double numeric, const char* text) {
	// A host-modified value that hasn't been pushed yet must win over the echo,
	// otherwise the echo reflects TD's OLD state over the pending set and the
	// push never carries the user's change (observed live as menu sets being
	// reverted before they reached TE).
	if (DirtyParams.find(ParamID) != DirtyParams.end()) {
		return;
	}
	auto pushed = LastPushFrame.find(ParamID);
	if (pushed != LastPushFrame.end()) {
		if (FrameCount - pushed->second < EchoSettleFrames) {
			return; // pushed value still in flight; drop stale echoes
		}
		LastPushFrame.erase(pushed);
	}
	switch (ParameterMapType[ParamID]) {
	case FF_TYPE_STANDARD:
	case FF_TYPE_RED:
	case FF_TYPE_GREEN:
	case FF_TYPE_BLUE:
	case FF_TYPE_ALPHA:
	{
		if (ParameterMapFloat[ParamID] == numeric) {
			return;
		}
		ParameterMapFloat[ParamID] = numeric;
		auto range = ParameterRanges.find(ParamID);
		if (range != ParameterRanges.end()) {
			range->second.first = std::min(range->second.first, numeric);
			range->second.second = std::max(range->second.second, numeric);
		}
		RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
		return;
	}
	case FF_TYPE_INTEGER:
	{
		int32_t value = static_cast<int32_t>(std::lround(numeric));
		if (ParameterMapInt[ParamID] == value) {
			return;
		}
		ParameterMapInt[ParamID] = value;
		RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
		return;
	}
	case FF_TYPE_OPTION:
	{
		int32_t index = static_cast<int32_t>(std::lround(numeric));
		if (text != nullptr) {
			// DAT rows carry the menu TOKEN ("red"); map it to its index.
			auto tokens = MenuTokens.find(ParamID);
			if (tokens != MenuTokens.end()) {
				for (size_t k = 0; k < tokens->second.size(); k++) {
					if (tokens->second[k] == text) {
						index = static_cast<int32_t>(k);
						break;
					}
				}
			}
		}
		if (ParameterMapInt[ParamID] == index) {
			return;
		}
		ParameterMapInt[ParamID] = index;
		RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
		return;
	}
	case FF_TYPE_BOOLEAN:
	{
		bool value = numeric != 0.0;
		if (ParameterMapBool[ParamID] == value) {
			return;
		}
		ParameterMapBool[ParamID] = value;
		RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
		return;
	}
	case FF_TYPE_TEXT:
	{
		if (text == nullptr || ParameterMapString[ParamID] == text) {
			return;
		}
		ParameterMapString[ParamID] = text;
		RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
		return;
	}
	default:
		// FF_TYPE_EVENT: pulse buttons are stateless on the host side.
		return;
	}
}

void FFGLTouchEnginePluginBase::HandleEchoChop() {
	TouchObject<TEFloatBuffer> buffer;
	if (TEInstanceLinkGetFloatBufferValue(instance, EchoChopIdentifier.c_str(), TELinkValueCurrent, buffer.take()) != TEResultSuccess || !buffer) {
		return;
	}
	int32_t channels = TEFloatBufferGetChannelCount(buffer);
	uint32_t samples = TEFloatBufferGetValueCount(buffer);
	const char* const* names = TEFloatBufferGetChannelNames(buffer);
	const float* const* values = TEFloatBufferGetValues(buffer);
	if (channels <= 0 || samples == 0 || names == nullptr || values == nullptr) {
		return;
	}
	for (int32_t c = 0; c < channels; c++) {
		if (names[c] == nullptr || values[c] == nullptr) {
			continue;
		}
		auto slot = EchoNameToParamID.find(names[c]);
		if (slot == EchoNameToParamID.end()) {
			continue;
		}
		ApplyEchoValue(slot->second, values[c][0], nullptr);
	}
}

void FFGLTouchEnginePluginBase::HandleEchoDat() {
	TouchObject<TETable> table;
	if (TEInstanceLinkGetTableValue(instance, EchoDatIdentifier.c_str(), TELinkValueCurrent, table.take()) != TEResultSuccess || !table) {
		return;
	}
	int32_t rows = TETableGetRowCount(table);
	int32_t cols = TETableGetColumnCount(table);
	if (rows < 1 || cols < 2) {
		return;
	}
	// Par DAT layout: columns name/value[/eval/style...], optional header row.
	int32_t nameCol = 0, valueCol = 1, firstRow = 0;
	const char* header = TETableGetStringValue(table, 0, 0);
	if (header != nullptr && strcmp(header, "name") == 0) {
		firstRow = 1;
		for (int32_t c = 0; c < cols; c++) {
			const char* colName = TETableGetStringValue(table, 0, c);
			if (colName == nullptr) continue;
			if (strcmp(colName, "name") == 0) nameCol = c;
			else if (strcmp(colName, "value") == 0) valueCol = c;
		}
	}
	for (int32_t r = firstRow; r < rows; r++) {
		const char* name = TETableGetStringValue(table, r, nameCol);
		const char* value = TETableGetStringValue(table, r, valueCol);
		if (name == nullptr || value == nullptr) {
			continue;
		}
		auto slot = EchoNameToParamID.find(name);
		if (slot == EchoNameToParamID.end()) {
			continue;
		}
		double numeric = 0;
		try { numeric = std::stod(value); } catch (...) {}
		ApplyEchoValue(slot->second, numeric, value);
	}
}

void FFGLTouchEnginePluginBase::linkCallback(TELinkEvent event, const char* identifier) {
	if (isBeingDestroyed) {
		return;
	}
	switch (event) {
	case TELinkEventAdded:
	{
		// Echo outputs (Out CHOP/DAT) can be registered by the engine lazily,
		// after enumeration has already walked the output scope — catch them
		// here. Parameter links added late are still handled by re-enumeration.
		if (identifier == nullptr || instance == nullptr) {
			break;
		}
		std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
		TouchObject<TELinkInfo> linkInfo;
		if (TEInstanceLinkGetInfo(instance, identifier, linkInfo.take()) != TEResultSuccess) {
			break;
		}
		if (linkInfo->domain == TELinkDomainOperator) {
			if (linkInfo->type == TELinkTypeFloatBuffer && EchoChopIdentifier.empty()) {
				EchoChopIdentifier = identifier;
				FFGLLog::LogToHost((std::string("FFGLTouchEngine: par echo CHOP registered (late): ") + EchoChopIdentifier).c_str());
			} else if (linkInfo->type == TELinkTypeStringData && EchoDatIdentifier.empty()) {
				EchoDatIdentifier = identifier;
				FFGLLog::LogToHost((std::string("FFGLTouchEngine: par echo DAT registered (late): ") + EchoDatIdentifier).c_str());
			}
		}
		break;
	}
	case TELinkEventRemoved:
	{
		// Safety net: if TE tears down a link we hold an identifier for (an
		// engine-side rebuild we didn't initiate), stop the render thread from
		// pushing into it. Readiness returns with the next enumeration
		// (TEEventInstanceDidLoad). Holding the last frame beats a segfault
		// inside TE's link table.
		if (!isTouchEngineReady || identifier == nullptr) {
			break;
		}
		std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
		bool held = (InputOpName == identifier) || (OutputOpName == identifier);
		if (!held) {
			for (auto& param : Parameters) {
				if (param.first == identifier) { held = true; break; }
			}
		}
		if (held) {
			isTouchEngineReady = false;
			std::string msg = std::string("FFGLTouchEngine: link '") + identifier +
				"' removed by engine — pausing output until parameters are re-enumerated";
			FFGLLog::LogToHost(msg.c_str());
		}
		break;
	}
	case TELinkEventValueChange:
	{
		// #28: reflect TD-initiated value changes back to the host. Values are
		// stored WITHOUT marking dirty (that is the echo guard — they must not
		// be pushed back), and FF_EVENT_FLAG_VALUE makes the host re-query the
		// slot (Resolume 7.4.0+). Our own pushes also land here; the equality
		// checks make those a no-op.
		if (!isTouchEngineReady || identifier == nullptr || instance == nullptr) {
			break;
		}
		// Operator links (our own per-frame texture pushes on in1, cook results
		// on out1) fire ValueChange every frame — skip them before taking the
		// lock; only parameter links are reflected.
		if (InputOpName == identifier || OutputOpName == identifier) {
			break;
		}
		if (EchoChopIdentifier == identifier || EchoDatIdentifier == identifier) {
			// Par-state echo channel (#28): the tox exposes its parameter state
			// through an Out CHOP / Out DAT; their per-cook ValueChange is our
			// only window into TD-initiated par changes.
			std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
			if (!isTouchEngineReady) {
				break;
			}
			if (EchoChopIdentifier == identifier) {
				HandleEchoChop();
			} else {
				HandleEchoDat();
			}
			break;
		}
		// NOTE (#28, verified empirically 2026-07-24): TouchEngine treats input
		// link values as HOST-authoritative. Comp-internal writes to root custom
		// pars emit no ValueChange and are invisible to TEInstanceLinkGet*Value,
		// so TD-initiated changes to input parameters CANNOT be reflected here.
		// This handler still absorbs echoes of our own pushes and covers any
		// engine-side value corrections. TD->host state echo requires an output
		// link (e.g. an Out CHOP with par-named channels) — see CHANGELOG.
		std::lock_guard<std::recursive_mutex> lock(TEStateMutex);
		if (!isTouchEngineReady) {
			break; // re-check under the lock: a reload may have started while we waited
		}

		for (auto& vp : VectorParameters) {
			if (vp.identifier != identifier) {
				continue;
			}
			double value[4] = { 0, 0, 0, 0 };
			if (TEInstanceLinkGetDoubleValue(instance, identifier, TELinkValueCurrent, value, vp.count) != TEResultSuccess) {
				return;
			}
			for (uint8_t i = 0; i < vp.count; i++) {
				if (ParameterMapFloat[vp.children[i]] == value[i]) {
					continue;
				}
				ParameterMapFloat[vp.children[i]] = value[i];
				auto range = ParameterRanges.find(vp.children[i]);
				if (range != ParameterRanges.end()) {
					range->second.first = std::min(range->second.first, value[i]);
					range->second.second = std::max(range->second.second, value[i]);
				}
				RaiseParamEvent(vp.children[i], FF_EVENT_FLAG_VALUE);
			}
			return;
		}

		for (auto& param : Parameters) {
			if (param.first != identifier) {
				continue;
			}
			FFUInt32 ParamID = param.second;
			switch (ParameterMapType[ParamID]) {
			case FF_TYPE_STANDARD:
			{
				double value = 0;
				if (TEInstanceLinkGetDoubleValue(instance, identifier, TELinkValueCurrent, &value, 1) == TEResultSuccess
					&& ParameterMapFloat[ParamID] != value) {
					ParameterMapFloat[ParamID] = value;
					auto range = ParameterRanges.find(ParamID);
					if (range != ParameterRanges.end()) {
						range->second.first = std::min(range->second.first, value);
						range->second.second = std::max(range->second.second, value);
					}
					RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
				}
				break;
			}
			case FF_TYPE_INTEGER:
			case FF_TYPE_OPTION:
			{
				int32_t value = 0;
				if (TEInstanceLinkGetIntValue(instance, identifier, TELinkValueCurrent, &value, 1) == TEResultSuccess
					&& ParameterMapInt[ParamID] != value) {
					ParameterMapInt[ParamID] = value;
					RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
				}
				break;
			}
			case FF_TYPE_BOOLEAN:
			{
				bool value = false;
				if (TEInstanceLinkGetBooleanValue(instance, identifier, TELinkValueCurrent, &value) == TEResultSuccess
					&& ParameterMapBool[ParamID] != value) {
					ParameterMapBool[ParamID] = value;
					RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
				}
				break;
			}
			case FF_TYPE_TEXT:
			{
				TouchObject<TEString> value;
				if (TEInstanceLinkGetStringValue(instance, identifier, TELinkValueCurrent, value.take()) == TEResultSuccess
					&& value && ParameterMapString[ParamID] != value->string) {
					ParameterMapString[ParamID] = value->string;
					RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
				}
				break;
			}
			default:
				// FF_TYPE_EVENT: host pulse buttons are stateless; a TD-side
				// pulse has nothing to reflect.
				break;
			}
			break;
		}
		break;
	}
	default:
		break;
	}
}

void FFGLTouchEnginePluginBase::eventCallbackStatic(TEInstance* instance, TEEvent event, TEResult result, int64_t start_time_value, int32_t start_time_scale, int64_t end_time_value, int32_t end_time_scale, void* info) {
	static_cast<FFGLTouchEnginePluginBase*>(info)->eventCallback(event, result, start_time_value, start_time_scale, end_time_value, end_time_scale);
}

void FFGLTouchEnginePluginBase::linkCallbackStatic(TEInstance* instance, TELinkEvent event, const char* identifier, void* info) {
	static_cast<FFGLTouchEnginePluginBase*>(info)->linkCallback(event, identifier);
}

#ifdef __APPLE__
GLuint FFGLTouchEnginePluginBase::CreateOpenGLTextureFromIOSurface(IOSurfaceRef surface, int width, int height)
{
	GLuint texture = 0;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_RECTANGLE, texture);

	CGLContextObj cglContext = CGLGetCurrentContext();
	CGLError err = CGLTexImageIOSurface2D(
		cglContext,
		GL_TEXTURE_RECTANGLE,
		GL_RGBA,
		width,
		height,
		GL_BGRA,
		GL_UNSIGNED_INT_8_8_8_8_REV,
		surface,
		0
	);

	if (err != kCGLNoError) {
		FFGLLog::LogToHost("Failed to bind IOSurface to OpenGL texture");
		glDeleteTextures(1, &texture);
		glBindTexture(GL_TEXTURE_RECTANGLE, 0);
		return 0;
	}

	glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindTexture(GL_TEXTURE_RECTANGLE, 0);

	return texture;
}

IOSurfaceRef FFGLTouchEnginePluginBase::CreateIOSurface(int width, int height)
{
	NSDictionary *properties = @{
		(NSString *)kIOSurfaceWidth: @(width),
		(NSString *)kIOSurfaceHeight: @(height),
		(NSString *)kIOSurfaceBytesPerElement: @(4),
		(NSString *)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
	};
	return IOSurfaceCreate((__bridge CFDictionaryRef)properties);
}

id<MTLTexture> FFGLTouchEnginePluginBase::CreateIOSurfaceBackedMetalTexture(int width, int height, IOSurfaceRef* outSurface)
{
	// Create the IOSurface
	IOSurfaceRef surface = CreateIOSurface(width, height);
	if (surface == nullptr) {
		FFGLLog::LogToHost("Failed to create IOSurface for Metal texture");
		return nil;
	}

	// Create a Metal texture descriptor matching the IOSurface
	MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
		width:width
		height:height
		mipmapped:NO];
	desc.storageMode = MTLStorageModeShared;
	desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;

	// Create Metal texture backed by the IOSurface
	id<MTLTexture> texture = [MetalDevice newTextureWithDescriptor:desc iosurface:surface plane:0];
	if (texture == nil) {
		FFGLLog::LogToHost("Failed to create IOSurface-backed Metal texture");
		CFRelease(surface);
		return nil;
	}

	*outSurface = surface;
	return texture;
}

void FFGLTouchEnginePluginBase::CopyMetalTexture(id<MTLTexture> src, id<MTLTexture> dst)
{
	id<MTLCommandBuffer> cmdBuf = [MetalCommandQueue commandBuffer];
	id<MTLBlitCommandEncoder> blit = [cmdBuf blitCommandEncoder];
	[blit copyFromTexture:src
		sourceSlice:0
		sourceLevel:0
		sourceOrigin:MTLOriginMake(0, 0, 0)
		sourceSize:MTLSizeMake(src.width, src.height, 1)
		toTexture:dst
		destinationSlice:0
		destinationLevel:0
		destinationOrigin:MTLOriginMake(0, 0, 0)];
	[blit endEncoding];
	[cmdBuf commit];
	[cmdBuf waitUntilCompleted];
}
#endif
