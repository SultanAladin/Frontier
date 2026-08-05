/*==============================================================================================================================================
                                                             CHANNELSLOTTABLE.CPP
==============================================================================================================================================*/
// 🧩 The fourteen channel definitions, the generator catalogue, and the sample seed. Transcribed from the CHANNEL_SLOTS and GENERATOR_CATALOGUE
//    literals in Documentation/Prototypes/ChannelPropertyPanel.html — the ORDER here is the panel's draw order and the add-menu's listing order.

#include "ChannelSlotTable.h"

#include <cstdio>
#include <cstring>

namespace ChannelPropertyValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL TABLES
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Chip swatch tones, written as ImGui ABGR literals (0xAA'BB'GG'RR) so they match the prototype's hex exactly.
    constexpr ImU32 HueCopper     = IM_COL32(0xb8, 0x73, 0x33, 0xff);
    constexpr ImU32 HueViolet     = IM_COL32(0x8b, 0x5c, 0xf6, 0xff);
    constexpr ImU32 HueBlue       = IM_COL32(0x3b, 0x82, 0xf6, 0xff);
    constexpr ImU32 HueGrey       = IM_COL32(0x8a, 0x8a, 0x8a, 0xff);
    constexpr ImU32 HueGreen      = IM_COL32(0x10, 0xb9, 0x81, 0xff);
    constexpr ImU32 HueSlate      = IM_COL32(0x94, 0xa3, 0xb8, 0xff);
    constexpr ImU32 HueAmber      = IM_COL32(0xf5, 0x9e, 0x0b, 0xff);
    constexpr ImU32 HueGraphite   = IM_COL32(0x6b, 0x72, 0x80, 0xff);
    constexpr ImU32 HueCyan       = IM_COL32(0x22, 0xd3, 0xee, 0xff);
    constexpr ImU32 HueAzure      = IM_COL32(0x0e, 0xa5, 0xe9, 0xff);
    constexpr ImU32 HueIce        = IM_COL32(0xe2, 0xe8, 0xf0, 0xff);
    constexpr ImU32 HueLavender   = IM_COL32(0xa7, 0x8b, 0xfa, 0xff);
    constexpr ImU32 HuePink       = IM_COL32(0xf4, 0x72, 0xb6, 0xff);
    constexpr ImU32 HueRose       = IM_COL32(0xfb, 0x71, 0x85, 0xff);

    // ⚠️ Row order IS the panel's draw order. Two rows break the 0..1 assumption every other scalar shares:
    //    Anisotropy Angle spans 0..360 degrees, and Refraction Index floors at 1.0 — every fraction, clamp and
    //    preview swatch normalises against the slot's own span rather than assuming unit range.
    const ChannelSlot SlotTable[ChannelSlotCount] =
    {
        { "Base Colour",       "Colour atlas \xC2\xB7 RGB",   ChannelGroup::Surface,     ChannelEditor::Colour,  0.0f,   0.0f, "%.2f", "-",            HueCopper,   false },
        { "Metallic",          "Material atlas \xC2\xB7 R",   ChannelGroup::Surface,     ChannelEditor::Scalar,  0.0f,   1.0f, "%.2f", "-",            HueViolet,   true  },
        { "Roughness",         "Material atlas \xC2\xB7 G",   ChannelGroup::Surface,     ChannelEditor::Scalar,  0.0f,   1.0f, "%.2f", "-",            HueBlue,     true  },
        { "Height",            "Material atlas \xC2\xB7 B",   ChannelGroup::Surface,     ChannelEditor::Scalar,  0.0f,   1.0f, "%.2f", "-",            HueGrey,     true  },
        { "Normal",            "No storage \xC2\xB7 derived", ChannelGroup::Surface,     ChannelEditor::Derived, 0.0f,   0.0f, "%.2f", "-",            HueGreen,    true  },
        { "Opacity",           "Material atlas \xC2\xB7 A",   ChannelGroup::Surface,     ChannelEditor::Scalar,  0.0f,   1.0f, "%.2f", "-",            HueSlate,    true  },

        { "Emissive",          "Emissive atlas \xC2\xB7 RGB", ChannelGroup::Radiance,    ChannelEditor::Colour,  0.0f,   0.0f, "%.2f", "-",            HueAmber,    true  },
        { "Ambient Occlusion", "Emissive atlas \xC2\xB7 A",   ChannelGroup::Radiance,    ChannelEditor::Scalar,  0.0f,   1.0f, "%.2f", "-",            HueGraphite, true  },

        { "Anisotropy",        "Reflect atlas \xC2\xB7 R",    ChannelGroup::Reflectance, ChannelEditor::Scalar,  0.0f,   1.0f, "%.2f", "-",            HueCyan,     true  },
        { "Anisotropy Angle",  "Reflect atlas \xC2\xB7 G",    ChannelGroup::Reflectance, ChannelEditor::Scalar,  0.0f, 360.0f, "%.0f", "\xC2\xB0",    HueAzure,    true  },
        { "Clearcoat",         "Reflect atlas \xC2\xB7 B",    ChannelGroup::Reflectance, ChannelEditor::Scalar,  0.0f,   1.0f, "%.2f", "-",            HueIce,      true  },
        { "Refraction Index",  "Reflect atlas \xC2\xB7 A",    ChannelGroup::Reflectance, ChannelEditor::Scalar,  1.0f,   3.0f, "%.2f", "-",            HueLavender, true  },

        { "Sheen",             "Scatter atlas \xC2\xB7 RGB",  ChannelGroup::Scattering,  ChannelEditor::Colour,  0.0f,   0.0f, "%.2f", "-",            HuePink,     true  },
        { "Subsurface",        "Sheen atlas \xC2\xB7 RGB",    ChannelGroup::Scattering,  ChannelEditor::Colour,  0.0f,   0.0f, "%.2f", "-",            HueRose,     true  }
    };

    // Substance-style generator families. Section is non-null only on the row that opens a group.
    const GeneratorEntry GeneratorTable[GeneratorCount] =
    {
        { "Curvature",         "Convex edge wear",       "Mask",       3, { { "Balance",   0.0f, 1.0f, 0.50f }, { "Contrast", 0.0f, 1.0f, 0.70f }, { "Radius",    0.0f, 1.0f, 0.25f } } },
        { "Ambient Occlusion", "Cavity dirt",            nullptr,      2, { { "Spread",    0.0f, 1.0f, 0.40f }, { "Contrast", 0.0f, 1.0f, 0.60f }, { "",          0.0f, 0.0f, 0.00f } } },
        { "Thickness",         "Translucent falloff",    nullptr,      2, { { "Depth",     0.0f, 1.0f, 0.50f }, { "Contrast", 0.0f, 1.0f, 0.50f }, { "",          0.0f, 0.0f, 0.00f } } },
        { "Position Gradient", "World-axis ramp",        nullptr,      2, { { "Origin",    0.0f, 1.0f, 0.50f }, { "Falloff",  0.0f, 1.0f, 0.35f }, { "",          0.0f, 0.0f, 0.00f } } },

        { "Metal Edge Wear",   "Curvature + grunge",     "Wear",       3, { { "Intensity", 0.0f, 1.0f, 0.60f }, { "Softness", 0.0f, 1.0f, 0.30f }, { "Grain",     0.0f, 1.0f, 0.45f } } },
        { "Dirt",              "Occlusion-driven",       nullptr,      2, { { "Amount",    0.0f, 1.0f, 0.50f }, { "Scale",    0.0f, 1.0f, 0.30f }, { "",          0.0f, 0.0f, 0.00f } } },
        { "Water Runoff",      "Gravity streaks",        nullptr,      3, { { "Length",    0.0f, 1.0f, 0.55f }, { "Density",  0.0f, 1.0f, 0.40f }, { "Gravity",   0.0f, 1.0f, 0.80f } } },

        { "Perlin Noise",      "Fractal value noise",    "Procedural", 3, { { "Scale",     0.0f, 1.0f, 0.40f }, { "Octaves",  0.0f, 1.0f, 0.50f }, { "Contrast",  0.0f, 1.0f, 0.50f } } },
        { "Voronoi",           "Cellular partition",     nullptr,      2, { { "Density",   0.0f, 1.0f, 0.35f }, { "Jitter",   0.0f, 1.0f, 0.70f }, { "",          0.0f, 0.0f, 0.00f } } },
        { "Brushed Metal",     "Anisotropic streaks",    nullptr,      2, { { "Angle",     0.0f, 1.0f, 0.00f }, { "Grain",    0.0f, 1.0f, 0.60f }, { "",          0.0f, 0.0f, 0.00f } } }
    };
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const ChannelSlot* ResolveChannelSlotTable()
{
    return SlotTable;
}

const GeneratorEntry* ResolveGeneratorCatalogue()
{
    return GeneratorTable;
}

const char* DescribeChannelGroup(ChannelGroup Group)
{
    switch (Group)
    {
        case ChannelGroup::Surface:     return "Surface";
        case ChannelGroup::Radiance:    return "Radiance";
        case ChannelGroup::Reflectance: return "Reflectance";
        case ChannelGroup::Scattering:  return "Scattering";
    }
    return "Surface";
}

const char* DescribeSourceMode(SourceMode Source)
{
    switch (Source)
    {
        case SourceMode::Value:     return "Value";
        case SourceMode::Texture:   return "Texture";
        case SourceMode::Generator: return "Generator";
    }
    return "Value";
}

void ResetGeneratorParameters(ChannelAuthoring& Authoring)
{
    if (Authoring.GeneratorIndex < 0 || Authoring.GeneratorIndex >= GeneratorCount) return;

    const GeneratorEntry& Definition = GeneratorTable[Authoring.GeneratorIndex];
    for (int Ordinal = 0; Ordinal < ParameterCeiling; ++Ordinal)
    {
        Authoring.GeneratorParameters[Ordinal] = (Ordinal < Definition.ParameterCount)
                                               ? Definition.Parameters[Ordinal].Default
                                               : 0.0f;
    }
}

void AssignGenerator(ChannelAuthoring& Authoring, int GeneratorIndex)
{
    if (GeneratorIndex < 0 || GeneratorIndex >= GeneratorCount) return;

    Authoring.GeneratorIndex = GeneratorIndex;
    ResetGeneratorParameters(Authoring);
}

int CountEnabledChannels(const ChannelPropertyState& State)
{
    int Total = 0;
    for (int Ordinal = 0; Ordinal < ChannelSlotCount; ++Ordinal)
    {
        if (State.Channels[Ordinal].Enabled) ++Total;
    }
    return Total;
}

void InitializeChannelPropertySample(ChannelPropertyState& State)
{
    State = ChannelPropertyState{};

    snprintf(State.LayerName, sizeof(State.LayerName), "%s", "Brushed Copper");
    State.Classification = "Material";
    State.Blend          = "Normal";
    State.ChipsExpanded  = true;
    State.AddMenuOpen    = false;

    // 📝 Every record starts disabled, folded, on Value, floored at its slot minimum and mid-grey.
    for (int Ordinal = 0; Ordinal < ChannelSlotCount; ++Ordinal)
    {
        ChannelAuthoring& Authoring = State.Channels[Ordinal];
        const ChannelSlot& Slot     = SlotTable[Ordinal];

        Authoring.Enabled        = false;
        Authoring.Expanded       = false;
        Authoring.Source         = SourceMode::Value;
        Authoring.Amount         = Slot.Minimum;
        Authoring.Tint[0]        = 0.5f;
        Authoring.Tint[1]        = 0.5f;
        Authoring.Tint[2]        = 0.5f;
        Authoring.StrokeCount    = 0;
        Authoring.ImportedBase   = false;
        Authoring.ImportName[0]  = '\0';
        Authoring.ImportWidth    = 0;
        Authoring.ImportHeight   = 0;
        Authoring.ImportFormat   = "";
        Authoring.GeneratorIndex = -1;
        for (int Slot2 = 0; Slot2 < ParameterCeiling; ++Slot2) Authoring.GeneratorParameters[Slot2] = 0.0f;
    }

    // -- The opening pose: nine channels live, so every control type is on screen at once -----------------------------
    //    Ordinals follow SlotTable: 0 baseColour · 1 metallic · 2 roughness · 3 height · 4 normal · 5 opacity
    //                               6 emissive   · 7 ambientOcclusion · 8 anisotropy · 9 anisotropyAngle · 10 clearcoat
    const int Live[] = { 0, 1, 2, 3, 4, 7, 8, 9, 10 };
    for (int Entry : Live) State.Channels[Entry].Enabled = true;

    // Base Colour — painted strokes WITH an imported base beneath them, and the card open.
    ChannelAuthoring& BaseColour = State.Channels[0];
    BaseColour.Source       = SourceMode::Texture;
    BaseColour.Expanded     = true;
    BaseColour.StrokeCount  = 148;
    BaseColour.ImportedBase = true;
    snprintf(BaseColour.ImportName, sizeof(BaseColour.ImportName), "%s", "copper_basecolour.png");
    BaseColour.ImportWidth  = 2048;
    BaseColour.ImportHeight = 2048;
    BaseColour.ImportFormat = "sRGB 8";
    BaseColour.Tint[0]      = 0.722f;   // #b87333
    BaseColour.Tint[1]      = 0.451f;
    BaseColour.Tint[2]      = 0.200f;

    // Metallic — a flat authored constant, card open.
    State.Channels[1].Amount   = 0.94f;
    State.Channels[1].Expanded = true;

    // Roughness — generator-driven (Metal Edge Wear), card open so the parameter rows show.
    ChannelAuthoring& Roughness = State.Channels[2];
    Roughness.Source   = SourceMode::Generator;
    Roughness.Expanded = true;
    Roughness.Amount   = 0.38f;
    AssignGenerator(Roughness, 4);      // Metal Edge Wear

    // Height — painted, no import, card folded.
    State.Channels[3].Source      = SourceMode::Texture;
    State.Channels[3].Amount      = 0.50f;
    State.Channels[3].StrokeCount = 62;

    // Ambient Occlusion — generator-driven (Ambient Occlusion), folded.
    ChannelAuthoring& Occlusion = State.Channels[7];
    Occlusion.Source = SourceMode::Generator;
    Occlusion.Amount = 1.0f;
    AssignGenerator(Occlusion, 1);      // Ambient Occlusion

    // The remaining live scalars carry the prototype's seeded constants.
    State.Channels[5].Amount  = 1.00f;      // Opacity
    State.Channels[8].Amount  = 0.72f;      // Anisotropy
    State.Channels[9].Amount  = 35.0f;      // Anisotropy Angle  (degrees)
    State.Channels[10].Amount = 0.15f;      // Clearcoat
    State.Channels[11].Amount = 1.45f;      // Refraction Index

    // Seeded colour channels.
    State.Channels[6].Tint[0]  = 0.0f;  State.Channels[6].Tint[1]  = 0.0f;  State.Channels[6].Tint[2]  = 0.0f;   // Emissive
    State.Channels[12].Tint[0] = 0.227f; State.Channels[12].Tint[1] = 0.227f; State.Channels[12].Tint[2] = 0.227f; // Sheen
    State.Channels[13].Tint[0] = 0.851f; State.Channels[13].Tint[1] = 0.541f; State.Channels[13].Tint[2] = 0.447f; // Subsurface
}

}   // namespace ChannelPropertyValidation
