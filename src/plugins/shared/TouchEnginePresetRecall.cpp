// In-plugin preset recall with morph, ported from drmbt-custom-fx (see the
// ffgl-preset-morph skill there). PresetMorph.{h,cpp} carries the host-agnostic
// half (folder discovery, XML scanning, easing, the dt-clocked Morpher); this
// file is the FFGL glue that drives FFGLTouchEnginePluginBase's dynamic-slot
// param storage with it.
//
// Design constraints inherited from the proven implementation:
// - Preset SAVING stays native host UI (Resolume's P. dropdown). The plugin
//   only reads the XMLs Arena wrote, so the menu and Arena's own preset list
//   always describe the same files.
// - Recall carries VALUES only. The Tox File slot is never touched — recalling
//   a preset must not reload (or swap) the tox; that is what made host-side
//   preset recall destructive before v3.5.0.
// - The six controls are excluded from recall by construction: the parser only
//   reads the pre-allocated tox families.

#include "TouchEnginePluginBase.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

using drmbt::MorphTarget;
namespace preset = drmbt::preset;

// Family index of a pre-allocated slot: 0 float, 1 int, 2 toggle, 3 text,
// 4 pulse, 5 menu, 6 color. Returns -1 for anything outside the families.
static int SlotFamily(FFUInt32 ParamID, uint32_t offset, uint32_t perFamily) {
	if (ParamID < offset || ParamID >= offset + perFamily * 7) {
		return -1;
	}
	return static_cast<int>((ParamID - offset) / perFamily);
}

std::string FFGLTouchEnginePluginBase::StaticSlotName(FFUInt32 ParamID) const {
	const int family = SlotFamily(ParamID, OffsetParamsByType, MaxParamsByType);
	if (family < 0) {
		return std::string();
	}
	const uint32_t n = (ParamID - OffsetParamsByType) % MaxParamsByType + 1;
	switch (family) {
	case 0: return "Float" + std::to_string(n);
	case 1: return "Int" + std::to_string(n);
	case 2: return "Toggle" + std::to_string(n);
	case 3: return "Text" + std::to_string(n);
	case 4: return "Pulse" + std::to_string(n);
	case 5: return "Menu" + std::to_string(n);
	default: {
		const uint32_t idx = ParamID - ColorFamilyBase();
		const std::string head = "Color" + std::to_string(idx / 4 + 1);
		if (UseHsbaColorQuads) {
			static const char* const suffixes[] = { "", "_sat", "_bri", "_alpha" };
			return head + suffixes[idx % 4];
		}
		static const char* const suffixes[] = { "R", "G", "B", "A" };
		return head + suffixes[idx % 4];
	}
	}
}

void FFGLTouchEnginePluginBase::ConstructPresetParameters() {
	const FFUInt32 base = OffsetParamsByType + MaxParamsByType * 7;
	PresetParamID = base;
	MorphParamID = base + 1;
	RecallParamID = base + 2;
	RescanParamID = base + 3;
	SnapParamID = base + 4;
	CurveParamID = base + 5;

	ScanPresetFolder();
	SetOptionParamInfo(PresetParamID, "Preset",
		static_cast<unsigned int>(PresetNames.size()), 0.0f);
	ApplyPresetElements(false);

	// Ranged FF_TYPE_STANDARD: the host reads and writes literal seconds. The
	// default sits inside [0,1], so SetParamInfo's pre-range clamp cannot bite
	// (the trap that turned Idle Seconds' default 20 into 1).
	SetParamInfo(MorphParamID, "Morph", FF_TYPE_STANDARD, 0.5f);
	SetParamRange(MorphParamID, 0.0f, 10.0f);

	SetParamInfof(RecallParamID, "Recall", FF_TYPE_EVENT);
	SetParamInfof(RescanParamID, "Rescan", FF_TYPE_EVENT);
	// Momentary instant-recall modifier: while on, recalls jump. Off by default.
	SetParamInfo(SnapParamID, "Snap", FF_TYPE_BOOLEAN, false);

	SetOptionParamInfo(CurveParamID, "Curve", preset::kNumCurves,
		static_cast<float>(preset::kDefaultCurve));
	std::vector<std::string> curveLabels;
	std::vector<float> curveValues;
	for (int i = 0; i < preset::kNumCurves; i++) {
		curveLabels.push_back(preset::kCurveNames[i]);
		curveValues.push_back(static_cast<float>(i));
	}
	SetParamElements(CurveParamID, curveLabels, curveValues, false);
}

