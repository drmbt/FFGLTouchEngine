#pragma once

#include "../FFGLTouchEngineFX/TouchEngineFX.h"

// The preset-enabled FX variant. Behaviorally it IS FFGLTouchEngineFX plus the
// two opt-in features the base carries:
// - HSBA color quads: TD-side RGBA parameters surface as consecutive
//   HUE/SATURATION/BRIGHTNESS/ALPHA runs, which is what makes Resolume render
//   its native color picker instead of four loose sliders.
// - The preset block (Preset/Morph/Recall/Rescan/Snap/Curve), built from
//   Resolume's own per-effect preset XMLs under
//   <Documents>/Resolume Arena/Presets/Video Effects/<display name>/.
//
// A separate plugin (own ID and display name) rather than a new version of
// TouchEngineFX because the HSBA switch changes what the color slots' stored
// values MEAN: a comp saved against RGBA-typed slots would reinterpret red
// (1,0,0) as hue 1 / sat 0 / bright 0 = black. New plugin, no migration risk.
//
// The display name is capped by FFGL's 16-char PluginName field, so the
// intended "TouchEngineFX_presets" cannot fit on the wire — "TEFX Presets" is
// the name Resolume shows AND the preset folder's name. The string passed here
// must stay identical to the name in this target's CFFGLPluginInfo.
class FFGLTouchEngineFXPresets : public FFGLTouchEngineFX
{
public:
	FFGLTouchEngineFXPresets()
		: FFGLTouchEngineFX(true, "TEFX Presets")
	{
	}
};
