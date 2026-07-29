#include "TouchEngineFXPresets.h"

// This target compiles TouchEngineFX.cpp with FFGLTE_NO_PLUGININFO, so these
// are the only plugin-info singletons in the module. The name here is the
// effect's display name in Resolume AND the preset folder name — it must match
// the string FFGLTouchEngineFXPresets passes to its base constructor.
static CFFGLPluginInfo PluginInfo(
	PluginFactory< FFGLTouchEngineFXPresets >,// Create method
	"TEFP",                        // Plugin unique ID
	"TEFX Presets",                // Plugin name (16-char FFGL field)
	2,                             // API major version number
	1,                             // API minor version number
	0,                             // Plugin major version number
	100,                           // Plugin minor version number
	FF_EFFECT,                     // Plugin type
	"Loads tox files from TouchDesigner, with native color pickers and host-preset recall with morph",// Plugin description
	"TouchEngine Loader made by Evan Clark; presets/color fork by drmbt"// About
);

static CFFGLThumbnailInfo ThumbnailInfo(160, 120, thumbnail);
