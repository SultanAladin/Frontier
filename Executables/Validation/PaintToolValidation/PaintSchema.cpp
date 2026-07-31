/*==============================================================================================================================================
                                                             PAINTSCHEMA.CPP
==============================================================================================================================================*/
// 🧩 The ten family schemas, their conditional rows, and the seeding rules. Ported from Documentation/Prototypes/PaintToolMenu.html's
//    SchemaFor() / VisibleControls() / SeedParams() (lines 2274-2653 of that file), and checked against the ground truth harvested by
//    evaluating those same three functions over all 102 instruments (_ClaudeScratch/tmp/ProbePaintSchema.js -> paint_schema.json).

#include "PaintSchema.h"
#include "PaintCatalogue.h"

#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        OPTION LISTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Shared because the prototype shares them: GRAPHITE_GRADES is a named constant there, and the two marker nib sets are
//    written inline but selected between. Everything else is local to its one row.
namespace
{
    const char* const GraphiteGradeOptions[] = {
        "9H","8H","7H","6H","5H","4H","3H","2H","H","F","HB","B","2B","3B","4B","5B","6B","7B","8B","9B","Custom" };
    constexpr int GraphiteGradeCount = 21;
    static_assert(GraphiteGradeCount <= PaintSelectOptionLimit, "the grade scale outgrew PaintSelectOptionLimit");

    const char* const PenNibOptions[]       = { "Fine", "Medium", "Broad" };
    const char* const PenInkOptions[]       = { "Pigment", "Dye", "Gel", "Oil-based", "Iron gall" };
    const char* const LeadOptions[]         = { "0.3 mm", "0.5 mm", "0.7 mm", "0.9 mm", "2.0 mm", "3.15 mm", "5.6 mm" };
    const char* const BinderOptions[]       = { "Wax", "Oil", "Watercolour" };
    const char* const BrushShapeOptions[]   = { "Round", "Flat / Wide", "Filbert", "Fan", "Liner", "Wash",
                                                "Bright", "Angular", "Mop", "Rigger", "Stippler", "Texture" };
    const char* const BrushPaintOptions[]   = { "Oil", "Acrylic", "Watercolour", "Gouache", "Ink", "Wood stain" };
    const char* const BristleOptions[]      = { "Hog", "Sable", "Kolinsky", "Squirrel", "Goat", "Badger",
                                                "Synthetic", "Taklon", "Foam", "Silicone" };
    // 🔴 TWO nib sets in a DIFFERENT order, which is exactly why PaintControlDescriptor carries its default by LABEL.
    //    Index 0 is "Chisel" for a highlighter and "Bullet" for a marker, so an index default would silently name the
    //    wrong option on one of the two.
    const char* const HighlighterNibOptions[] = { "Chisel", "Bullet" };
    const char* const MarkerNibOptions[]      = { "Bullet", "Chisel", "Brush" };
    const char* const MarkerInkOptions[]      = { "Alcohol", "Water" };
    const char* const EraserTypeOptions[]     = { "Pencil", "Kneaded", "Vinyl / Plastic", "Gum", "Smudge eraser", "Electric" };
    const char* const BlendModeOptions[]      = { "Normal", "Multiply", "Screen", "Overlay", "Add" };
    const char* const NozzleOptions[]         = { "Fat", "Skinny" };
    const char* const ToneOptions[]           = { "Sanguine", "Bistre", "Sepia", "White", "Black" };
    const char* const EdgeOptions[]           = { "Point", "Broad" };
    const char* const DryVariantOptions[]     = { "Soft", "Oil", "Hard" };
    const char* const FlakeOptions[]          = { "Fine", "Medium", "Chunky" };
}


