//========================================================================================================================
//                                                  STREETLIGHTSCENEOBJECT.H
//========================================================================================================================
// 🧩 The Streetlight template: a SceneObject with base, pole, attachments, headlamp, light and IES profile.
//========================================================================================================================
#pragma once

#include "SceneObjectPartStore.h"

namespace Frontier {

// 📝 Builds the six parts in build order with the prototype's defaults. Entry order:
//    0 PlinthPart · 1 ShaftPart (host 0) · 2 ArmPart (host 1) · 3 HousingPart (host 2)
//    4 PhotometricProfile (data) · 5 EmitterPart (host 3, data 4)
[[nodiscard]] SceneObjectPartStore MakeStreetlightSceneObject();

}   // namespace Frontier
