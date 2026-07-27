/*============================================================================================================================================
                                                           PAINTSTOREADDRESS.H
============================================================================================================================================*/
// 🧩 The composite key that addresses one per-object paint-layer store. A texture-paint workspace panel used to own a single paint
//    store keyed by its panel key; now every paintable object in that panel owns its OWN store (its own layer stack), so the store
//    map is keyed by the (panel key, object id) pair. This header folds those two 32-bit ids into one 64-bit address so the store
//    map stays a flat std::unordered_map<uint64_t, PaintLayerStore>. Object id follows the surface's draw-slot convention exactly:
//    id 0 is "no object", id 1 is the specimen (Suzanne), id N>1 is the (N-1)th placed geometry — the same numbering the object-id
//    attachment and the gizmo/outliner id->object walk use, so a paint target resolved from HoveredObjectId indexes straight in.
#pragma once
#ifndef FRONTIER_AUTHORING_PAINTING_PAINTSTOREADDRESS_H
#define FRONTIER_AUTHORING_PAINTING_PAINTSTOREADDRESS_H

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Encode a (panel key, object id) pair into the flat 64-bit store address: the panel key occupies the high 32 bits, the object
//    id the low 32. A distinct panel or a distinct object therefore yields a distinct address, so per-object stores never collide.
inline uint64_t EncodePaintStoreAddress(uint32_t PanelKey, uint32_t ObjectId)
{
    return (static_cast<uint64_t>(PanelKey) << 32) | static_cast<uint64_t>(ObjectId);
}

// The panel key half of an encoded store address (the high 32 bits).
inline uint32_t DecodePaintStorePanelKey(uint64_t StoreAddress)
{
    return static_cast<uint32_t>(StoreAddress >> 32);
}

// The object id half of an encoded store address (the low 32 bits).
inline uint32_t DecodePaintStoreObjectId(uint64_t StoreAddress)
{
    return static_cast<uint32_t>(StoreAddress & 0xFFFFFFFFull);
}

} // namespace Frontier

#endif
