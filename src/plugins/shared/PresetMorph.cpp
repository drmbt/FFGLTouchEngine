#include "PresetMorph.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
	#include <windows.h>
#endif

namespace drmbt
{
namespace preset
{
//FFGL offers no host-path query, so the preset root is derived OS-side. Deterministic per
//OS: macOS is always ~/Documents; Windows Documents may be redirected (OneDrive Known
//Folder Move, domain redirection), which SHGetKnownFolderPath(FOLDERID_Documents) resolves
//authoritatively — with raw USERPROFILE and OneDrive env fallbacks for exotic setups. The
//first candidate that actually contains a "Resolume Arena" folder wins.
static std::vector< std::string > DocumentsCandidates()
{
	std::vector< std::string > candidates;
#ifdef _WIN32
	//SHGetKnownFolderPath resolved dynamically: the FFGL SDK includes a deliberately
	//restricted windows.h, and shlobj.h/ole2.h refuse to compile after it (missing LPMSG
	//etc.). GetProcAddress needs no headers or import libs at all.
	typedef HRESULT( WINAPI * KnownFolderFn )( const GUID&, DWORD, HANDLE, PWSTR* );
	typedef void( WINAPI * TaskMemFreeFn )( void* );
	static const GUID kFolderIdDocuments = { 0xFDD39AD0, 0x238F, 0x46AF, { 0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7 } };

	HMODULE shell32 = LoadLibraryA( "shell32.dll" );
	HMODULE ole32   = LoadLibraryA( "ole32.dll" );
	if( shell32 != nullptr && ole32 != nullptr )
	{
		KnownFolderFn getKnownFolder = reinterpret_cast< KnownFolderFn >(
			reinterpret_cast< void* >( GetProcAddress( shell32, "SHGetKnownFolderPath" ) ) );
		TaskMemFreeFn taskMemFree = reinterpret_cast< TaskMemFreeFn >(
			reinterpret_cast< void* >( GetProcAddress( ole32, "CoTaskMemFree" ) ) );

		PWSTR knownPath = nullptr;
		if( getKnownFolder != nullptr && taskMemFree != nullptr &&
			SUCCEEDED( getKnownFolder( kFolderIdDocuments, 0, nullptr, &knownPath ) ) && knownPath != nullptr )
		{
			//std::filesystem handles the wide->narrow conversion (the SDK's restricted
			//windows.h also excludes the NLS conversion APIs).
			candidates.push_back( std::filesystem::path( knownPath ).string() );
			taskMemFree( knownPath );
		}
	}
	if( const char* profile = std::getenv( "USERPROFILE" ) )
		candidates.push_back( std::string( profile ) + "\\Documents" );
	if( const char* oneDrive = std::getenv( "OneDrive" ) )
		candidates.push_back( std::string( oneDrive ) + "\\Documents" );
#else
	if( const char* home = std::getenv( "HOME" ) )
		candidates.push_back( std::string( home ) + "/Documents" );
#endif
	return candidates;
}

std::string Directory( const std::string& effectDisplayName )
{
#ifdef _WIN32
	const char sep = '\\';
#else
	const char sep = '/';
#endif
	const std::string s( 1, sep );
	const std::string tail = s + "Resolume Arena" + sep + "Presets" + sep + "Video Effects" + sep + effectDisplayName;

	std::error_code ec;
	std::string first;
	for( const std::string& documents : DocumentsCandidates() )
	{
		if( first.empty() )
			first = documents + tail;
		if( std::filesystem::is_directory( documents + s + "Resolume Arena", ec ) )
			return documents + tail;
	}
	return first;//No live install found; scan will simply yield an empty menu.
}

std::vector< std::string > Scan( const std::string& directory )
{
	std::vector< std::string > paths;
	if( directory.empty() )
		return paths;

	std::error_code ec;
	for( const auto& entry : std::filesystem::directory_iterator( directory, ec ) )
	{
		//The ec overload throughout: the throwing overloads abort the HOST on the
		//kinds of entries a OneDrive-synced folder can serve up (a CRT fail-fast,
		//not a catchable crash), and a preset scan must never be able to do that.
		std::error_code entryEc;
		if( ec || !entry.is_regular_file( entryEc ) || entryEc )
			continue;
		//AppleDouble droppings from Mac-synced folders are not presets.
		if( entry.path().filename().string().rfind( "._", 0 ) == 0 )
			continue;
		if( entry.path().extension() != ".xml" )
			continue;
		paths.push_back( entry.path().string() );
	}
	std::sort( paths.begin(), paths.end() );
	return paths;
}

std::string ReadFile( const std::string& path )
{
	std::ifstream file( path, std::ios::binary );
	if( !file )
		return std::string();
	std::stringstream buffer;
	buffer << file.rdbuf();
	return buffer.str();
}

std::string EffectSlice( const std::string& xml )
{
	const size_t begin = xml.find( "type=\"FFGLEffect\"" );
	if( begin == std::string::npos )
		return std::string();
	size_t end = xml.find( "<ChoosableMixer", begin );
	if( end == std::string::npos )
		end = xml.size();
	return xml.substr( begin, end - begin );
}

/// Shared tag lookup: value="..." of the first <tag name="<name>" in scope.
static bool FindTagValue( const std::string& scope, const std::string& tag, const std::string& name, float& outValue )
{
	const std::string needle = "<" + tag + " name=\"" + name + "\"";
	const size_t at          = scope.find( needle );
	if( at == std::string::npos )
		return false;
	const size_t tagEnd  = scope.find( '>', at );
	const size_t valueAt = scope.find( "value=\"", at );
	if( valueAt == std::string::npos || tagEnd == std::string::npos || valueAt > tagEnd )
		return false;
	outValue = std::strtof( scope.c_str() + valueAt + 7, nullptr );
	return true;
}

bool FindRangeValue( const std::string& scope, const std::string& name, float& outValue )
{
	return FindTagValue( scope, "ParamRange", name, outValue );
}

bool FindParamValue( const std::string& scope, const std::string& name, float& outValue )
{
	return FindTagValue( scope, "Param", name, outValue );
}

bool FindChoiceValue( const std::string& scope, const std::string& name, float& outValue )
{
	return FindTagValue( scope, "ParamChoice", name, outValue );
}

std::string ColorChannelsSlice( const std::string& scope, const std::string& name )
{
	const std::string needle = "<ParamColor name=\"" + name + "\"";
	const size_t at          = scope.find( needle );
	if( at == std::string::npos )
		return std::string();
	size_t end = scope.find( "</ParamColor>", at );
	if( end == std::string::npos )
		end = scope.size();
	return scope.substr( at, end - at );
}

const char* const kCurveNames[] = {
	"Linear",
	"QuadIn", "QuadOut", "QuadInOut",
	"SineIn", "SineOut", "SineInOut",
	"CircIn", "CircOut", "CircInOut",
	"ExpoIn", "ExpoOut", "ExpoInOut",
	"Hold"
};
const int kNumCurves    = sizeof( kCurveNames ) / sizeof( kCurveNames[ 0 ] );
const int kDefaultCurve = 6;//SineInOut — the S curve.

static const float kPi = 3.14159265358979f;

float Ease( int curve, float t )
{
	t = std::max( 0.0f, std::min( t, 1.0f ) );
	switch( curve )
	{
	default:
	case 0: return t;                                                                    //Linear
	case 1: return t * t;                                                                //QuadIn
	case 2: return t * ( 2.0f - t );                                                     //QuadOut
	case 3: return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * ( 1.0f - t ) * ( 1.0f - t );  //QuadInOut
	case 4: return 1.0f - std::cos( t * kPi * 0.5f );                                    //SineIn
	case 5: return std::sin( t * kPi * 0.5f );                                           //SineOut
	case 6: return 0.5f - 0.5f * std::cos( t * kPi );                                    //SineInOut
	case 7: return 1.0f - std::sqrt( 1.0f - t * t );                                     //CircIn
	case 8: return std::sqrt( 1.0f - ( 1.0f - t ) * ( 1.0f - t ) );                      //CircOut
	case 9: return t < 0.5f ? 0.5f * ( 1.0f - std::sqrt( 1.0f - 4.0f * t * t ) )
							: 0.5f * ( std::sqrt( 1.0f - 4.0f * ( 1.0f - t ) * ( 1.0f - t ) ) + 1.0f );//CircInOut
	case 10: return t <= 0.0f ? 0.0f : std::pow( 2.0f, 10.0f * ( t - 1.0f ) );           //ExpoIn
	case 11: return t >= 1.0f ? 1.0f : 1.0f - std::pow( 2.0f, -10.0f * t );              //ExpoOut
	case 12:                                                                             //ExpoInOut
		if( t <= 0.0f ) return 0.0f;
		if( t >= 1.0f ) return 1.0f;
		return t < 0.5f ? 0.5f * std::pow( 2.0f, 20.0f * t - 10.0f )
						: 1.0f - 0.5f * std::pow( 2.0f, -20.0f * t + 10.0f );
	case 13: return t >= 1.0f ? 1.0f : 0.0f;                                             //Hold
	}
}

}//End namespace preset

void ShortestHuePath( MorphTarget& target )
{
	//Hue is circular: glide the short way around the wheel. Step() wraps the applied value
	//back into [0,1), so a target outside the rails is intentional here.
	const float delta = target.to - target.from;
	if( delta > 0.5f )
		target.to -= 1.0f;
	else if( delta < -0.5f )
		target.to += 1.0f;
}

void HsbToRgb( float h, float s, float v, float& r, float& g, float& b )
{
	if( h >= 1.0f )
		h = 0.0f;//Otherwise hue 1.0 comes out pink instead of wrapping to red.
	const float i = std::floor( h * 6.0f );
	const float f = h * 6.0f - i;
	const float p = v * ( 1.0f - s );
	const float q = v * ( 1.0f - f * s );
	const float t = v * ( 1.0f - ( 1.0f - f ) * s );
	switch( static_cast< int >( i ) % 6 )
	{
	case 0: r = v; g = t; b = p; break;
	case 1: r = q; g = v; b = p; break;
	case 2: r = p; g = v; b = t; break;
	case 3: r = p; g = q; b = v; break;
	case 4: r = t; g = p; b = v; break;
	default: r = v; g = p; b = q; break;
	}
}

void RgbToHsb( float r, float g, float b, float prevH, float prevS, float& h, float& s, float& v )
{
	const float mx = std::max( r, std::max( g, b ) );
	const float mn = std::min( r, std::min( g, b ) );
	const float d  = mx - mn;
	v              = mx;
	if( mx <= 0.0f )
	{
		//Black: both hue and saturation are undefined — keep what the user last had.
		h = prevH;
		s = prevS;
		return;
	}
	s = d / mx;
	if( d <= 1e-6f )
	{
		h = prevH;//Gray: hue undefined.
		return;
	}
	if( mx == r )
		h = ( g - b ) / d;
	else if( mx == g )
		h = 2.0f + ( b - r ) / d;
	else
		h = 4.0f + ( r - g ) / d;
	h /= 6.0f;
	h -= std::floor( h );//Wrap into [0,1).
}

void Morpher::Begin( std::vector< MorphTarget > newTargets, float seconds )
{
	targets      = std::move( newTargets );
	morphSeconds = seconds;
	morphElapsed = 0.0f;
	morphing     = !targets.empty();
}

float Morpher::Clamp01Delta( float deltaTime )
{
	return std::min( std::max( 0.0f, deltaTime ), 0.1f );
}

float Morpher::Shape( const MorphTarget& target, float shapedT )
{
	const float v = target.from + ( target.to - target.from ) * shapedT;
	if( target.hue )
		return v - std::floor( v );//Wraps around the wheel.
	return std::max( target.min, std::min( v, target.max ) );//Safety clamp; all curves are monotonic in [0,1].
}

}//End namespace drmbt
