/*==============================================================================================================================================
                                                             PAINTLAYERSTORE.CPP
==============================================================================================================================================*/
// 🧩 The vocabulary tables and the sequence mutations behind the paint layer stack. Every table below is a literal transcription of the
//    prototype's SCHEMA block, in the same order, so an ordinal here and an ordinal there always name the same thing.
//
// 🔴 The channel order is LOAD-BEARING. Chips, the add menu, the inspector card list and the metadata counts all walk it, and the history
//    log stores a channel ORDINAL rather than a key — reorder this table and every seeded revision points at the wrong channel.
//
// 📝 Revision stamps are a monotone minute counter, not a wall clock. The seed dates its entries backwards from one session origin and each
//    later record steps forward by a minute, which reproduces the prototype's ordered timeline without pulling in a clock the port has no
//    reason to read.

#include "PaintLayerStore.h"

#include <cstring>
#include <cstdio>

namespace PaintLayerSequenceValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // -- Channel ordinals, named so the seed below reads as the prototype does ------------------------------------------
    constexpr int ChBaseColour       =  0;   // [idx] - baseColour
    constexpr int ChMetallic         =  1;   // [idx] - metallic
    constexpr int ChRoughness        =  2;   // [idx] - roughness
    constexpr int ChHeight           =  3;   // [idx] - height
    constexpr int ChNormal           =  4;   // [idx] - normal
    constexpr int ChOpacity          =  5;   // [idx] - opacity
    constexpr int ChEmission         =  6;   // [idx] - emission
    constexpr int ChAmbientOcclusion =  7;   // [idx] - ambientOcclusion
    constexpr int ChAnisotropy       =  8;   // [idx] - anisotropy
    constexpr int ChAnisotropyAngle  =  9;   // [idx] - anisotropyAngle
    constexpr int ChClearcoat        = 10;   // [idx] - clearcoat
    constexpr int ChRefractionIndex  = 11;   // [idx] - refractionIndex
    constexpr int ChSheen            = 12;   // [idx] - sheen
    constexpr int ChSubsurface       = 13;   // [idx] - subsurface

    // -- Generator ordinals ----------------------------------------------------------------------------------------------
    constexpr int GenCurvatureEdges   = 0;   // [idx] - CurvatureEdges
    constexpr int GenAmbientOcclusion = 1;   // [idx] - AmbientOcclusion
    constexpr int GenMetalEdgeWear    = 2;   // [idx] - MetalEdgeWear
    constexpr int GenDirtAccumulation = 3;   // [idx] - DirtAccumulation
    constexpr int GenPerlinNoise      = 4;   // [idx] - PerlinNoise

    constexpr int SessionMinuteOrigin = 14 * 60;   // [-] - Clock the seeded revisions are dated back from

    // 📝 One shared cursor: the seed walks it backwards, every later record steps it forward. Reset on each reseed.
    int LiveMinuteStamp = SessionMinuteOrigin;

    // 📝 Token counter behind the "L3" identity shown in the metadata rows. Reset on each reseed.
    int LiveTokenCounter = 0;

    void CopyText(char* Target, int Capacity, const char* Source)
    {
        if (Target == nullptr || Capacity <= 0) return;
        if (Source == nullptr) { Target[0] = '\0'; return; }
        int Written = 0;
        while (Written < Capacity - 1 && Source[Written] != '\0') { Target[Written] = Source[Written]; ++Written; }
        Target[Written] = '\0';
    }

    void ResolveClock(int MinutesSinceMidnight, int& Hour, int& Minute)
    {
        int Wrapped = MinutesSinceMidnight % (24 * 60);
        if (Wrapped < 0) { Wrapped += 24 * 60; }
        Hour   = Wrapped / 60;
        Minute = Wrapped % 60;
    }

    // 📝 Every authoring record a fresh layer starts from: no source chosen beyond Value, nothing painted, nothing imported.
    void ResetChannelAuthoring(ChannelAuthoring& Authoring, const ChannelDefinition& Definition)
    {
        Authoring                  = ChannelAuthoring{};
        Authoring.Enabled          = false;
        Authoring.Source           = ChannelSource::Value;
        Authoring.Amount           = Definition.Minimum;
        Authoring.AmountAuthored   = false;
        Authoring.Tint[0]          = 0.5f;
        Authoring.Tint[1]          = 0.5f;
        Authoring.Tint[2]          = 0.5f;
        Authoring.TintAuthored     = false;
        Authoring.StrokeCount      = 0;
        Authoring.TexturePresent   = false;
        Authoring.TextureName[0]   = '\0';
        Authoring.TextureWidth     = 0;
        Authoring.TextureHeight    = 0;
        Authoring.TextureFormat[0] = '\0';
        Authoring.GeneratorOrdinal = -1;
        for (int Index = 0; Index < GeneratorParameterMax; ++Index) { Authoring.GeneratorParameters[Index] = 0.5f; }
    }

    // 📝 The prototype's mkLayer defaults, verbatim.
    void ResetLayer(PaintLayerEntry& Layer)
    {
        const ChannelDefinition* Table = ResolveChannelTable();

        CopyText(Layer.Name, NameCapacity, "Layer");
        char Token[TokenCapacity];
        snprintf(Token, TokenCapacity, "L%d", ++LiveTokenCounter);
        CopyText(Layer.Token, TokenCapacity, Token);

        Layer.CategoryOrdinal = 0;
        Layer.BlendOrdinal    = 0;
        Layer.Opacity         = 100.0f;
        Layer.Shown           = true;
        Layer.Expanded        = false;
        Layer.MaterialExpanded = true;
        Layer.MaskExpanded     = true;
        Layer.Tag             = IM_COL32(148, 163, 184, 255);
        Layer.Paint           = IM_COL32( 76,  79,  83, 255);
        CopyText(Layer.PaintHex, 8, "#4c4f53");

        for (int Index = 0; Index < ChannelCount; ++Index) { ResetChannelAuthoring(Layer.Channels[Index], Table[Index]); }
        Layer.Channels[ChBaseColour].Enabled = true;
        Layer.Channels[ChRoughness].Enabled  = true;
        Layer.Channels[ChNormal].Enabled     = true;

        Layer.Mask                  = MaskAuthoring{};
        Layer.Mask.Enabled          = false;
        Layer.Mask.Base             = MaskBase::White;
        Layer.Mask.Inverted         = false;
        Layer.Mask.Strength         = 100.0f;
        Layer.Mask.Source           = MaskSource::Solid;
        Layer.Mask.GeneratorOrdinal = GenPerlinNoise;
        Layer.Mask.GeneratorParameters[0] = 0.4f;
        Layer.Mask.GeneratorParameters[1] = 0.5f;
        Layer.Mask.GeneratorParameters[2] = 0.5f;

        Layer.HistoryCount = 0;
        Layer.Cursor       = -1;
    }

    // 📝 Seeding path: dates one revision backwards from the session origin instead of stepping the live cursor forward.
    void SeedRevision(PaintLayerEntry& Layer,
                      HistoryCategory  Category,
                      HistoryScope     Scope,
                      int              ScopeChannel,
                      const char*      Title,
                      const char*      Detail,
                      int              MinutesAgo)
    {
        if (Layer.HistoryCount >= HistoryCapacity) return;
        HistoryEntry& Entry = Layer.History[Layer.HistoryCount++];
        Entry.Category      = Category;
        Entry.Scope         = Scope;
        Entry.ScopeChannel  = ScopeChannel;
        CopyText(Entry.Title,  TitleCapacity,  Title);
        CopyText(Entry.Detail, DetailCapacity, Detail);
        ResolveClock(SessionMinuteOrigin - MinutesAgo, Entry.Hour, Entry.Minute);
        Layer.Cursor = Layer.HistoryCount - 1;
    }

    void AssignTint(ChannelAuthoring& Authoring, float Red, float Green, float Blue)
    {
        Authoring.Tint[0]      = Red;
        Authoring.Tint[1]      = Green;
        Authoring.Tint[2]      = Blue;
        Authoring.TintAuthored = true;
    }

    void AssignAmount(ChannelAuthoring& Authoring, float Amount)
    {
        Authoring.Amount         = Amount;
        Authoring.AmountAuthored = true;
    }

    void AssignTexture(ChannelAuthoring& Authoring, const char* Name, int Width, int Height, const char* Format)
    {
        Authoring.TexturePresent = true;
        CopyText(Authoring.TextureName, TextureNameCapacity, Name);
        Authoring.TextureWidth  = Width;
        Authoring.TextureHeight = Height;
        CopyText(Authoring.TextureFormat, FormatCapacity, Format);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          VOCABULARY
//------------------------------------------------------------------------------------------------------------------------

const ChannelDefinition* ResolveChannelTable()
{
    // 🔴 Fourteen rows, fixed order — see the file header. Unit null means the scalar reads as a 0-100 percentage.
    static const ChannelDefinition Table[ChannelCount] =
    {
        { "baseColour",       "Base Colour",       "Colour atlas \xC2\xB7 RGB",   ChannelSection::Surface,     ChannelEditor::Colour,  0.0f, 1.0f,   nullptr, IM_COL32(184, 115,  51, 255), false },
        { "metallic",         "Metallic",          "Material atlas \xC2\xB7 R",   ChannelSection::Surface,     ChannelEditor::Scalar,  0.0f, 1.0f,   nullptr, IM_COL32(139,  92, 246, 255), true  },
        { "roughness",        "Roughness",         "Material atlas \xC2\xB7 G",   ChannelSection::Surface,     ChannelEditor::Scalar,  0.0f, 1.0f,   nullptr, IM_COL32( 59, 130, 246, 255), true  },
        { "height",           "Height",            "Material atlas \xC2\xB7 B",   ChannelSection::Surface,     ChannelEditor::Scalar,  0.0f, 1.0f,   nullptr, IM_COL32(138, 138, 138, 255), true  },
        { "normal",           "Normal",            "No storage \xC2\xB7 derived", ChannelSection::Surface,     ChannelEditor::Derived, 0.0f, 1.0f,   nullptr, IM_COL32( 16, 185, 129, 255), true  },
        { "opacity",          "Opacity",           "Material atlas \xC2\xB7 A",   ChannelSection::Surface,     ChannelEditor::Scalar,  0.0f, 1.0f,   nullptr, IM_COL32(148, 163, 184, 255), true  },
        { "emission",         "Emissive",          "Emissive atlas \xC2\xB7 RGB", ChannelSection::Radiance,    ChannelEditor::Colour,  0.0f, 1.0f,   nullptr, IM_COL32(245, 158,  11, 255), true  },
        { "ambientOcclusion", "Ambient Occlusion", "Emissive atlas \xC2\xB7 A",   ChannelSection::Radiance,    ChannelEditor::Scalar,  0.0f, 1.0f,   nullptr, IM_COL32(107, 114, 128, 255), true  },
        { "anisotropy",       "Anisotropy",        "Reflect atlas \xC2\xB7 R",    ChannelSection::Reflectance, ChannelEditor::Scalar,  0.0f, 1.0f,   nullptr, IM_COL32( 34, 211, 238, 255), true  },
        { "anisotropyAngle",  "Anisotropy Angle",  "Reflect atlas \xC2\xB7 G",    ChannelSection::Reflectance, ChannelEditor::Scalar,  0.0f, 360.0f, "\xC2\xB0", IM_COL32( 14, 165, 233, 255), true },
        { "clearcoat",        "Clearcoat",         "Reflect atlas \xC2\xB7 B",    ChannelSection::Reflectance, ChannelEditor::Scalar,  0.0f, 1.0f,   nullptr, IM_COL32(226, 232, 240, 255), true  },
        { "refractionIndex",  "Refraction Index",  "Reflect atlas \xC2\xB7 A",    ChannelSection::Reflectance, ChannelEditor::Scalar,  1.0f, 3.0f,   "ior",   IM_COL32(167, 139, 250, 255), true  },
        { "sheen",            "Sheen",             "Scatter atlas \xC2\xB7 RGB",  ChannelSection::Scattering,  ChannelEditor::Colour,  0.0f, 1.0f,   nullptr, IM_COL32(244, 114, 182, 255), true  },
        { "subsurface",       "Subsurface",        "Sheen atlas \xC2\xB7 RGB",    ChannelSection::Scattering,  ChannelEditor::Colour,  0.0f, 1.0f,   nullptr, IM_COL32(251, 113, 133, 255), true  }
    };
    return Table;
}

const GeneratorDefinition* ResolveGeneratorTable()
{
    static const GeneratorDefinition Table[GeneratorCount] =
    {
        { "Curvature",         "Convex edge wear",    "Mask",       { { "Balance",   0.50f }, { "Contrast", 0.70f }, { "Radius",   0.25f } }, 3 },
        { "Ambient Occlusion", "Cavity dirt",         "Mask",       { { "Spread",    0.40f }, { "Contrast", 0.60f }, { nullptr,    0.00f } }, 2 },
        { "Metal Edge Wear",   "Curvature + grunge",  "Wear",       { { "Intensity", 0.60f }, { "Softness", 0.30f }, { "Grain",    0.45f } }, 3 },
        { "Dirt",              "Occlusion-driven",    "Wear",       { { "Amount",    0.50f }, { "Scale",    0.30f }, { nullptr,    0.00f } }, 2 },
        { "Perlin Noise",      "Fractal value noise", "Procedural", { { "Scale",     0.40f }, { "Octaves",  0.50f }, { "Contrast", 0.50f } }, 3 }
    };
    return Table;
}

const LayerCategoryDefinition* ResolveLayerCategoryTable()
{
    static const LayerCategoryDefinition Table[LayerCategoryCount] =
    {
        { "Paint",     "Accepts brush strokes",     IM_COL32(249, 115,  22, 255) },
        { "Fill",      "Uniform authored values",   IM_COL32( 59, 130, 246, 255) },
        { "Material",  "A PBR preset, flooded",     IM_COL32(139,  92, 246, 255) },
        { "Generator", "Procedural over the atlas", IM_COL32( 16, 185, 129, 255) }
    };
    return Table;
}

const HistoryCategoryDefinition* ResolveHistoryCategoryTable()
{
    static const HistoryCategoryDefinition Table[HistoryCategoryCount] =
    {
        { "Stroke",  IM_COL32(249, 115,  22, 255) },
        { "Channel", IM_COL32( 59, 130, 246, 255) },
        { "Mask",    IM_COL32(139,  92, 246, 255) },
        { "Gen",     IM_COL32( 16, 185, 129, 255) },
        { "Texture", IM_COL32(245, 158,  11, 255) },
        { "Edit",    IM_COL32(148, 163, 184, 255) },
        { "Create",  IM_COL32( 34, 211, 238, 255) }
    };
    return Table;
}

const char* ResolveBlendName(int Ordinal)
{
    static const char* Names[BlendCount] =
    {
        "Normal", "Multiply", "Screen", "Overlay", "Add", "Darken", "Linear Dodge"
    };
    if (Ordinal < 0 || Ordinal >= BlendCount) { return Names[0]; }
    return Names[Ordinal];
}

const char* ResolveChannelSectionName(ChannelSection Section)
{
    static const char* Names[ChannelGroupCount] = { "Surface", "Radiance", "Reflectance", "Scattering" };
    const int Ordinal = (int)Section;
    if (Ordinal < 0 || Ordinal >= ChannelGroupCount) { return Names[0]; }
    return Names[Ordinal];
}

const char* ResolveGeneratorSectionName(int Ordinal)
{
    static const char* Names[GeneratorGroupCount] = { "Mask", "Wear", "Procedural" };
    if (Ordinal < 0 || Ordinal >= GeneratorGroupCount) { return Names[0]; }
    return Names[Ordinal];
}

int ResolveChannelOrdinal(const char* Key)
{
    if (Key == nullptr) return -1;
    const ChannelDefinition* Table = ResolveChannelTable();
    for (int Index = 0; Index < ChannelCount; ++Index)
    {
        if (strcmp(Table[Index].Key, Key) == 0) { return Index; }
    }
    return -1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        DERIVED READS
//------------------------------------------------------------------------------------------------------------------------

PaintLayerEntry* ResolveFocusedLayer(PaintLayerSequenceState& Sequence)
{
    if (Sequence.LayerCount <= 0) return nullptr;
    if (Sequence.Focus < 0 || Sequence.Focus >= Sequence.LayerCount) { Sequence.Focus = 0; }
    return &Sequence.Layers[Sequence.Focus];
}

const PaintLayerEntry* ResolveFocusedLayer(const PaintLayerSequenceState& Sequence)
{
    if (Sequence.LayerCount <= 0) return nullptr;
    const int Ordinal = (Sequence.Focus < 0 || Sequence.Focus >= Sequence.LayerCount) ? 0 : Sequence.Focus;
    return &Sequence.Layers[Ordinal];
}

ChannelSource ResolveChannelSource(const PaintLayerEntry& Layer, int ChannelOrdinal)
{
    if (ChannelOrdinal < 0 || ChannelOrdinal >= ChannelCount) { return ChannelSource::Value; }
    // 📝 A derived channel has no storage and no authored value, so it never reports anything but Value.
    if (ResolveChannelTable()[ChannelOrdinal].Editor == ChannelEditor::Derived) { return ChannelSource::Value; }
    return Layer.Channels[ChannelOrdinal].Source;
}

int CountEnabledChannels(const PaintLayerEntry& Layer)
{
    int Total = 0;
    for (int Index = 0; Index < ChannelCount; ++Index)
    {
        if (Layer.Channels[Index].Enabled) { ++Total; }
    }
    return Total;
}

int AccumulateStrokeCount(const PaintLayerEntry& Layer)
{
    int Total = 0;
    for (int Index = 0; Index < ChannelCount; ++Index) { Total += Layer.Channels[Index].StrokeCount; }
    return Total;
}

int CountImportedTextures(const PaintLayerEntry& Layer)
{
    int Total = 0;
    for (int Index = 0; Index < ChannelCount; ++Index)
    {
        if (Layer.Channels[Index].TexturePresent) { ++Total; }
    }
    return Total;
}

int CountAssignedGenerators(const PaintLayerEntry& Layer)
{
    int Total = 0;
    for (int Index = 0; Index < ChannelCount; ++Index)
    {
        if (Layer.Channels[Index].GeneratorOrdinal >= 0) { ++Total; }
    }
    return Total;
}

float ResolveMaskTone(const MaskAuthoring& Mask)
{
    const float Base = (Mask.Base == MaskBase::Black) ? 0.0f : 1.0f;
    return Mask.Inverted ? (1.0f - Base) : Base;
}

int CountScopedRevisions(const PaintLayerEntry& Layer, HistoryScope Scope, int ScopeChannel)
{
    int Total = 0;
    for (int Index = 0; Index < Layer.HistoryCount; ++Index)
    {
        const HistoryEntry& Entry = Layer.History[Index];
        if (Entry.Scope != Scope) continue;
        if (Scope == HistoryScope::ChannelSlot && Entry.ScopeChannel != ScopeChannel) continue;
        ++Total;
    }
    return Total;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          MUTATION
//------------------------------------------------------------------------------------------------------------------------

void RecordRevision(PaintLayerEntry&  Layer,
                    HistoryCategory   Category,
                    HistoryScope      Scope,
                    int               ScopeChannel,
                    const char*       Title,
                    const char*       Detail)
{
    // 📝 Recording behind the cursor drops the redo tail — the log is linear, exactly as the source keeps it.
    if (Layer.Cursor < Layer.HistoryCount - 1) { Layer.HistoryCount = Layer.Cursor + 1; }

    // 🔴 A full log discards its OLDEST revision rather than refusing the newest: losing the edit that just happened would leave the
    //    timeline describing a layer that no longer matches what is on screen.
    if (Layer.HistoryCount >= HistoryCapacity)
    {
        for (int Index = 1; Index < HistoryCapacity; ++Index) { Layer.History[Index - 1] = Layer.History[Index]; }
        Layer.HistoryCount = HistoryCapacity - 1;
    }

    HistoryEntry& Entry = Layer.History[Layer.HistoryCount++];
    Entry.Category      = Category;
    Entry.Scope         = Scope;
    Entry.ScopeChannel  = ScopeChannel;
    CopyText(Entry.Title,  TitleCapacity,  Title);
    CopyText(Entry.Detail, DetailCapacity, Detail);
    ResolveClock(++LiveMinuteStamp, Entry.Hour, Entry.Minute);
    Layer.Cursor = Layer.HistoryCount - 1;
}

void AlignChannelGenerator(ChannelAuthoring& Authoring, int GeneratorOrdinal)
{
    if (GeneratorOrdinal < 0 || GeneratorOrdinal >= GeneratorCount)
    {
        Authoring.GeneratorOrdinal = -1;
        return;
    }
    const GeneratorDefinition& Definition = ResolveGeneratorTable()[GeneratorOrdinal];
    Authoring.GeneratorOrdinal = GeneratorOrdinal;
    for (int Index = 0; Index < GeneratorParameterMax; ++Index)
    {
        Authoring.GeneratorParameters[Index] = (Index < Definition.ParameterCount) ? Definition.Parameters[Index].Default : 0.5f;
    }
}

void AlignMaskGenerator(MaskAuthoring& Mask, int GeneratorOrdinal)
{
    if (GeneratorOrdinal < 0 || GeneratorOrdinal >= GeneratorCount) return;
    const GeneratorDefinition& Definition = ResolveGeneratorTable()[GeneratorOrdinal];
    Mask.GeneratorOrdinal = GeneratorOrdinal;
    for (int Index = 0; Index < GeneratorParameterMax; ++Index)
    {
        Mask.GeneratorParameters[Index] = (Index < Definition.ParameterCount) ? Definition.Parameters[Index].Default : 0.5f;
    }
}

void ConstructLayer(PaintLayerSequenceState& Sequence, int CategoryOrdinal)
{
    if (Sequence.LayerCount >= LayerCapacity) return;
    if (CategoryOrdinal < 0 || CategoryOrdinal >= LayerCategoryCount) { CategoryOrdinal = 0; }
    const LayerCategoryDefinition& Category = ResolveLayerCategoryTable()[CategoryOrdinal];

    // 📝 A new layer lands on TOP of the sequence, so every existing entry steps down one slot first.
    for (int Index = Sequence.LayerCount; Index > 0; --Index) { Sequence.Layers[Index] = Sequence.Layers[Index - 1]; }
    ++Sequence.LayerCount;

    PaintLayerEntry& Layer = Sequence.Layers[0];
    ResetLayer(Layer);

    char Name[NameCapacity];
    snprintf(Name, NameCapacity, "New %s", Category.Label);
    CopyText(Layer.Name, NameCapacity, Name);

    Layer.CategoryOrdinal = CategoryOrdinal;
    Layer.Tag             = Category.Tint;
    Layer.Expanded        = true;
    AssignAmount(Layer.Channels[ChRoughness], 0.5f);
    AssignTint(Layer.Channels[ChBaseColour], 76.0f / 255.0f, 79.0f / 255.0f, 83.0f / 255.0f);

    RecordRevision(Layer, HistoryCategory::Create, HistoryScope::Layer, -1, "Layer created", Category.Label);

    Sequence.OpenMenu[0] = '\0';
    Sequence.Focus       = 0;
    Sequence.Face        = InspectorFace::Layer;
}

void DuplicateLayer(PaintLayerSequenceState& Sequence, int Ordinal)
{
    if (Sequence.LayerCount >= LayerCapacity) return;
    if (Ordinal < 0 || Ordinal >= Sequence.LayerCount) return;

    // 📝 The clone takes the source's slot and pushes the source down, matching the prototype's splice(ord, 0, copy).
    for (int Index = Sequence.LayerCount; Index > Ordinal; --Index) { Sequence.Layers[Index] = Sequence.Layers[Index - 1]; }
    ++Sequence.LayerCount;

    PaintLayerEntry&       Clone  = Sequence.Layers[Ordinal];
    const PaintLayerEntry& Source = Sequence.Layers[Ordinal + 1];

    char Name[NameCapacity];
    snprintf(Name, NameCapacity, "%s Copy", Source.Name);
    CopyText(Clone.Name, NameCapacity, Name);

    char Token[TokenCapacity];
    snprintf(Token, TokenCapacity, "L%d", ++LiveTokenCounter);
    CopyText(Clone.Token, TokenCapacity, Token);

    // 📝 The copied revisions are re-dated onto the live cursor so the clone's timeline reads as a fresh run, not as the source's past.
    for (int Index = 0; Index < Clone.HistoryCount; ++Index)
    {
        ResolveClock(++LiveMinuteStamp, Clone.History[Index].Hour, Clone.History[Index].Minute);
    }

    char Detail[DetailCapacity];
    snprintf(Detail, DetailCapacity, "from %s", Source.Name);
    RecordRevision(Clone, HistoryCategory::Create, HistoryScope::Layer, -1, "Duplicated layer", Detail);

    Sequence.OpenMenu[0] = '\0';
    Sequence.Focus       = Ordinal;
    Sequence.Face        = InspectorFace::Layer;
}

void ReclaimLayer(PaintLayerSequenceState& Sequence, int Ordinal)
{
    // 🔴 The last layer is never removed — the surface always resolves against at least one.
    if (Sequence.LayerCount <= 1) return;
    if (Ordinal < 0 || Ordinal >= Sequence.LayerCount) return;

    for (int Index = Ordinal; Index < Sequence.LayerCount - 1; ++Index) { Sequence.Layers[Index] = Sequence.Layers[Index + 1]; }
    --Sequence.LayerCount;

    Sequence.OpenMenu[0] = '\0';
    Sequence.Focus       = (Ordinal - 1) > 0 ? (Ordinal - 1) : 0;
    Sequence.Face        = InspectorFace::Layer;
}

void ReorderLayer(PaintLayerSequenceState& Sequence, int FromOrdinal, int Slot)
{
    if (FromOrdinal < 0 || FromOrdinal >= Sequence.LayerCount) return;
    if (Slot < 0 || Slot > Sequence.LayerCount) return;
    // 📝 The slot is a GAP index, so both the gap above and the gap below the dragged row resolve to no movement.
    if (Slot == FromOrdinal || Slot == FromOrdinal + 1) return;

    const int Target = (Slot > FromOrdinal) ? (Slot - 1) : Slot;

    PaintLayerEntry Lifted = Sequence.Layers[FromOrdinal];
    if (Target > FromOrdinal)
    {
        for (int Index = FromOrdinal; Index < Target; ++Index) { Sequence.Layers[Index] = Sequence.Layers[Index + 1]; }
    }
    else
    {
        for (int Index = FromOrdinal; Index > Target; --Index) { Sequence.Layers[Index] = Sequence.Layers[Index - 1]; }
    }
    Sequence.Layers[Target] = Lifted;

    char Title[TitleCapacity];
    char Detail[DetailCapacity];
    snprintf(Title,  TitleCapacity,  "Reordered to slot %d", Target);
    snprintf(Detail, DetailCapacity, "was %d", FromOrdinal);
    RecordRevision(Sequence.Layers[Target], HistoryCategory::Edit, HistoryScope::Layer, -1, Title, Detail);

    Sequence.Focus = Target;
}

void ConstructMask(PaintLayerSequenceState& Sequence, int Ordinal, MaskSource Source)
{
    if (Ordinal < 0 || Ordinal >= Sequence.LayerCount) return;
    PaintLayerEntry& Layer = Sequence.Layers[Ordinal];

    Layer.Mask.Enabled = true;
    Layer.Mask.Source  = Source;
    Layer.MaskExpanded = true;

    char Detail[DetailCapacity];
    if (Source == MaskSource::Generator)
    {
        const int Assigned = (Layer.Mask.GeneratorOrdinal >= 0) ? Layer.Mask.GeneratorOrdinal : GenPerlinNoise;
        snprintf(Detail, DetailCapacity, "%s", ResolveGeneratorTable()[Assigned].Label);
    }
    else
    {
        snprintf(Detail, DetailCapacity, "%s base", Layer.Mask.Base == MaskBase::Black ? "Black" : "White");
    }
    RecordRevision(Layer, HistoryCategory::Mask, HistoryScope::Mask, -1, "Mask created", Detail);

    Sequence.OpenMenu[0] = '\0';
    Sequence.Focus       = Ordinal;
    Sequence.Face        = InspectorFace::Mask;
}

void ReclaimMask(PaintLayerSequenceState& Sequence, int Ordinal)
{
    if (Ordinal < 0 || Ordinal >= Sequence.LayerCount) return;
    PaintLayerEntry& Layer = Sequence.Layers[Ordinal];

    Layer.Mask.Enabled = false;
    RecordRevision(Layer, HistoryCategory::Mask, HistoryScope::Mask, -1, "Mask removed", "");

    Sequence.OpenMenu[0] = '\0';
    Sequence.Focus       = Ordinal;
    Sequence.Face        = InspectorFace::Layer;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                             SEED
//------------------------------------------------------------------------------------------------------------------------

void InitializePaintLayerSample(PaintLayerSequenceState& Sequence)
{
    LiveMinuteStamp  = SessionMinuteOrigin;
    LiveTokenCounter = 0;

    Sequence            = PaintLayerSequenceState{};
    CopyText(Sequence.SurfaceName, SurfaceCapacity, "Suzanne");
    Sequence.Filter[0]  = '\0';
    Sequence.Focus      = 0;
    Sequence.Face       = InspectorFace::Layer;
    Sequence.Slide      = 1;
    Sequence.OpenMenu[0] = '\0';
    Sequence.Lifted     = -1;
    Sequence.Renaming   = -1;
    Sequence.Timeline   = HistoryFilter::All;
    Sequence.LayerCount = 6;

    // 📝 The channels that open FOLDED in the inspector. Base Colour, Metallic and Roughness stay open — they are what a first look wants.
    for (int Index = 0; Index < ChannelCount; ++Index) { Sequence.ChannelFolded[Index] = true; }
    Sequence.ChannelFolded[ChBaseColour] = false;
    Sequence.ChannelFolded[ChMetallic]   = false;
    Sequence.ChannelFolded[ChRoughness]  = false;

    for (int Index = 0; Index < 6; ++Index) { ResetLayer(Sequence.Layers[Index]); }

    // -- 1 · Edge Wear ----------------------------------------------------------------------------------------------------
    {
        PaintLayerEntry& Layer = Sequence.Layers[0];
        CopyText(Layer.Name, NameCapacity, "Edge Wear");
        Layer.CategoryOrdinal = 0;
        Layer.BlendOrdinal    = 1;
        Layer.Opacity         = 78.0f;
        Layer.Expanded        = true;
        Layer.Tag             = IM_COL32(249, 115,  22, 255);
        Layer.Paint           = IM_COL32( 46,  45,  43, 255);
        CopyText(Layer.PaintHex, 8, "#2e2d2b");

        Layer.Channels[ChMetallic].Enabled         = true;
        Layer.Channels[ChHeight].Enabled           = true;
        Layer.Channels[ChAmbientOcclusion].Enabled = true;
        Layer.Channels[ChAnisotropy].Enabled       = true;
        Layer.Channels[ChClearcoat].Enabled        = true;

        Layer.Channels[ChBaseColour].Source       = ChannelSource::Texture;
        Layer.Channels[ChRoughness].Source        = ChannelSource::Generator;
        Layer.Channels[ChAmbientOcclusion].Source = ChannelSource::Generator;

        AssignTint(Layer.Channels[ChBaseColour], 184.0f / 255.0f, 115.0f / 255.0f, 51.0f / 255.0f);
        AssignAmount(Layer.Channels[ChMetallic],         0.94f);
        AssignAmount(Layer.Channels[ChRoughness],        0.38f);
        AssignAmount(Layer.Channels[ChHeight],           0.50f);
        AssignAmount(Layer.Channels[ChAmbientOcclusion], 1.00f);
        AssignAmount(Layer.Channels[ChAnisotropy],       0.72f);
        AssignAmount(Layer.Channels[ChClearcoat],        0.15f);

        AssignTexture(Layer.Channels[ChBaseColour], "copper_basecolour.png", 2048, 2048, "sRGB 8");
        Layer.Channels[ChBaseColour].StrokeCount = 148;
        Layer.Channels[ChHeight].StrokeCount     = 62;

        AlignChannelGenerator(Layer.Channels[ChRoughness],        GenMetalEdgeWear);
        AlignChannelGenerator(Layer.Channels[ChAmbientOcclusion], GenAmbientOcclusion);

        Layer.Mask.Enabled  = true;
        Layer.Mask.Base     = MaskBase::Black;
        Layer.Mask.Inverted = false;
        Layer.Mask.Strength = 92.0f;
        Layer.Mask.Source   = MaskSource::Generator;
        AlignMaskGenerator(Layer.Mask, GenMetalEdgeWear);

        SeedRevision(Layer, HistoryCategory::Create,    HistoryScope::Layer,       -1,                 "Layer created",                    "Paint layer",     96);
        SeedRevision(Layer, HistoryCategory::Channel,   HistoryScope::ChannelSlot, ChBaseColour,       "Base Colour \xE2\x86\x92 Texture", "copper_basecolour.png", 84);
        SeedRevision(Layer, HistoryCategory::Stroke,    HistoryScope::ChannelSlot, ChBaseColour,       "Painted 96 strokes",               "Round \xC2\xB7 42px", 71);
        SeedRevision(Layer, HistoryCategory::Mask,      HistoryScope::Mask,        -1,                 "Mask created",                     "Black base",      58);
        SeedRevision(Layer, HistoryCategory::Generator, HistoryScope::Mask,        -1,                 "Mask generator \xE2\x86\x92 Metal Edge Wear", "Intensity 0.60", 57);
        SeedRevision(Layer, HistoryCategory::Channel,   HistoryScope::ChannelSlot, ChRoughness,        "Roughness \xE2\x86\x92 Generator", "Metal Edge Wear", 44);
        SeedRevision(Layer, HistoryCategory::Stroke,    HistoryScope::ChannelSlot, ChHeight,           "Painted 62 strokes",               "Height \xC2\xB7 18px", 31);
        SeedRevision(Layer, HistoryCategory::Edit,      HistoryScope::Layer,       -1,                 "Opacity 78%",                      "was 100%",        12);
    }

    // -- 2 · Cavity Dirt --------------------------------------------------------------------------------------------------
    {
        PaintLayerEntry& Layer = Sequence.Layers[1];
        CopyText(Layer.Name, NameCapacity, "Cavity Dirt");
        Layer.CategoryOrdinal = 3;
        Layer.BlendOrdinal    = 1;
        Layer.Opacity         = 55.0f;
        Layer.Shown           = false;
        Layer.Tag             = IM_COL32( 16, 185, 129, 255);
        Layer.Paint           = IM_COL32( 46,  45,  43, 255);
        CopyText(Layer.PaintHex, 8, "#2e2d2b");

        Layer.Channels[ChBaseColour].Enabled       = false;
        Layer.Channels[ChNormal].Enabled           = false;
        Layer.Channels[ChAmbientOcclusion].Enabled = true;

        Layer.Channels[ChRoughness].Source        = ChannelSource::Generator;
        Layer.Channels[ChAmbientOcclusion].Source = ChannelSource::Generator;
        AssignAmount(Layer.Channels[ChRoughness],        0.50f);
        AssignAmount(Layer.Channels[ChAmbientOcclusion], 0.80f);

        AlignChannelGenerator(Layer.Channels[ChAmbientOcclusion], GenAmbientOcclusion);
        AlignChannelGenerator(Layer.Channels[ChRoughness],        GenDirtAccumulation);

        SeedRevision(Layer, HistoryCategory::Create,    HistoryScope::Layer,       -1,                 "Layer created",                      "Generator layer", 74);
        SeedRevision(Layer, HistoryCategory::Generator, HistoryScope::ChannelSlot, ChAmbientOcclusion, "AO \xE2\x86\x92 Ambient Occlusion",  "Spread 0.40",     70);
        SeedRevision(Layer, HistoryCategory::Generator, HistoryScope::ChannelSlot, ChRoughness,        "Roughness \xE2\x86\x92 Dirt",        "Amount 0.50",     63);
        SeedRevision(Layer, HistoryCategory::Edit,      HistoryScope::Layer,       -1,                 "Hidden",                             "visibility off",  21);
    }

    // -- 3 · Hand Detail --------------------------------------------------------------------------------------------------
    {
        PaintLayerEntry& Layer = Sequence.Layers[2];
        CopyText(Layer.Name, NameCapacity, "Hand Detail");
        Layer.CategoryOrdinal = 0;
        Layer.BlendOrdinal    = 0;
        Layer.Tag             = IM_COL32(239,  68,  68, 255);
        Layer.Paint           = IM_COL32(239,  68,  68, 255);
        CopyText(Layer.PaintHex, 8, "#ef4444");

        Layer.Channels[ChNormal].Enabled = false;
        Layer.Channels[ChHeight].Enabled = true;

        Layer.Channels[ChBaseColour].Source = ChannelSource::Texture;
        AssignTint(Layer.Channels[ChBaseColour], 239.0f / 255.0f, 68.0f / 255.0f, 68.0f / 255.0f);
        AssignAmount(Layer.Channels[ChRoughness], 0.40f);
        AssignAmount(Layer.Channels[ChHeight],    0.60f);

        AssignTexture(Layer.Channels[ChBaseColour], "detail_albedo.png", 2048, 2048, "sRGB 8");
        Layer.Channels[ChBaseColour].StrokeCount = 62;

        SeedRevision(Layer, HistoryCategory::Create,  HistoryScope::Layer,       -1,           "Layer created",                "Paint layer",                  52);
        SeedRevision(Layer, HistoryCategory::Texture, HistoryScope::ChannelSlot, ChBaseColour, "Imported detail_albedo.png",   "2048 \xC3\x97 2048 \xC2\xB7 sRGB 8", 50);
        SeedRevision(Layer, HistoryCategory::Stroke,  HistoryScope::ChannelSlot, ChBaseColour, "Painted 62 strokes",           "Round \xC2\xB7 12px",          38);
    }

    // -- 4 · Roughness Lift -----------------------------------------------------------------------------------------------
    {
        PaintLayerEntry& Layer = Sequence.Layers[3];
        CopyText(Layer.Name, NameCapacity, "Roughness Lift");
        Layer.CategoryOrdinal = 1;
        Layer.BlendOrdinal    = 4;
        Layer.Opacity         = 40.0f;
        Layer.Tag             = IM_COL32( 59, 130, 246, 255);

        Layer.Channels[ChBaseColour].Enabled = false;
        Layer.Channels[ChNormal].Enabled     = false;
        AssignAmount(Layer.Channels[ChRoughness], 0.75f);

        SeedRevision(Layer, HistoryCategory::Create,  HistoryScope::Layer,       -1,          "Layer created",                "Fill layer",   40);
        SeedRevision(Layer, HistoryCategory::Channel, HistoryScope::ChannelSlot, ChRoughness, "Roughness 0.75",               "flooded",      39);
        SeedRevision(Layer, HistoryCategory::Edit,    HistoryScope::Layer,       -1,          "Blend \xE2\x86\x92 Add",       "was Normal",   36);
    }

    // -- 5 · Brushed Copper -----------------------------------------------------------------------------------------------
    {
        PaintLayerEntry& Layer = Sequence.Layers[4];
        CopyText(Layer.Name, NameCapacity, "Brushed Copper");
        Layer.CategoryOrdinal = 2;
        Layer.Tag             = IM_COL32(139,  92, 246, 255);
        Layer.Paint           = IM_COL32(184, 115,  51, 255);
        CopyText(Layer.PaintHex, 8, "#b87333");

        Layer.Channels[ChMetallic].Enabled = true;
        Layer.Channels[ChHeight].Enabled   = true;

        Layer.Channels[ChRoughness].Source = ChannelSource::Generator;
        AssignTint(Layer.Channels[ChBaseColour], 184.0f / 255.0f, 115.0f / 255.0f, 51.0f / 255.0f);
        AssignAmount(Layer.Channels[ChMetallic],  0.95f);
        AssignAmount(Layer.Channels[ChRoughness], 0.30f);
        AssignAmount(Layer.Channels[ChHeight],    0.50f);
        AlignChannelGenerator(Layer.Channels[ChRoughness], GenCurvatureEdges);

        Layer.Mask.Enabled  = true;
        Layer.Mask.Base     = MaskBase::White;
        Layer.Mask.Inverted = true;
        Layer.Mask.Strength = 100.0f;
        Layer.Mask.Source   = MaskSource::Generator;
        AlignMaskGenerator(Layer.Mask, GenCurvatureEdges);

        SeedRevision(Layer, HistoryCategory::Create,    HistoryScope::Layer,       -1,          "Layer created",                     "Material preset", 30);
        SeedRevision(Layer, HistoryCategory::Mask,      HistoryScope::Mask,        -1,          "Mask created",                      "White base",      28);
        SeedRevision(Layer, HistoryCategory::Mask,      HistoryScope::Mask,        -1,          "Mask inverted",                     "",                27);
        SeedRevision(Layer, HistoryCategory::Generator, HistoryScope::ChannelSlot, ChRoughness, "Roughness \xE2\x86\x92 Curvature",  "Contrast 0.70",   22);
    }

    // -- 6 · Base Steel ---------------------------------------------------------------------------------------------------
    {
        PaintLayerEntry& Layer = Sequence.Layers[5];
        CopyText(Layer.Name, NameCapacity, "Base Steel");
        Layer.CategoryOrdinal = 2;
        Layer.Tag             = IM_COL32(148, 163, 184, 255);
        Layer.Paint           = IM_COL32( 76,  79,  83, 255);
        CopyText(Layer.PaintHex, 8, "#4c4f53");

        Layer.Channels[ChMetallic].Enabled = true;
        Layer.Channels[ChHeight].Enabled   = true;

        AssignTint(Layer.Channels[ChBaseColour], 76.0f / 255.0f, 79.0f / 255.0f, 83.0f / 255.0f);
        AssignAmount(Layer.Channels[ChMetallic],  0.90f);
        AssignAmount(Layer.Channels[ChRoughness], 0.20f);
        AssignAmount(Layer.Channels[ChHeight],    0.50f);

        SeedRevision(Layer, HistoryCategory::Create,  HistoryScope::Layer,       -1,         "Layer created",   "Material preset", 18);
        SeedRevision(Layer, HistoryCategory::Channel, HistoryScope::ChannelSlot, ChMetallic, "Metallic 0.90",   "flooded",         17);
    }

    // 📝 The seed dated backwards; the live cursor now sits at the newest of those stamps so the next recorded edit lands after them.
    LiveMinuteStamp = SessionMinuteOrigin;
}

}   // namespace PaintLayerSequenceValidation