void FFGLTouchEnginePluginBase::ScanPresetFolder() {
	// Element 0 is the None slot: value 0, so a fresh instance (whose option
	// params default to 0) never auto-recalls anything.
	PresetNames.assign(1, "None");
	PresetPaths.assign(1, std::string());

	const std::string directory = preset::Directory(PresetEffectName);
	for (const std::string& path : preset::Scan(directory)) {
		PresetNames.push_back(std::filesystem::path(path).stem().string());
		PresetPaths.push_back(path);
	}
	FFGLLog::LogToHost((std::string("FFGLTouchEngine: ") +
		std::to_string(PresetNames.size() - 1) + " preset(s) scanned from " +
		directory).c_str());
}

void FFGLTouchEnginePluginBase::ApplyPresetElements(bool raiseEvent) {
	std::vector<float> values;
	for (size_t i = 0; i < PresetNames.size(); i++) {
		values.push_back(static_cast<float>(i));
	}
	SetParamElements(PresetParamID, PresetNames, values, raiseEvent);
}

void FFGLTouchEnginePluginBase::NoteRecallSetWrite(FFUInt32 ParamID) {
	const int family = SlotFamily(ParamID, OffsetParamsByType, MaxParamsByType);
	// Only slots a recall itself writes may arm the adoption guard. Text (3) is
	// excluded from recall and pulses (4) are host-driven events — either would
	// keep the guard permanently armed and recall would silently never fire
	// (the FrameGrid failure mode).
	if (family < 0 || family == 3 || family == 4) {
		return;
	}
	LastRecallSetWriteFrame = MorphFrameCounter;
	HasRecallSetWrite = true;
}

bool FFGLTouchEnginePluginBase::HandlePresetSetFloat(unsigned int dwIndex, float value) {
	if (PresetParamID == 0) {
		return false;
	}
	if (dwIndex == PresetParamID) {
		int selection = static_cast<int>(std::lround(value));
		if (selection < 0) selection = 0;
		if (selection >= static_cast<int>(PresetNames.size())) {
			selection = static_cast<int>(PresetNames.size()) - 1;
		}
		// Host restores (comp load, native P.-dropdown recall) arrive as a burst
		// of param writes around the stored Preset value; a user menu click sets
		// only Preset. Adopt the selection without recalling on a burst — Arena
		// is already delivering every value itself, and the stored selection may
		// even point at a DIFFERENT preset than the one being restored.
		const bool adopt = !RenderedOnce ||
			(HasRecallSetWrite && MorphFrameCounter - LastRecallSetWriteFrame <= PresetAdoptWindowFrames);
		PresetSelection = selection;
		if (selection > 0) {
			if (adopt) {
				FFGLLog::LogToHost("FFGLTouchEngine: preset selection adopted without recall (host restore)");
			} else {
				RecallSelectedPreset();
			}
		}
		return true;
	}
	if (dwIndex == MorphParamID) {
		MorphSecondsParam = std::max(0.0f, std::min(value, 10.0f));
		return true;
	}
	if (dwIndex == RecallParamID) {
		// Re-fires the current selection (after hand-tweaks). Deliberately
		// ignores the adoption guard — this trigger IS the recovery from it.
		if (value == 1) {
			RecallSelectedPreset();
		}
		return true;
	}
	if (dwIndex == RescanParamID) {
		if (value == 1) {
			RescanPresets();
		}
		return true;
	}
	if (dwIndex == SnapParamID) {
		SnapRecall = (value != 0.0f);
		// Snap engaging mid-glide finishes it instantly. This settles the
		// double-mapping race: one key mapped to both Snap and a recall can
		// deliver the recall first (Snap still off), starting a glide the Snap
		// write right behind it must jump home.
		if (SnapRecall) {
			PresetMorpher.Finish();
		}
		return true;
	}
	if (dwIndex == CurveParamID) {
		int curve = static_cast<int>(std::lround(value));
		CurveSelectionParam = std::max(0, std::min(curve, preset::kNumCurves - 1));
		return true;
	}
	return false;
}

bool FFGLTouchEnginePluginBase::GetPresetFloat(unsigned int dwIndex, float& outValue) {
	if (PresetParamID == 0) {
		return false;
	}
	if (dwIndex == PresetParamID) {
		outValue = static_cast<float>(PresetSelection);
		return true;
	}
	if (dwIndex == MorphParamID) {
		outValue = MorphSecondsParam;
		return true;
	}
	if (dwIndex == RecallParamID || dwIndex == RescanParamID) {
		outValue = 0.0f;
		return true;
	}
	if (dwIndex == SnapParamID) {
		outValue = SnapRecall ? 1.0f : 0.0f;
		return true;
	}
	if (dwIndex == CurveParamID) {
		outValue = static_cast<float>(CurveSelectionParam);
		return true;
	}
	return false;
}

