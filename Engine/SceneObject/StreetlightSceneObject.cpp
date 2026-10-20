//========================================================================================================================
//                                                  STREETLIGHTSCENEOBJECT.CPP
//========================================================================================================================
// 🧩 Streetlight template. Each part is appended after the parts it links to, so the template is always valid.
//========================================================================================================================

#include "StreetlightSceneObject.h"

namespace Frontier {

SceneObjectPartStore MakeStreetlightSceneObject()
{
    SceneObjectPartStore Store;
    Store.AddPart(SceneObjectPartClass::Plinth);
    Store.AddPart(SceneObjectPartClass::Shaft, 0);
    Store.AddPart(SceneObjectPartClass::Arm, 1);
    Store.AddPart(SceneObjectPartClass::Housing, 2);
    Store.AddPart(SceneObjectPartClass::PhotometricProfile);
    Store.AddPart(SceneObjectPartClass::Emitter, 3, 4);
    return Store;
}

}   // namespace Frontier