//------------------------------------------------------------------------------------------------------------------------
//                                                       ROW CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Four builders, one per control kind, so a row reads as its own shape rather than as a 10-field aggregate initialiser
//    whose unused members the reader has to count past. They exist for legibility of the tables below, nothing else.
namespace
{
    PaintControlDescriptor MakeSlider(const char* Key, const char* GlyphName, const char* Label,
                                      float Minimum, float Maximum, float Initial, const char* Unit)
    {
        PaintControlDescriptor Row{};
        Row.Category        = PaintControlCategory::Slider;
        Row.Key             = Key;
        Row.GlyphName       = GlyphName;
        Row.Label           = Label;
        Row.MinimumBoundary = Minimum;
        Row.MaximumBoundary = Maximum;
        Row.InitialReading  = Initial;
        Row.Unit            = Unit;
        return Row;
    }

    PaintControlDescriptor MakeChoice(PaintControlCategory Category, const char* Key,
                                      const char* GlyphName, const char* Label,
                                      const char* const* Options, int OptionCount, const char* InitialLabel)
    {
        PaintControlDescriptor Row{};
        Row.Category           = Category;
        Row.Key                = Key;
        Row.GlyphName          = GlyphName;
        Row.Label              = Label;
        Row.OptionLabels       = Options;
        Row.OptionCount        = OptionCount;
        Row.InitialOptionLabel = InitialLabel;
        return Row;
    }

    PaintControlDescriptor MakeSwitch(const char* Key, const char* GlyphName, const char* Label, bool Initial)
    {
        PaintControlDescriptor Row{};
        Row.Category          = PaintControlCategory::Switch;
        Row.Key               = Key;
        Row.GlyphName         = GlyphName;
        Row.Label             = Label;
        Row.InitialActivation = Initial;
        return Row;
    }

    // 📝 The prototype's `Tool.field || "Fallback"`, which treats an empty string as absent exactly as JS does.
    const char* AuthoredOr(const char* Authored, const char* Fallback)
    {
        return (Authored != nullptr && Authored[0] != '\0') ? Authored : Fallback;
    }

