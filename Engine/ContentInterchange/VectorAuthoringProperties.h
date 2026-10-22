/*============================================================================================================================================
                                                       VECTORAUTHORINGPROPERTIES.H
============================================================================================================================================*/
// 🧩 Shared channel contributions, changed-field patches and enclosure properties without vector or workspace dependencies.

#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     CHANNEL AND ENCLOSURE PROPERTIES
//------------------------------------------------------------------------------------------------------------------------

enum class VectorAuthoringSurfaceChannel : uint32_t
{
    Colour, Height, Normal, Roughness, Metalness, Opacity, Occlusion
};
enum class VectorAuthoringSurfaceStructure : uint32_t
{
    Freeform, Stitch, Weave
};
enum class VectorAuthoringTextileGeometry : uint32_t
{
    Unclassified, NeedleEntry, StitchThread, UpperThreadPass, WeaveYarn
};
struct VectorAuthoringChannelContribution
{
    bool  Enabled  = true;
    float Strength = 1;
    bool operator==(const VectorAuthoringChannelContribution &) const = default;
};
struct VectorAuthoringEnclosure
{
    uint32_t    Token                   = 0;
    uint32_t    EnclosureToken          = 0;
    std::string Name;
    bool        FolderEnabled           = false;
    bool        VisibilityEnabled       = true;
    bool        LockEnabled             = false;
    bool        GroupWideEditingEnabled = false;
    bool operator==(const VectorAuthoringEnclosure &) const = default;
};
using VectorAuthoringSurfaceChannels = std::array<VectorAuthoringChannelContribution, 7>;

struct VectorAuthoringChannelPatch
{
    std::array<std::optional<bool>, 7>  Enabled;
    std::array<std::optional<float>, 7> Strength;
};

constexpr VectorAuthoringSurfaceChannels ConstructVectorAuthoringSurfaceChannels() noexcept
{
    VectorAuthoringSurfaceChannels Contributions{};
    Contributions.back().Enabled = false;
    return Contributions;
}

void ValidateVectorAuthoringSurfaceChannels(const VectorAuthoringSurfaceChannels &Channels);
void ValidateVectorAuthoringChannelPatch(const VectorAuthoringChannelPatch &Patch);
bool ApplyVectorAuthoringSurfacePatch(VectorAuthoringSurfaceChannels &Channels, const VectorAuthoringChannelPatch &Patch);
}
