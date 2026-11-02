#pragma once
#include "PluginAPI.h"
class TESObjectREFR;
class TESForm;
class NiObject;
class BSFixedString;
// By-value declarations required to lay out the published vtable. Body Slots
// calls only GetBuildNumber and the no-argument phase callback registration.
struct NiPoint3 { float x, y, z; };
struct NiMatrix33 { float entry[3][3]; };
struct NiTransform { NiMatrix33 rotate; NiPoint3 translate; float scale; };
static_assert(sizeof(NiTransform) == 52);