    bool SameText(const char* Left, const char* Right)
    {
        if (Left == nullptr || Right == nullptr) { return Left == Right; }
        return std::strcmp(Left, Right) == 0;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      SCHEMA ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // An append cursor over one group, so the family tables below read as a plain list of rows.
    struct GroupBuilder
    {
        PaintControlGroup* Group;

        void Add(const PaintControlDescriptor& Row)
        {
            if (Group->ControlCount < PaintGroupControlLimit)
            {
                Group->Controls[Group->ControlCount] = Row;
                ++Group->ControlCount;
            }
        }

        // Attach a condition to the row just added.
        void Gate(PaintRowCondition Condition)
        {
            if (Group->ControlCount > 0)
            {
                Group->Controls[Group->ControlCount - 1].Condition = Condition;
            }
        }
    };


    // 🔴 The prototype's StrokeGroup(Extra): FOUR shared rows and then the family's extras, in that order. Nine of the ten
    //    families call it; the eraser deliberately does NOT — it authors its own Stroke group with a size default of 14 and
    //    no opacity/flow/smoothing at all. Routing the eraser through here would give it three phantom rows and the wrong
    //    size default, which is why this is a helper rather than something applied unconditionally.
    GroupBuilder BeginStrokeGroup(PaintControlGroup& Group)
    {
        Group.Title        = "Stroke";
        Group.ControlCount = 0;

        GroupBuilder Builder{ &Group };
        Builder.Add(MakeSlider("size", "ParamSize",   "Size",      1.0f, 80.0f,   8.0f, "px"));
        Builder.Add(MakeSlider("opacity", "ParamOpacity","Opacity",   1.0f, 100.0f, 100.0f, "%"));
        Builder.Add(MakeSlider("flow", "ParamFlow",   "Flow",      1.0f, 100.0f,  90.0f, "%"));
        Builder.Add(MakeSlider("smoothing", "ParamSmooth", "Smoothing", 0.0f, 100.0f,  30.0f, "%"));
        return Builder;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    VISIBILITY EVALUATION
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Read the label currently chosen on a row, for the two live-state predicates. Null when the row or value is out of range.
    const char* ChosenLabelAt(const PaintSchema& Schema, const PaintControlValue* Values, int ValueCount, int ValueIndex)
    {
        if (Values == nullptr || ValueIndex < 0 || ValueIndex >= ValueCount) { return nullptr; }

        // Walk to the row that owns this value index — the same declared-row order SeedPaintValues wrote in.
        int Cursor = 0;
        for (int GroupIndex = 0; GroupIndex < Schema.GroupCount; ++GroupIndex)
        {
            const PaintControlGroup& Group = Schema.Groups[GroupIndex];
            if (ValueIndex < Cursor + Group.ControlCount)
            {
                const PaintControlDescriptor& Control = Group.Controls[ValueIndex - Cursor];
                const int                     Chosen  = Values[ValueIndex].ChosenOption;
                if (Control.OptionLabels == nullptr || Chosen < 0 || Chosen >= Control.OptionCount) { return nullptr; }
                return Control.OptionLabels[Chosen];
            }
            Cursor += Group.ControlCount;
        }
        return nullptr;
    }


    // 🔴 Both live-state predicates read the FIRST row of the family group — pencil `grade` and coloured-pencil `binder` are
    //    each their family's leading control. Resolved structurally rather than by searching for a key string, because the
    //    schema carries no keys: a row is identified by its position, which is also what the value array is indexed by.
    bool IsRowVisible(const PaintControlDescriptor& Control, const PaintInstrumentDescriptor& Instrument,
                      const PaintSchema& Schema, const PaintControlValue* Values, int ValueCount)
    {
        switch (Control.Condition)
        {
            case PaintRowCondition::Always:
                return true;

            // ── live state: re-read on every edit ──
            case PaintRowCondition::GradeIsCustom:
                return SameText(ChosenLabelAt(Schema, Values, ValueCount, 0), "Custom");

            case PaintRowCondition::BinderIsWatercolour:
                return SameText(ChosenLabelAt(Schema, Values, ValueCount, 0), "Watercolour");

            // ── instrument: fixed for the selection ──
            case PaintRowCondition::InstrumentMechanical:
                return Instrument.Mechanical;

            // 📝 `!== true`, so an unauthored flag SHOWS the row. Both of these read as "absent means visible".
            case PaintRowCondition::InstrumentNotHighlighter:
                return !Instrument.Highlighter;

            case PaintRowCondition::InstrumentNotAirbrush:
                return !Instrument.Airbrush;

            // 📝 Two ways to qualify — `Array.isArray(Tool.variants) || Tool.variant !== undefined`. An instrument that
            //    authors a bare `variant` with no list still shows the row, over the default Soft/Oil/Hard options.
            case PaintRowCondition::DryHasVariants:
                return (Instrument.Variants[0] != nullptr) || (Instrument.Variant != nullptr);

            // 🔴 `tone` is authored by ZERO instruments, verified against SEED, so `Tool.tone !== undefined` is false for all
            //    102 and the row is DEAD in the prototype as shipped. The catalogue therefore has no Tone field to read — the
            //    emitter only emits keys that are authored somewhere — so the condition is stated as never rather than as a
            //    comparison against a field invented solely to always be null. The row is still declared and still SEEDED
            //    (to "Sanguine"), which is what keeps the value indices matching the prototype's.
            case PaintRowCondition::DryHasTone:
                return false;

            case PaintRowCondition::DryHasEdge:
                return Instrument.Edge != nullptr;
        }

        return true;
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                     PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

PaintSchema ResolvePaintSchema(const PaintInstrumentDescriptor& Instrument)
{
    PaintSchema Schema{};
    const char* const Family = Instrument.FamilyKey;

    // 📝 The family group is Groups[0] and Stroke is Groups[1], matching the prototype's array order — the options column
    //    draws them in this order and the preview's spec rows read the family group by index.
    PaintControlGroup& FamilyGroup = Schema.Groups[0];
    PaintControlGroup& StrokeSlot  = Schema.Groups[1];
    Schema.GroupCount = 2;

    if (SameText(Family, "pen"))
    {
        FamilyGroup.Title = "Pen";
        GroupBuilder Pen{ &FamilyGroup };
        Pen.Add(MakeChoice(PaintControlCategory::Segmented, "nib", "ParamNib", "Nib",
                           PenNibOptions, 3, AuthoredOr(Instrument.Nib, "Medium")));
        Pen.Add(MakeChoice(PaintControlCategory::Select, "ink", "ParamPaint", "Ink",
                           PenInkOptions, 5, AuthoredOr(Instrument.Ink, "Pigment")));
        // 🔴 `def:Tool.taper !== false` — ABSENT means TRUE. This is the whole reason Taper is a PaintAuthoredFlag and not a
        //    bool: reading an unauthored taper as false flips 6 of the 10 pens from taper-on to taper-off, silently.
        Pen.Add(MakeSwitch("taper", "ParamTaper", "Speed taper", Instrument.Taper != PaintAuthoredFlag::AuthoredFalse));

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("pressure", "ParamPressure", "Pressure",   0.0f, 100.0f, 65.0f, "%"));
        Stroke.Add(MakeSlider("bleed", "ParamWetness",  "Feathering", 0.0f, 100.0f, 15.0f, "%"));
        return Schema;
    }

    if (SameText(Family, "pencil"))
    {
        FamilyGroup.Title = "Graphite";
        GroupBuilder Graphite{ &FamilyGroup };
        Graphite.Add(MakeChoice(PaintControlCategory::Select, "grade", "ParamGrade", "Grade",
                                GraphiteGradeOptions, GraphiteGradeCount, AuthoredOr(Instrument.Grade, "HB")));
        // 📝 Unitless, unlike every other slider in the card — the prototype writes `unit:""`.
        Graphite.Add(MakeSlider("hardness", "ParamGrain", "Custom hardness", 0.0f, 100.0f, 50.0f, ""));
        Graphite.Gate(PaintRowCondition::GradeIsCustom);
        Graphite.Add(MakeChoice(PaintControlCategory::Select, "lead", "ParamLead", "Lead",
                                LeadOptions, 7, AuthoredOr(Instrument.Lead, "0.5 mm")));
        Graphite.Gate(PaintRowCondition::InstrumentMechanical);

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("pressure", "ParamPressure", "Pressure",      0.0f, 100.0f, 70.0f, "%"));
        Stroke.Add(MakeSlider("tilt", "ParamTilt",     "Tilt shading",  0.0f, 100.0f, 20.0f, "%"));
        Stroke.Add(MakeSlider("grain", "ParamGrain",    "Tooth / grain", 0.0f, 100.0f, 55.0f, "%"));
        return Schema;
    }

    if (SameText(Family, "colored-pencil"))
    {
        FamilyGroup.Title = "Colour Pencil";
        GroupBuilder Coloured{ &FamilyGroup };
        Coloured.Add(MakeChoice(PaintControlCategory::Segmented, "binder", "ParamBinder", "Binder",
                                BinderOptions, 3, AuthoredOr(Instrument.Binder, "Wax")));
        Coloured.Add(MakeSlider("wet", "ParamWetness", "Water blend", 0.0f, 100.0f, 40.0f, "%"));
        Coloured.Gate(PaintRowCondition::BinderIsWatercolour);
        Coloured.Add(MakeSlider("bleed", "ParamSmear",   "Bleed",       0.0f, 100.0f, 30.0f, "%"));
        Coloured.Gate(PaintRowCondition::BinderIsWatercolour);

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("pressure", "ParamPressure", "Pressure",      0.0f, 100.0f, 75.0f, "%"));
        Stroke.Add(MakeSlider("grain", "ParamGrain",    "Tooth / grain", 0.0f, 100.0f, 45.0f, "%"));
        Stroke.Add(MakeSlider("buildup", "ParamPigment",  "Build-up",      0.0f, 100.0f, 50.0f, "%"));
        return Schema;
    }

    if (SameText(Family, "brush"))
    {
        FamilyGroup.Title = "Brush";
        GroupBuilder Brush{ &FamilyGroup };
        Brush.Add(MakeChoice(PaintControlCategory::Select, "shape", "ParamShape", "Shape",
                             BrushShapeOptions, 12, AuthoredOr(Instrument.Shape, "Round")));
        Brush.Add(MakeChoice(PaintControlCategory::Select, "paint", "ParamPaint", "Paint",
                             BrushPaintOptions, 6, AuthoredOr(Instrument.Paint, "Acrylic")));
        Brush.Add(MakeChoice(PaintControlCategory::Select, "bristle", "ParamBristle", "Bristle",
                             BristleOptions, 10, AuthoredOr(Instrument.Bristle, "Synthetic")));

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("wetness", "ParamWetness", "Wetness",         0.0f, 100.0f, 40.0f, "%"));
        Stroke.Add(MakeSlider("taperAmt", "ParamTaper",   "Taper",           0.0f, 100.0f, 60.0f, "%"));
        Stroke.Add(MakeSlider("bristleTx", "ParamBristle", "Bristle texture", 0.0f, 100.0f, 35.0f, "%"));
        return Schema;
    }

