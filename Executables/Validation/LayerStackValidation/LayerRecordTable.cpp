/*==============================================================================================================================================
                                                           LAYERRECORDTABLE.CPP
==============================================================================================================================================*/
// 🧩 The four layer kinds, the blend names, the sample seed and the stack verbs. Transcribed from the LAYER_KINDS / LAYER_KIND_ORDER literals in
//    Documentation/Prototypes/PaintingSurface/Layers/LayerKinds.js and BLEND_MODES in Layers/LayerStack.js.

#include "LayerRecordTable.h"

#include <cstdio>
#include <cstring>

namespace LayerStackValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       INTERNAL TABLES
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Kind tints, straight from LayerKinds.js: paint #f97316, fill #3b82f6, material #8b5cf6, generator #10b981.
    const LayerKindEntry KindTable[LayerKindCount] =
    {
        { "Paint",     "Hand-painted. Accepts brush strokes.",              IM_COL32(0xf9, 0x73, 0x16, 0xff), true,  false },
        { "Fill",      "Uniform authored values across the whole surface.", IM_COL32(0x3b, 0x82, 0xf6, 0xff), false, true  },
        { "Material",  "A PBR preset flooded over the surface.",            IM_COL32(0x8b, 0x5c, 0xf6, 0xff), false, true  },
        { "Generator", "Driven by this layer's procedural pass.",           IM_COL32(0x10, 0xb9, 0x81, 0xff), false, true  }
    };

    const char* const BlendTable[BlendModeCount] =
    {
        "Normal", "Multiply", "Screen", "Overlay", "Add", "Darken", "Linear Dodge"
    };

    // 📝 Clear one record to the pose a freshly created layer holds: shown, opaque, folded, no mask.
    void ClearRecord(LayerRecord& Record, LayerKind Kind, const char* Name)
    {
        Record = LayerRecord{};

        snprintf(Record.Name, sizeof(Record.Name), "%s", Name);
        Record.Kind             = Kind;
        Record.BlendIndex       = 0;        // Normal
        Record.Opacity          = 100.0f;
        Record.Shown            = true;
        Record.Expanded         = false;
        Record.Tag[0]           = 0.54f;
        Record.Tag[1]           = 0.54f;
        Record.Tag[2]           = 0.54f;
        Record.Paint[0]         = 0.5f;
        Record.Paint[1]         = 0.5f;
        Record.Paint[2]         = 0.5f;
        Record.PaintsBaseColour = true;
        Record.ChannelTotal     = 3;
        Record.StrokeCount      = 0;

        Record.MaskEnabled    = false;
        Record.MaskFillMode   = MaskFill::White;
        Record.MaskInverted   = false;
        Record.MaskStrength   = 100.0f;
        Record.ComponentTotal = 0;
    }

    // Lowercase one byte without pulling in a locale.
    char FoldCase(char Letter)
    {
        return (Letter >= 'A' && Letter <= 'Z') ? (char)(Letter - 'A' + 'a') : Letter;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const LayerKindEntry* ResolveLayerKindTable()
{
    return KindTable;
}

const char* const* ResolveBlendModeTable()
{
    return BlendTable;
}

const char* DescribeLayerKind(LayerKind Kind)
{
    const int Ordinal = (int)Kind;
    if (Ordinal < 0 || Ordinal >= LayerKindCount) return "Paint";
    return KindTable[Ordinal].Label;
}

int CountHiddenLayers(const LayerStackState& State)
{
    int Total = 0;
    for (int Ordinal = 0; Ordinal < State.LayerTotal; ++Ordinal)
    {
        if (!State.Layers[Ordinal].Shown) ++Total;
    }
    return Total;
}

bool QueryLayerPassesFilter(const LayerRecord& Record, const char* FilterTerm)
{
    if (!FilterTerm || FilterTerm[0] == '\0') return true;

    // Case-folded substring search over the name — the prototype's `.includes()` on a lowered pair.
    for (int Start = 0; Record.Name[Start] != '\0'; ++Start)
    {
        int Step = 0;
        while (FilterTerm[Step] != '\0' &&
               FoldCase(Record.Name[Start + Step]) == FoldCase(FilterTerm[Step]))
        {
            ++Step;
        }
        if (FilterTerm[Step] == '\0') return true;
    }
    return false;
}

void InsertLayerOfKind(LayerStackState& State, LayerKind Kind)
{
    if (State.LayerTotal >= LayerCapacity) return;

    // 🔴 A new layer lands at the TOP of the stack (ordinal 0), so every existing record slides down one.
    //    The prototype's Stack.Add pushes onto the end of a bottom-up array and the rail reverses it; here
    //    the array IS the rail order, so the insert is at the front.
    for (int Ordinal = State.LayerTotal; Ordinal > 0; --Ordinal)
    {
        State.Layers[Ordinal] = State.Layers[Ordinal - 1];
    }
    ++State.LayerTotal;

    char Name[64];
    snprintf(Name, sizeof(Name), "NEW_%s", DescribeLayerKind(Kind));
    ClearRecord(State.Layers[0], Kind, Name);

    // A fill / material / generator authors values rather than accepting strokes.
    const LayerKindEntry& Definition = KindTable[(int)Kind];
    State.Layers[0].PaintsBaseColour = true;
    State.Layers[0].ChannelTotal     = Definition.Paintable ? 3 : 4;

    // Focusing the new row opens its expand, exactly as a row click does.
    State.Layers[0].Expanded = true;
    State.FocusOrdinal       = 0;
    State.RenameOrdinal      = -1;
}

void WithdrawLayer(LayerStackState& State, int Ordinal)
{
    if (Ordinal < 0 || Ordinal >= State.LayerTotal) return;
    if (State.LayerTotal <= 1) return;      // the stack always keeps one layer

    for (int Step = Ordinal; Step < State.LayerTotal - 1; ++Step)
    {
        State.Layers[Step] = State.Layers[Step + 1];
    }
    --State.LayerTotal;

    // 🔴 Re-seat the focus rather than leaving it on a now-shifted ordinal: the record that used to sit at
    //    Ordinal is gone, so a focus left there would silently point at the layer that slid up into its place
    //    only when the removed row was above it, and past the end when it was the last one.
    if (State.FocusOrdinal == Ordinal)
    {
        State.FocusOrdinal = (Ordinal < State.LayerTotal) ? Ordinal : State.LayerTotal - 1;
    }
    else if (State.FocusOrdinal > Ordinal)
    {
        --State.FocusOrdinal;
    }
    State.RenameOrdinal = -1;
}

void RelocateLayer(LayerStackState& State, int FromOrdinal, int ToOrdinal)
{
    if (FromOrdinal < 0 || FromOrdinal >= State.LayerTotal) return;
    if (ToOrdinal   < 0 || ToOrdinal   >= State.LayerTotal) return;
    if (FromOrdinal == ToOrdinal) return;

    const LayerRecord Carried = State.Layers[FromOrdinal];
    if (FromOrdinal < ToOrdinal)
    {
        for (int Step = FromOrdinal; Step < ToOrdinal; ++Step) State.Layers[Step] = State.Layers[Step + 1];
    }
    else
    {
        for (int Step = FromOrdinal; Step > ToOrdinal; --Step) State.Layers[Step] = State.Layers[Step - 1];
    }
    State.Layers[ToOrdinal] = Carried;

    // The focus follows the layer it was on, not the ordinal it used to occupy.
    if (State.FocusOrdinal == FromOrdinal)                                             State.FocusOrdinal = ToOrdinal;
    else if (FromOrdinal < ToOrdinal && State.FocusOrdinal > FromOrdinal &&
             State.FocusOrdinal <= ToOrdinal)                                          --State.FocusOrdinal;
    else if (FromOrdinal > ToOrdinal && State.FocusOrdinal >= ToOrdinal &&
             State.FocusOrdinal < FromOrdinal)                                         ++State.FocusOrdinal;
}

void InitializeLayerStackSample(LayerStackState& State)
{
    State = LayerStackState{};

    snprintf(State.SurfaceName, sizeof(State.SurfaceName), "%s", "Suzanne");
    State.FilterTerm[0] = '\0';
    State.AddListOpen   = false;
    State.RenameOrdinal = -1;

    // -- The opening pose: six layers, top-down, so every kind and both mask poses are on screen --------
    State.LayerTotal = 6;

    // 0 — a paint layer, focused and open, carrying an enabled mask with a component stack.
    ClearRecord(State.Layers[0], LayerKind::Paint, "Edge Wear");
    State.Layers[0].Expanded         = true;
    State.Layers[0].Opacity          = 78.0f;
    State.Layers[0].BlendIndex       = 1;       // Multiply
    State.Layers[0].StrokeCount      = 148;
    State.Layers[0].ChannelTotal     = 3;
    State.Layers[0].Tag[0]           = 0.976f;  // #f97316
    State.Layers[0].Tag[1]           = 0.451f;
    State.Layers[0].Tag[2]           = 0.086f;
    State.Layers[0].Paint[0]         = 0.180f;
    State.Layers[0].Paint[1]         = 0.176f;
    State.Layers[0].Paint[2]         = 0.169f;
    State.Layers[0].MaskEnabled      = true;
    State.Layers[0].MaskFillMode     = MaskFill::Black;
    State.Layers[0].MaskInverted     = false;
    State.Layers[0].MaskStrength     = 92.0f;
    State.Layers[0].ComponentTotal   = 2;
    snprintf(State.Layers[0].Components[0].Label, sizeof(State.Layers[0].Components[0].Label), "%s", "Curvature");
    State.Layers[0].Components[0].Enabled = true;
    State.Layers[0].Components[0].Weight  = 0.72f;
    snprintf(State.Layers[0].Components[1].Label, sizeof(State.Layers[0].Components[1].Label), "%s", "Grunge Noise");
    State.Layers[0].Components[1].Enabled = true;
    State.Layers[0].Components[1].Weight  = 0.35f;

    // 1 — a generator layer, folded, hidden so the muted row styling is visible.
    ClearRecord(State.Layers[1], LayerKind::Generator, "Cavity Dirt");
    State.Layers[1].Shown        = false;
    State.Layers[1].Opacity      = 55.0f;
    State.Layers[1].BlendIndex   = 1;           // Multiply
    State.Layers[1].ChannelTotal = 2;
    State.Layers[1].Tag[0]       = 0.063f;      // #10b981
    State.Layers[1].Tag[1]       = 0.725f;
    State.Layers[1].Tag[2]       = 0.506f;

    // 2 — a paint layer with strokes, no mask, folded.
    ClearRecord(State.Layers[2], LayerKind::Paint, "Hand Detail");
    State.Layers[2].Opacity      = 100.0f;
    State.Layers[2].StrokeCount  = 62;
    State.Layers[2].ChannelTotal = 3;
    State.Layers[2].Tag[0]       = 0.937f;
    State.Layers[2].Tag[1]       = 0.267f;
    State.Layers[2].Tag[2]       = 0.267f;

    // 3 — a fill layer, folded.
    ClearRecord(State.Layers[3], LayerKind::Fill, "Roughness Lift");
    State.Layers[3].Opacity          = 40.0f;
    State.Layers[3].BlendIndex       = 4;       // Add
    State.Layers[3].ChannelTotal     = 1;
    State.Layers[3].PaintsBaseColour = false;   // a roughness-only fill has no colour to author
    State.Layers[3].Tag[0]           = 0.231f;  // #3b82f6
    State.Layers[3].Tag[1]           = 0.510f;
    State.Layers[3].Tag[2]           = 0.965f;

    // 4 — a material preset with a white mask, folded.
    ClearRecord(State.Layers[4], LayerKind::Material, "Brushed Copper");
    State.Layers[4].Opacity        = 100.0f;
    State.Layers[4].ChannelTotal   = 5;
    State.Layers[4].Paint[0]       = 0.722f;    // #b87333
    State.Layers[4].Paint[1]       = 0.451f;
    State.Layers[4].Paint[2]       = 0.200f;
    State.Layers[4].Tag[0]         = 0.545f;    // #8b5cf6
    State.Layers[4].Tag[1]         = 0.361f;
    State.Layers[4].Tag[2]         = 0.965f;
    State.Layers[4].MaskEnabled    = true;
    State.Layers[4].MaskFillMode   = MaskFill::White;
    State.Layers[4].MaskInverted   = true;
    State.Layers[4].MaskStrength   = 100.0f;
    State.Layers[4].ComponentTotal = 1;
    snprintf(State.Layers[4].Components[0].Label, sizeof(State.Layers[4].Components[0].Label), "%s", "Position Gradient");
    State.Layers[4].Components[0].Enabled = false;
    State.Layers[4].Components[0].Weight  = 0.50f;

    // 5 — the base material at the bottom of the stack.
    ClearRecord(State.Layers[5], LayerKind::Material, "Base Steel");
    State.Layers[5].Opacity      = 100.0f;
    State.Layers[5].ChannelTotal = 5;
    State.Layers[5].Paint[0]     = 0.298f;
    State.Layers[5].Paint[1]     = 0.310f;
    State.Layers[5].Paint[2]     = 0.325f;
    State.Layers[5].Tag[0]       = 0.580f;
    State.Layers[5].Tag[1]       = 0.639f;
    State.Layers[5].Tag[2]       = 0.722f;

    State.FocusOrdinal = 0;
}

}   // namespace LayerStackValidation
