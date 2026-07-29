#pragma once

// Host-agnostic half of the preset-recall-with-morph system, ported from
// drmbt-custom-fx (sdk/drmbt/PresetMorph.*; see the ffgl-preset-morph skill).
// Everything here is plain C++/filesystem: preset folder discovery, the flat XML
// scanning Resolume's per-effect preset files need, the easing curve menu, and the
// dt-clocked morph engine. The FFGL host glue lives in TouchEnginePresetRecall.cpp,
// driving FFGLTouchEnginePluginBase's own param storage.

#include <string>
#include <vector>

namespace drmbt
{
namespace preset
{
/// <Documents>/Resolume Arena/Presets/Video Effects/<effectDisplayName>. FFGL has no host
/// path query, so this is derived OS-side; the folder name must match the effect's DISPLAY
/// name exactly. Returns a best-guess path even when no Arena install is found (the scan
/// then simply yields an empty menu).
std::string Directory( const std::string& effectDisplayName );

/// Sorted .xml files in the directory, AppleDouble "._*" droppings filtered out.
std::vector< std::string > Scan( const std::string& directory );

/// Whole file as a string; empty when unreadable.
std::string ReadFile( const std::string& path );

/// Slice of the preset file belonging to the FFGL effect itself. The DryWet wrapper and
/// mixer around it carry their own "Opacity" params, which would otherwise shadow the
/// plugin's — every lookup below must be scoped to this slice.
std::string EffectSlice( const std::string& xml );

/// value="..." of the first <ParamRange name="<name>" in scope.
bool FindRangeValue( const std::string& scope, const std::string& name, float& outValue );

/// value="..." of the first <Param name="<name>" in scope (bools).
bool FindParamValue( const std::string& scope, const std::string& name, float& outValue );

/// value="..." of the first <ParamChoice name="<name>" in scope — how the host stores an
/// FFGL option param's selection.
bool FindChoiceValue( const std::string& scope, const std::string& name, float& outValue );

/// The Channels sub-block of <ParamColor name="<name>"> — the host collapses an HSBA
/// picker quad into one ParamColor, so the four channels live here, not at top level.
std::string ColorChannelsSlice( const std::string& scope, const std::string& name );

//The morph's interpolation curve menu, mirroring Resolume's envelope keyframe curves.
//Monotonic curves only — Elastic/Back/Bounce were implemented and deliberately cut:
//overshoot at the target reads as a glitch on a preset recall, not a flourish.
extern const char* const kCurveNames[];
extern const int kNumCurves;
extern const int kDefaultCurve;//SineInOut — the S curve.

float Ease( int curve, float t );

}//End namespace preset

/// One param's journey during a recall. `hue` targets legitimately sit outside [0,1]
/// (shortest-path around the wheel) and wrap instead of clamping; everything else clamps to
/// its own rails, which are 0..1 for normalized params and the real range for a ParamRange.
struct MorphTarget
{
	unsigned int paramIndex;
	float from;
	float to;
	bool hue  = false;
	float min = 0.0f;
	float max = 1.0f;
};

/// Adjusts a hue target for shortest-path travel around the wheel.
void ShortestHuePath( MorphTarget& target );

/// HSB -> straight RGB, with the hue-1.0 wrap (hue 1.0 must read as red, not pink).
/// Quickstart plugins get this for free — SendParams converts an HSBA quad into a vec4 —
/// but a raw-SDK plugin holds the four channels itself and has to convert.
void HsbToRgb( float h, float s, float v, float& r, float& g, float& b );

/// Straight RGB -> HSB. Hue (and, at black, saturation) are undefined for grays, so the
/// previous values are preserved there — a picker whose brightness hits 0 must not snap
/// its hue back to red when the value comes round-tripped from TouchDesigner.
void RgbToHsb( float r, float g, float b, float prevH, float prevS, float& h, float& s, float& v );

/// The morph engine: holds the in-flight glide and steps it on accumulated render time.
class Morpher
{
public:
	void Begin( std::vector< MorphTarget > targets, float seconds );

	/// Snap engaging mid-glide finishes it instantly — also settles the double-mapping race
	/// where one key writes both Snap and a recall and the recall lands first.
	void Finish()
	{
		morphSeconds = 0.0f;
	}

	bool Active() const
	{
		return morphing;
	}

	/// Advances the glide by one frame and hands every target's shaped value to `apply`
	/// (signature: void( unsigned int paramIndex, float value )). Returns true on the frame
	/// the morph completes. Clocked on accumulated deltaTime, not wall time, so a bypassed
	/// or ejected clip pauses its glide instead of silently finishing.
	template< class Apply >
	bool Step( float deltaTime, int curve, Apply apply )
	{
		if( !morphing )
			return false;

		float t = 1.0f;
		if( morphSeconds > 0.0f )
		{
			//Clamped: the first deltaTime after a render pause spans the whole gap.
			morphElapsed += Clamp01Delta( deltaTime );
			t = morphElapsed / morphSeconds;
			if( t > 1.0f )
				t = 1.0f;
		}

		const float shapedT = preset::Ease( curve, t );
		for( const MorphTarget& target : targets )
			apply( target.paramIndex, Shape( target, shapedT ) );

		if( t >= 1.0f )
		{
			morphing = false;
			return true;
		}
		return false;
	}

	const std::vector< MorphTarget >& Targets() const
	{
		return targets;
	}

private:
	static float Clamp01Delta( float deltaTime );
	static float Shape( const MorphTarget& target, float shapedT );

	std::vector< MorphTarget > targets;
	bool morphing       = false;
	float morphSeconds  = 0.0f;
	float morphElapsed  = 0.0f;
};

}//End namespace drmbt