    if (SameText(Family, "marker"))
    {
        // 🔴 Three things swap on the same flag, and they are easy to fix only two of: the group TITLE, the nib OPTION LIST
        //    (and its differing order), and a spliced-in Glow row at the end of Stroke. A highlighter also loses its Ink row.
        const bool IsHighlighter = Instrument.Highlighter;

        FamilyGroup.Title = IsHighlighter ? "Highlighter" : "Marker";
        GroupBuilder Marker{ &FamilyGroup };
        Marker.Add(MakeChoice(PaintControlCategory::Segmented, "nibShape", "ParamNib", "Nib",
                              IsHighlighter ? HighlighterNibOptions : MarkerNibOptions,
                              IsHighlighter ? 2 : 3,
                              AuthoredOr(Instrument.NibShape, "Chisel")));
        Marker.Add(MakeChoice(PaintControlCategory::Segmented, "ink", "ParamPaint", "Ink",
                              MarkerInkOptions, 2, AuthoredOr(Instrument.Ink, "Alcohol")));
        Marker.Gate(PaintRowCondition::InstrumentNotHighlighter);

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("saturation", "ParamPigment", "Saturation", 0.0f, 100.0f, 70.0f, "%"));
        Stroke.Add(MakeSlider("streak", "ParamSmear",   "Streaking",  0.0f, 100.0f, 25.0f, "%"));
        // 📝 Spliced, not gated. The prototype builds this row into the array only for a highlighter rather than declaring it
        //    with a `when:` — so on a plain marker it is never DECLARED and therefore never seeded either.
        if (IsHighlighter) { Stroke.Add(MakeSlider("glow", "ParamGlow", "Glow", 0.0f, 100.0f, 40.0f, "%")); }
        return Schema;
    }

    if (SameText(Family, "eraser"))
    {
        FamilyGroup.Title = "Eraser";
        GroupBuilder Eraser{ &FamilyGroup };
        Eraser.Add(MakeChoice(PaintControlCategory::Select, "etype", "ParamMode", "Type",
                              EraserTypeOptions, 6, AuthoredOr(Instrument.Etype, "Vinyl / Plastic")));

        // 🔴 Its OWN Stroke group, not BeginStrokeGroup: size defaults to 14 rather than 8, and there is no
        //    opacity / flow / smoothing. The only family that does this.
        StrokeSlot.Title = "Stroke";
        GroupBuilder Stroke{ &StrokeSlot };
        Stroke.Add(MakeSlider("size", "ParamSize",     "Size",           1.0f, 80.0f,  14.0f, "px"));
        Stroke.Add(MakeSlider("strength", "ParamStrength", "Erase strength", 1.0f, 100.0f, 100.0f, "%"));
        Stroke.Add(MakeSlider("softness", "ParamSoftness", "Edge softness",  0.0f, 100.0f,  20.0f, "%"));
        return Schema;
    }

    if (SameText(Family, "stylus"))
    {
        FamilyGroup.Title = "Digital";
        GroupBuilder Digital{ &FamilyGroup };
        // 📝 `Tool.hardness ?? 90` uses NULLISH coalescing, not `||`, so an authored 0 would survive where `||` would
        //    replace it with 90. The catalogue stores an unauthored hardness as 0.0f, which cannot express that difference —
        //    it is lossless only because no instrument authors 0, and the check asserts exactly that so a future SEED edit
        //    authoring `hardness:0` fails loudly here instead of reading as 90 on screen.
        Digital.Add(MakeSlider("hardness", "ParamSoftness", "Hardness", 0.0f, 100.0f,
                               (Instrument.Hardness > 0.0f) ? Instrument.Hardness : 90.0f, "%"));
        // 📝 No Tool. read at all — the prototype hardcodes "Normal", so an authored blendMode would be ignored.
        Digital.Add(MakeChoice(PaintControlCategory::Select, "blendMode", "ParamBlend", "Blend mode",
                               BlendModeOptions, 5, "Normal"));
        Digital.Add(MakeSwitch("tiltTrack", "ParamTilt", "Tilt tracking", true));

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("scatter", "ParamScatter", "Scatter",    0.0f, 100.0f, 0.0f, "%"));
        Stroke.Add(MakeSlider("jitter", "ParamJitter",  "Hue jitter", 0.0f, 100.0f, 0.0f, "%"));
        return Schema;
    }

    if (SameText(Family, "spray"))
    {
        // 🔴 The airbrush flag swaps the title, hides the nozzle row, AND moves two Stroke defaults (spread 45->35,
        //    density 60->40). The probe's family dump only ever showed the can's values, so the two moved defaults are read
        //    off the source rather than off the harvest — asserted per-instrument in the check.
        const bool IsAirbrush = Instrument.Airbrush;

        FamilyGroup.Title = IsAirbrush ? "Airbrush" : "Spray Can";
        GroupBuilder Spray{ &FamilyGroup };
        Spray.Add(MakeChoice(PaintControlCategory::Segmented, "nozzle", "ParamNib", "Nozzle",
                             NozzleOptions, 2, AuthoredOr(Instrument.Nozzle, "Fat")));
        Spray.Gate(PaintRowCondition::InstrumentNotAirbrush);
        Spray.Add(MakeSlider("pressure", "ParamPressure", "Air pressure", 0.0f, 100.0f, 60.0f, "%"));
        Spray.Add(MakeSlider("distance", "ParamSpread",   "Distance",     0.0f, 100.0f, 50.0f, "%"));

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("spread", "ParamSpread",  "Spread",  1.0f, 100.0f, IsAirbrush ? 35.0f : 45.0f, "%"));
        Stroke.Add(MakeSlider("density", "ParamDensity", "Density", 1.0f, 100.0f, IsAirbrush ? 40.0f : 60.0f, "%"));
        Stroke.Add(MakeSlider("drip", "ParamDrip",    "Drips",   0.0f, 100.0f, 10.0f, "%"));
        return Schema;
    }

    if (SameText(Family, "dry"))
    {
        FamilyGroup.Title = AuthoredOr(Instrument.DryTitle, "Dry Media");
        GroupBuilder Dry{ &FamilyGroup };
        // 📝 The option list itself is authored per instrument (`Tool.variants||["Soft","Oil","Hard"]`), so a charcoal shows
        //    Vine/Compressed while a pastel shows Soft/Oil/Hard. Null-padded in the catalogue, so count the non-null entries.
        int VariantCount = 0;
        while (VariantCount < PaintVariantLimit && Instrument.Variants[VariantCount] != nullptr) { ++VariantCount; }
        const bool HasVariantList = (VariantCount > 0);

        Dry.Add(MakeChoice(PaintControlCategory::Segmented, "variant", "ParamBinder", "Type",
                           HasVariantList ? Instrument.Variants : DryVariantOptions,
                           HasVariantList ? VariantCount : 3,
                           AuthoredOr(Instrument.Variant, "Soft")));
        Dry.Gate(PaintRowCondition::DryHasVariants);
        Dry.Add(MakeChoice(PaintControlCategory::Select, "tone", "ParamTone", "Tone",
                           ToneOptions, 5, "Sanguine"));
        Dry.Gate(PaintRowCondition::DryHasTone);
        Dry.Add(MakeChoice(PaintControlCategory::Segmented, "edge", "ParamNib", "Edge",
                           EdgeOptions, 2, AuthoredOr(Instrument.Edge, "Broad")));
        Dry.Gate(PaintRowCondition::DryHasEdge);

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("pigment", "ParamPigment", "Pigment load",  0.0f, 100.0f, 70.0f, "%"));
        Stroke.Add(MakeSlider("grain", "ParamGrain",   "Tooth / grain", 0.0f, 100.0f, 62.0f, "%"));
        Stroke.Add(MakeSlider("smear", "ParamSmear",   "Smear",         0.0f, 100.0f, 38.0f, "%"));
        return Schema;
    }

    if (SameText(Family, "glitter"))
    {
        FamilyGroup.Title = "Glitter";
        GroupBuilder Glitter{ &FamilyGroup };
        // 📝 Both defaults are hardcoded in the prototype; no instrument override is read.
        Glitter.Add(MakeChoice(PaintControlCategory::Segmented, "flake", "ParamSparkle", "Flake size",
                               FlakeOptions, 3, "Medium"));
        Glitter.Add(MakeSlider("sparkle", "ParamSparkle", "Sparkle density", 0.0f, 100.0f, 65.0f, "%"));

        GroupBuilder Stroke = BeginStrokeGroup(StrokeSlot);
        Stroke.Add(MakeSlider("scatter", "ParamScatter", "Scatter", 0.0f, 100.0f, 45.0f, "%"));
        Stroke.Add(MakeSlider("glow", "ParamGlow",    "Glow",    0.0f, 100.0f, 35.0f, "%"));
        return Schema;
    }

    // 📝 The prototype's `return [StrokeGroup()]` fallthrough: an unknown family still gets a usable pane. Unreachable with
    //    the generated catalogue, kept because the port is 1:1 and a future family added to the rail lands here rather than
    //    on an empty card.
    BeginStrokeGroup(Schema.Groups[0]);
    Schema.GroupCount = 1;
    return Schema;
}