void FFGLTouchEnginePluginBase::RecallSelectedPreset() {
	if (PresetSelection <= 0 || PresetSelection >= static_cast<int>(PresetPaths.size())) {
		return;
	}
	const std::string xml = preset::ReadFile(PresetPaths[PresetSelection]);
	const std::string scope = preset::EffectSlice(xml);
	if (scope.empty()) {
		FFGLLog::LogToHost((std::string("FFGLTouchEngine: preset '") +
			PresetNames[PresetSelection] + "' has no FFGLEffect block — not recalled").c_str());
		return;
	}

	// Floats (and, in RGBA mode, color children) glide; ints, bools and menus
	// snap at recall start; text and pulses are excluded entirely. Everything
	// morphs in host-wire space so one apply path serves the host UI events and
	// the TE push equally.
	std::vector<MorphTarget> targets;

	for (const FFUInt32 ParamID : ActiveParams) {
		const int family = SlotFamily(ParamID, OffsetParamsByType, MaxParamsByType);
		float value = 0.0f;
		switch (family) {
		case 0://Float — morphs, wire space is the normalized 0-1 prototype range.
			if (preset::FindRangeValue(scope, StaticSlotName(ParamID), value)) {
				MorphTarget target;
				target.paramIndex = ParamID;
				target.from = static_cast<float>(NormalizeToHost(ParamID, ParameterMapFloat[ParamID]));
				target.to = std::max(0.0f, std::min(value, 1.0f));
				targets.push_back(target);
			}
			break;
		case 1://Int — snaps. The wire carries real values (range declared at enumeration).
			if (preset::FindRangeValue(scope, StaticSlotName(ParamID), value)) {
				const int32_t intValue = static_cast<int32_t>(std::lround(value));
				if (ParameterMapInt[ParamID] != intValue) {
					ParameterMapInt[ParamID] = intValue;
					DirtyParams.insert(ParamID);
					RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
				}
			}
			break;
		case 2://Toggle — snaps.
			if (preset::FindParamValue(scope, StaticSlotName(ParamID), value)) {
				const bool boolValue = (value != 0.0f);
				if (ParameterMapBool[ParamID] != boolValue) {
					ParameterMapBool[ParamID] = boolValue;
					DirtyParams.insert(ParamID);
					RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
				}
			}
			break;
		case 5://Menu — snaps (mode-like; morphing an enum reads as glitching).
			if (preset::FindChoiceValue(scope, StaticSlotName(ParamID), value)) {
				const int32_t option = static_cast<int32_t>(std::lround(value));
				if (ParameterMapInt[ParamID] != option) {
					ParameterMapInt[ParamID] = option;
					DirtyParams.insert(ParamID);
					RaiseParamEvent(ParamID, FF_EVENT_FLAG_VALUE);
				}
			}
			break;
		default:
			break;//Text/pulse excluded; color quads handled whole below.
		}
	}

	// Color quads, whole-vector. In HSBA mode Arena collapses the quad into a
	// <ParamColor> whose Channels block carries Hue/Saturation/Brightness/Alpha
	// — the four channels live THERE, not as top-level ParamRanges.
	for (const auto& vp : VectorParameters) {
		if (!IsColorFamilySlot(vp.children[0])) {
			continue;
		}
		const FFUInt32 head = ColorQuadHead(vp.children[0]);
		if (UseHsbaColorQuads) {
			const std::string channels = preset::ColorChannelsSlice(scope, StaticSlotName(head));
			if (channels.empty()) {
				continue;
			}
			static const char* const channelNames[4] = { "Hue", "Saturation", "Brightness", "Alpha" };
			const std::array<float, 4>& hsba = ColorQuadHsba[head];
			for (uint32_t i = 0; i < 4 && i < vp.count; i++) {
				float value = 0.0f;
				if (!preset::FindRangeValue(channels, channelNames[i], value)) {
					continue;
				}
				MorphTarget target;
				target.paramIndex = head + i;
				target.from = hsba[i];
				target.to = std::max(0.0f, std::min(value, 1.0f));
				if (i == 0) {
					// Hue is circular — glide the short way around the wheel.
					target.hue = true;
					drmbt::ShortestHuePath(target);
				}
				targets.push_back(target);
			}
		} else {
			// RGBA-typed quads stay loose ParamRanges in the preset file.
			for (uint32_t i = 0; i < vp.count; i++) {
				float value = 0.0f;
				if (!preset::FindRangeValue(scope, StaticSlotName(head + i), value)) {
					continue;
				}
				MorphTarget target;
				target.paramIndex = head + i;
				target.from = static_cast<float>(ParameterMapFloat[head + i]);
				target.to = std::max(0.0f, std::min(value, 1.0f));
				targets.push_back(target);
			}
		}
	}

	// Snap active means jump; the live Morph knob is read here and nowhere else
	// (the copy Arena stored inside the preset file is deliberately not parsed —
	// presets carry destinations, one knob owns glide speed).
	PresetMorpher.Begin(std::move(targets), SnapRecall ? 0.0f : MorphSecondsParam);
	SetLogStatus("recalling preset '" + PresetNames[PresetSelection] + "'");
}