int SeedPaintValues(const PaintInstrumentDescriptor& Instrument, const PaintSchema& Schema,
                    PaintControlValue* Values, int ValueCapacity)
{
    (void)Instrument;   // 📝 Unused BY DESIGN: the authored defaults were already baked into the schema's rows by
                        //    ResolvePaintSchema, exactly as the prototype's SeedParams reads only `C.def`. Taking the
                        //    instrument again and re-deriving here is how the two would drift.

    if (Values == nullptr) { return 0; }

    int Written = 0;

    // 🔴 Walks EVERY declared row, hidden ones included, because the prototype's SeedParams ignores `when` entirely. That is
    //    what lets a value survive being hidden and come back unchanged: 39 of the 102 instruments have at least one such
    //    row, and skipping them would both lose those values and shift every later row's index by one.
    for (int GroupIndex = 0; GroupIndex < Schema.GroupCount; ++GroupIndex)
    {
        const PaintControlGroup& Group = Schema.Groups[GroupIndex];

        for (int RowIndex = 0; RowIndex < Group.ControlCount; ++RowIndex)
        {
            if (Written >= ValueCapacity) { return Written; }

            const PaintControlDescriptor& Control = Group.Controls[RowIndex];
            PaintControlValue&            Value   = Values[Written];

            Value = PaintControlValue{};

            switch (Control.Category)
            {
                case PaintControlCategory::Slider:
                    Value.Reading = Control.InitialReading;
                    break;

                case PaintControlCategory::Segmented:
                case PaintControlCategory::Select:
                    Value.ChosenOption = ResolvePaintOptionIndex(Control, Control.InitialOptionLabel);
                    break;

                case PaintControlCategory::Switch:
                    Value.Activated = Control.InitialActivation;
                    break;
            }

            ++Written;
        }
    }

    return Written;
}


int ResolveVisiblePaintControls(const PaintInstrumentDescriptor& Instrument, const PaintSchema& Schema,
                                const PaintControlValue* Values, int ValueCount,
                                PaintVisibleControl* Visible, int VisibleCapacity)
{
    if (Visible == nullptr) { return 0; }

    int VisibleCount = 0;
    int ValueIndex   = 0;   // 📝 Advances over EVERY declared row, shown or not — it indexes the seeded array, which
                            //    includes hidden rows. Advancing it only on shown rows is the classic off-by-N here.

    for (int GroupIndex = 0; GroupIndex < Schema.GroupCount; ++GroupIndex)
    {
        const PaintControlGroup& Group = Schema.Groups[GroupIndex];

        // 📝 Resolved before anything is emitted so an empty group can be dropped without having written a title for it.
        const int GroupFirstValueIndex = ValueIndex;
        bool      GroupHasVisibleRow   = false;

        for (int RowIndex = 0; RowIndex < Group.ControlCount; ++RowIndex)
        {
            const PaintControlDescriptor& Control    = Group.Controls[RowIndex];
            const int                     ThisValue  = GroupFirstValueIndex + RowIndex;

            if (IsRowVisible(Control, Instrument, Schema, Values, ValueCount))
            {
                if (VisibleCount < VisibleCapacity)
                {
                    PaintVisibleControl& Entry = Visible[VisibleCount];
                    Entry.Control     = &Control;
                    Entry.GroupTitle  = Group.Title;
                    // 🔴 True on the first VISIBLE row of the group, not on RowIndex == 0. When a group's first declared row
                    //    is hidden — the airbrush, whose Nozzle row leads the group — the title must still be drawn, above
                    //    whichever row actually appears first.
                    Entry.StartsGroup = !GroupHasVisibleRow;
                    Entry.ValueIndex  = ThisValue;
                    ++VisibleCount;
                }

                GroupHasVisibleRow = true;
            }
        }

        ValueIndex += Group.ControlCount;

        // 🔴 An empty group DISAPPEARS rather than drawing as a title with no body — the prototype's
        //    `.filter(G => G.controls.length > 0)`. Nothing to undo here because StartsGroup is what carries the title and no
        //    row was emitted to carry it: Chalk Stick and Wax Crayon hide all three dry rows and so show ONLY a Stroke group.
    }

    return VisibleCount;
}


int ResolvePaintOptionIndex(const PaintControlDescriptor& Control, const char* OptionLabel)
{
    if (Control.OptionLabels == nullptr || OptionLabel == nullptr) { return 0; }

    for (int Index = 0; Index < Control.OptionCount; ++Index)
    {
        if (SameText(Control.OptionLabels[Index], OptionLabel)) { return Index; }
    }

    // 📝 An unmatched label falls back to the first option rather than asserting. The catalogue's authored strings are
    //    generated from the prototype's own SEED, so a mismatch means the two drifted — which the check catches loudly
    //    and exhaustively, and which should not crash a validation app in the meantime.
    return 0;
}

} // namespace Frontier