void FFGLTouchEnginePluginBase::RescanPresets() {
	const std::string current =
		(PresetSelection > 0 && PresetSelection < static_cast<int>(PresetNames.size()))
			? PresetNames[PresetSelection] : std::string();

	ScanPresetFolder();

	// Re-match the selection BY NAME: menu indices are positional, and a folder
	// change must not silently re-point the selection at a different preset.
	// Vanished -> None, no recall.
	int newSelection = 0;
	if (!current.empty()) {
		for (size_t i = 1; i < PresetNames.size(); i++) {
			if (PresetNames[i] == current) {
				newSelection = static_cast<int>(i);
				break;
			}
		}
	}
	PresetSelection = newSelection;

	ApplyPresetElements(false);
	RaiseParamEvent(PresetParamID, FF_EVENT_FLAG_ELEMENTS | FF_EVENT_FLAG_VALUE);
}

void FFGLTouchEnginePluginBase::StepPresetMorph() {
	if (!EnablePresetControls) {
		return;
	}
	MorphFrameCounter++;
	RenderedOnce = true;

	// dt between RENDERED frames, not wall time: a bypassed/ejected clip stops
	// rendering and its glide must pause, not silently finish. The Morpher
	// clamps each step, so the one big delta after a pause spans 0.1s at most.
	const auto now = std::chrono::steady_clock::now();
	float deltaTime = 0.0f;
	if (MorphTickValid) {
		deltaTime = std::chrono::duration<float>(now - LastMorphTick).count();
	}
	LastMorphTick = now;
	MorphTickValid = true;

	if (!PresetMorpher.Active()) {
		return;
	}

	// HSBA channels are collected per quad and flushed once after the step —
	// each applied channel changes the derived RGBA of the whole quad.
	std::set<FFUInt32> touchedQuads;
	PresetMorpher.Step(deltaTime, CurveSelectionParam, [&](unsigned int paramIndex, float value) {
		auto typeIt = ParameterMapType.find(paramIndex);
		if (typeIt == ParameterMapType.end()) {
			return;//Slot vanished in a re-enumeration mid-glide.
		}
		switch (typeIt->second) {
		case FF_TYPE_STANDARD:
			ParameterMapFloat[paramIndex] = DenormalizeFromHost(paramIndex, value);
			DirtyParams.insert(paramIndex);
			RaiseParamEvent(paramIndex, FF_EVENT_FLAG_VALUE);
			break;
		case FF_TYPE_HUE:
		case FF_TYPE_SATURATION:
		case FF_TYPE_BRIGHTNESS:
		{
			const FFUInt32 head = ColorQuadHead(paramIndex);
			ColorQuadHsba[head][paramIndex - head] = value;
			touchedQuads.insert(head);
			break;
		}
		case FF_TYPE_ALPHA:
			if (UseHsbaColorQuads) {
				const FFUInt32 head = ColorQuadHead(paramIndex);
				ColorQuadHsba[head][paramIndex - head] = value;
				touchedQuads.insert(head);
			} else {
				ParameterMapFloat[paramIndex] = value;
				DirtyParams.insert(paramIndex);
				RaiseParamEvent(paramIndex, FF_EVENT_FLAG_VALUE);
			}
			break;
		case FF_TYPE_RED:
		case FF_TYPE_GREEN:
		case FF_TYPE_BLUE:
			ParameterMapFloat[paramIndex] = value;
			DirtyParams.insert(paramIndex);
			RaiseParamEvent(paramIndex, FF_EVENT_FLAG_VALUE);
			break;
		default:
			break;
		}
	});

	for (const FFUInt32 head : touchedQuads) {
		const std::array<float, 4>& hsba = ColorQuadHsba[head];
		float r = 0, g = 0, b = 0;
		drmbt::HsbToRgb(hsba[0], hsba[1], hsba[2], r, g, b);
		const float rgba[4] = { r, g, b, hsba[3] };
		for (FFUInt32 i = 0; i < 4; i++) {
			if (ActiveParams.find(head + i) == ActiveParams.end()) {
				continue;
			}
			ParameterMapFloat[head + i] = rgba[i];
			DirtyParams.insert(head + i);
			RaiseParamEvent(head + i, FF_EVENT_FLAG_VALUE);
		}
	}
}
