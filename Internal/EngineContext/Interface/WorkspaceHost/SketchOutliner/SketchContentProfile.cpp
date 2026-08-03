/*==============================================================================================================================================
                                                            SKETCHCONTENTPROFILE.CPP
==============================================================================================================================================*/
// 🧩 The parametric-sketch (CAD) content profile for the shared outliner panel: the Part / Origin / Sketches / Bodies / Features vocabulary,
//    the "cad-" registry icon keys, the CAD add catalogue and filter facets, the default demonstration tree, and the procedural stroke art for
//    every CAD glyph. The panel (SketchOutlinerPanel) owns all behaviour and names none of this — it reads it through the OutlinerContentProfile
//    the ResolveSketchContentProfile() below returns. "Another outliner" is another profile like this one, never another panel.

#include "OutlinerContentProfile.h"
#include "SketchOutlinerPanel.h"

#include "imgui.h"

#include <cstring>
#include <vector>

namespace Frontier::SketchOutlinerUi
{

namespace
{
    //---------------------------------------------------- CLASSIFICATION IDS ----------------------------------------------------

    // 📝 The CAD classification vocabulary as opaque ids. The panel only ever compares these and looks them up in ClassRows below; the values
    //    themselves are private to this profile (a scene tree carries an entirely different set).
    enum SketchClass : int
    {
        PartRoot = 0,
        DatumPlane,
        DatumAxis,
        DatumPoint,
        CoordinateFrame,
        SketchProfile,
        SketchCurve,
        SketchConstraint,
        SketchDimension,
        SolidShell,
        FeatureOperation,
        FeatureDirectory,
        ComponentInstance,
    };

    // -- Per-classification icon tints (the CAD stroke colours) --
    const ImU32 TintPart       = IM_COL32(0xc9, 0xc9, 0xcf, 255);
    const ImU32 TintDatum      = IM_COL32(0x5a, 0x95, 0xdd, 255);
    const ImU32 TintFrame      = IM_COL32(0xb9, 0xb9, 0xc0, 255);
    const ImU32 TintSketch     = IM_COL32(0x3a, 0x7b, 0xd5, 255);
    const ImU32 TintConstraint = IM_COL32(0xc7, 0x74, 0xe0, 255);
    const ImU32 TintSolid      = IM_COL32(0xe0, 0xb6, 0x4f, 255);
    const ImU32 TintFeature    = IM_COL32(0x3a, 0xd0, 0x7a, 255);
    const ImU32 TintDirectory  = IM_COL32(0xd0, 0x8a, 0x4f, 255);
    const ImU32 TintComponent  = IM_COL32(0xb9, 0xb9, 0xc0, 255);


    //---------------------------------------------------- PROFILE TABLES ----------------------------------------------------

    // 📝 Classification table: every icon / tint / label / container decision the panel makes for a CAD row routes through here by id.
    const OutlinerClassRow ClassRows[] =
    {
        { PartRoot,          "Part",       "cad-document",    TintPart,       true  },
        { DatumPlane,        "Datums",     "cad-datum-plane", TintDatum,      false },
        { DatumAxis,         "Datum Axis", "cad-datum-axis",  TintDatum,      false },
        { DatumPoint,        "Datum Point","cad-datum-point", TintDatum,      false },
        { CoordinateFrame,   "Coordinate", "cad-coordinate",  TintFrame,      false },
        { SketchProfile,     "Sketches",   "cad-profile",     TintSketch,     true  },
        { SketchCurve,       "Curve",      "cad-curve",       TintSketch,     false },
        { SketchConstraint,  "Constraint", "cad-constraint",  TintConstraint, false },
        { SketchDimension,   "Dimension",  "cad-dimension",   TintConstraint, false },
        { SolidShell,        "Solids",     "cad-component",   TintSolid,      false },
        { FeatureOperation,  "Features",   "cad-feature",     TintFeature,    false },
        { FeatureDirectory,  "Directories","g-folder",        TintDirectory,  true  },
        { ComponentInstance, "Component",  "cad-component",   TintComponent,  false },
    };

    // 📝 The add-object catalogue: sketches + features + datums + a directory, grouped by build stage. Section rows carry a non-null Section;
    //    creatable rows carry a Stem (the unique-name scheme) and a ClassificationId (looked up in ClassRows for icon / tint / container).
    const OutlinerAddRow AddRows[] =
    {
        { "Sketch",   nullptr,       nullptr,     PartRoot         },
        { nullptr,    "Sketch",      "Sketch",    SketchProfile    },
        { "Features", nullptr,       nullptr,     PartRoot         },
        { nullptr,    "Extrude",     "Extrude",   FeatureOperation },
        { nullptr,    "Revolve",     "Revolve",   FeatureOperation },
        { nullptr,    "Fillet",      "Fillet",    FeatureOperation },
        { "Datums",   nullptr,       nullptr,     PartRoot         },
        { nullptr,    "Datum Plane", "Plane",     DatumPlane       },
        { nullptr,    "Datum Axis",  "Axis",      DatumAxis        },
        { nullptr,    "Datum Point", "Point",     DatumPoint       },
        { "Organize", nullptr,       nullptr,     PartRoot         },
        { nullptr,    "Directory",   "Directory", FeatureDirectory },
    };

    // 📝 Which classification ids appear in the TYPES filter section, in order (the PartRoot container is excluded on purpose).
    const int FilterClassIds[] = { SketchProfile, FeatureOperation, DatumPlane, SolidShell, FeatureDirectory };

    // 📝 The COLOURS filter swatches.
    const OutlinerTintFacet TintFacets[] =
    {
        { TintSketch,     "Blue" },
        { TintDatum,      "Sky" },
        { TintConstraint, "Purple" },
        { TintSolid,      "Amber" },
        { TintFeature,    "Green" },
        { TintDirectory,  "Orange" },
    };


    //---------------------------------------------------- SAMPLE SEED ----------------------------------------------------

    // 📝 Issue the next stable token and stamp a freshly built CAD entry.
    RecordEntry MakeEntry(SketchOutlinerState& State, const char* Label, int ClassificationId, const char* IconKey, ImU32 Tint, bool Expanded)
    {
        RecordEntry Entry;
        Entry.Token            = State.NextToken++;
        Entry.Label            = Label;
        Entry.ClassificationId = ClassificationId;
        Entry.IconKey          = IconKey;
        Entry.TintColor        = Tint;
        Entry.ExpandedState    = Expanded;
        Entry.ConcealedState   = false;
        return Entry;
    }

    // 📝 Build the CAD demonstration tree: a part root, an origin (datums + a coordinate frame), a sketch with curves + a constraint + a
    //    dimension, a solid body from a feature, the feature build order, and a component instance.
    void SeedSketchSample(SketchOutlinerState& State, const OutlinerContentProfile& /*Profile*/)
    {
        State.RootRegion.clear();
        State.SelectionSet.clear();

        RecordEntry Part = MakeEntry(State, "Part1", PartRoot, "cad-document", TintPart, true);

        RecordEntry Origin = MakeEntry(State, "Origin", DatumPlane, "g-folder", TintDatum, true);
        Origin.ClassificationId = PartRoot;   // the Origin directory is a plain container, not a datum leaf
        Origin.IconKey = "g-folder";
        Origin.TintColor = TintDirectory;
        Origin.NestedRegion.push_back(MakeEntry(State, "Front Plane", DatumPlane, "cad-datum-plane", TintDatum, false));
        Origin.NestedRegion.push_back(MakeEntry(State, "Top Plane",   DatumPlane, "cad-datum-plane", TintDatum, false));
        Origin.NestedRegion.push_back(MakeEntry(State, "Right Plane", DatumPlane, "cad-datum-plane", TintDatum, false));
        Origin.NestedRegion.push_back(MakeEntry(State, "Origin Point", DatumPoint, "cad-datum-point", TintDatum, false));
        Origin.NestedRegion.push_back(MakeEntry(State, "Frame", CoordinateFrame, "cad-coordinate", TintFrame, false));

        RecordEntry Sketch = MakeEntry(State, "Sketch1", SketchProfile, "cad-profile", TintSketch, true);
        Sketch.NestedRegion.push_back(MakeEntry(State, "Line1", SketchCurve, "cad-curve", TintSketch, false));
        Sketch.NestedRegion.push_back(MakeEntry(State, "Arc1",  SketchCurve, "cad-curve", TintSketch, false));
        Sketch.NestedRegion.push_back(MakeEntry(State, "Coincident1", SketchConstraint, "cad-constraint", TintConstraint, false));
        Sketch.NestedRegion.push_back(MakeEntry(State, "Length1", SketchDimension, "cad-dimension", TintConstraint, false));

        RecordEntry Bodies = MakeEntry(State, "Bodies", PartRoot, "g-folder", TintDirectory, true);
        Bodies.NestedRegion.push_back(MakeEntry(State, "Body1", SolidShell, "cad-component", TintSolid, false));

        RecordEntry Features = MakeEntry(State, "Features", PartRoot, "g-folder", TintDirectory, true);
        Features.NestedRegion.push_back(MakeEntry(State, "Extrude1", FeatureOperation, "cad-feature", TintFeature, false));
        Features.NestedRegion.push_back(MakeEntry(State, "Fillet1",  FeatureOperation, "cad-feature", TintFeature, false));

        RecordEntry Component = MakeEntry(State, "Fastener_M6", ComponentInstance, "cad-component", TintComponent, false);

        Part.NestedRegion.push_back(std::move(Origin));
        Part.NestedRegion.push_back(std::move(Sketch));
        Part.NestedRegion.push_back(std::move(Bodies));
        Part.NestedRegion.push_back(std::move(Features));
        Part.NestedRegion.push_back(std::move(Component));

        State.RootRegion.push_back(std::move(Part));

        // 📝 Pre-select Body1 so the panel opens with a real selection.
        struct Locator
        {
            static RecordToken Resolve(const std::vector<RecordEntry>& Region)
            {
                for (const RecordEntry& Entry : Region)
                {
                    if (Entry.Label == "Body1") { return Entry.Token; }
                    RecordToken Found = Resolve(Entry.NestedRegion);
                    if (Found != 0) { return Found; }
                }
                return 0;
            }
        };
        RecordToken BodyToken = Locator::Resolve(State.RootRegion);
        if (BodyToken != 0) { State.SelectionSet.push_back(BodyToken); State.RangeAnchor = BodyToken; }
    }


    //---------------------------------------------------- FALLBACK ART ----------------------------------------------------

    // 📝 The procedural stroke fallback for every CAD icon key, drawn centred in an 18px box at Origin, tinted by TintColor. Used only when the
    //    registry is null or a key has not uploaded, so a row still reads its item type.
    void PaintSketchGlyph(ImDrawList* DrawList, const char* IconKey, ImVec2 Origin, ImU32 TintColor)
    {
        const float Box = 18.0f;
        ImVec2 Centre = ImVec2(Origin.x + Box * 0.5f, Origin.y + Box * 0.5f);
        const float Thickness = 1.3f;
        if (IconKey == nullptr) { IconKey = ""; }

        if (std::strcmp(IconKey, "cad-document") == 0)
        {
            ImVec2 Min = ImVec2(Centre.x - 5.0f, Centre.y - 6.5f);
            ImVec2 Max = ImVec2(Centre.x + 5.0f, Centre.y + 6.5f);
            DrawList->AddRect(Min, Max, TintColor, 1.0f, 0, Thickness);
            DrawList->AddLine(ImVec2(Max.x - 3.5f, Min.y), ImVec2(Max.x, Min.y + 3.5f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Min.x + 2.5f, Centre.y - 1.0f), ImVec2(Max.x - 2.5f, Centre.y - 1.0f), TintColor, 1.0f);
            DrawList->AddLine(ImVec2(Min.x + 2.5f, Centre.y + 2.0f), ImVec2(Max.x - 2.5f, Centre.y + 2.0f), TintColor, 1.0f);
        }
        else if (std::strcmp(IconKey, "g-folder") == 0 || std::strcmp(IconKey, "g-folder-open") == 0)
        {
            ImVec2 Min = ImVec2(Centre.x - 6.5f, Centre.y - 4.5f);
            ImVec2 Max = ImVec2(Centre.x + 6.5f, Centre.y + 5.0f);
            DrawList->AddRect(Min, Max, TintColor, 1.5f, 0, Thickness);
            DrawList->AddLine(ImVec2(Min.x, Min.y), ImVec2(Min.x + 3.5f, Min.y - 2.0f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Min.x + 3.5f, Min.y - 2.0f), ImVec2(Min.x + 6.0f, Min.y), TintColor, Thickness);
        }
        else if (std::strcmp(IconKey, "cad-datum-plane") == 0)
        {
            DrawList->AddQuad(ImVec2(Centre.x - 6.5f, Centre.y - 3.0f), ImVec2(Centre.x + 2.5f, Centre.y - 5.0f),
                              ImVec2(Centre.x + 6.5f, Centre.y + 3.0f), ImVec2(Centre.x - 2.5f, Centre.y + 5.0f), TintColor, Thickness);
        }
        else if (std::strcmp(IconKey, "cad-datum-axis") == 0)
        {
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y + 4.0f), ImVec2(Centre.x + 6.0f, Centre.y - 5.0f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + 6.0f, Centre.y - 5.0f), ImVec2(Centre.x + 2.0f, Centre.y - 4.5f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + 6.0f, Centre.y - 5.0f), ImVec2(Centre.x + 5.5f, Centre.y - 1.0f), TintColor, Thickness);
        }
        else if (std::strcmp(IconKey, "cad-datum-point") == 0)
        {
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y), ImVec2(Centre.x + 6.0f, Centre.y), TintColor, 1.0f);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - 6.0f), ImVec2(Centre.x, Centre.y + 6.0f), TintColor, 1.0f);
            DrawList->AddCircleFilled(Centre, 1.8f, TintColor);
        }
        else if (std::strcmp(IconKey, "cad-coordinate") == 0)
        {
            DrawList->AddLine(Centre, ImVec2(Centre.x + 6.0f, Centre.y), TintColor, Thickness);
            DrawList->AddLine(Centre, ImVec2(Centre.x, Centre.y - 6.0f), TintColor, Thickness);
            DrawList->AddLine(Centre, ImVec2(Centre.x - 5.0f, Centre.y + 4.0f), TintColor, Thickness);
            DrawList->AddCircleFilled(Centre, 1.6f, TintColor);
        }
        else if (std::strcmp(IconKey, "cad-profile") == 0)
        {
            DrawList->AddRect(ImVec2(Centre.x - 5.5f, Centre.y - 5.5f), ImVec2(Centre.x + 5.5f, Centre.y + 5.5f), TintColor, 3.5f, 0, Thickness);
            DrawList->AddCircleFilled(ImVec2(Centre.x - 5.5f, Centre.y - 5.5f), 1.5f, TintColor);
            DrawList->AddCircleFilled(ImVec2(Centre.x + 5.5f, Centre.y + 5.5f), 1.5f, TintColor);
        }
        else if (std::strcmp(IconKey, "cad-curve") == 0)
        {
            DrawList->AddBezierQuadratic(ImVec2(Centre.x - 6.0f, Centre.y + 4.0f), ImVec2(Centre.x, Centre.y - 8.0f),
                                         ImVec2(Centre.x + 6.0f, Centre.y + 4.0f), TintColor, 1.4f);
            DrawList->AddCircleFilled(ImVec2(Centre.x - 6.0f, Centre.y + 4.0f), 1.6f, TintColor);
            DrawList->AddCircleFilled(ImVec2(Centre.x + 6.0f, Centre.y + 4.0f), 1.6f, TintColor);
        }
        else if (std::strcmp(IconKey, "cad-constraint") == 0)
        {
            DrawList->AddLine(ImVec2(Centre.x - 5.0f, Centre.y - 5.0f), ImVec2(Centre.x + 5.0f, Centre.y + 5.0f), TintColor, 1.2f);
            DrawList->AddCircle(ImVec2(Centre.x - 3.5f, Centre.y - 3.5f), 2.2f, TintColor, 0, Thickness);
            DrawList->AddCircle(ImVec2(Centre.x + 3.5f, Centre.y + 3.5f), 2.2f, TintColor, 0, Thickness);
        }
        else if (std::strcmp(IconKey, "cad-dimension") == 0)
        {
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y), ImVec2(Centre.x + 6.0f, Centre.y), TintColor, 1.2f);
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y - 4.0f), ImVec2(Centre.x - 6.0f, Centre.y + 4.0f), TintColor, 1.2f);
            DrawList->AddLine(ImVec2(Centre.x + 6.0f, Centre.y - 4.0f), ImVec2(Centre.x + 6.0f, Centre.y + 4.0f), TintColor, 1.2f);
            DrawList->AddLine(ImVec2(Centre.x - 6.0f, Centre.y), ImVec2(Centre.x - 3.0f, Centre.y - 2.0f), TintColor, 1.0f);
            DrawList->AddLine(ImVec2(Centre.x + 6.0f, Centre.y), ImVec2(Centre.x + 3.0f, Centre.y + 2.0f), TintColor, 1.0f);
        }
        else if (std::strcmp(IconKey, "cad-feature") == 0)
        {
            float H = 5.5f;
            DrawList->AddRect(ImVec2(Centre.x - H, Centre.y - H * 0.4f), ImVec2(Centre.x + H * 0.4f, Centre.y + H), TintColor, 0.0f, 0, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - H, Centre.y - H * 0.4f), ImVec2(Centre.x - H * 0.3f, Centre.y - H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H * 0.4f, Centre.y - H * 0.4f), ImVec2(Centre.x + H, Centre.y - H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - H * 0.3f, Centre.y - H), ImVec2(Centre.x + H, Centre.y - H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H * 0.4f, Centre.y + H), ImVec2(Centre.x + H, Centre.y + H * 0.2f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H, Centre.y - H), ImVec2(Centre.x + H, Centre.y + H * 0.2f), TintColor, Thickness);
        }
        else if (std::strcmp(IconKey, "cad-component") == 0 || std::strcmp(IconKey, "cad-mate") == 0)
        {
            float H = 6.0f;
            DrawList->AddRect(ImVec2(Centre.x - H, Centre.y - H * 0.4f), ImVec2(Centre.x + H * 0.4f, Centre.y + H), TintColor, 0.0f, 0, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - H, Centre.y - H * 0.4f), ImVec2(Centre.x - H * 0.3f, Centre.y - H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H * 0.4f, Centre.y - H * 0.4f), ImVec2(Centre.x + H, Centre.y - H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - H * 0.3f, Centre.y - H), ImVec2(Centre.x + H, Centre.y - H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H, Centre.y - H), ImVec2(Centre.x + H, Centre.y + H * 0.2f), TintColor, Thickness);
        }
        else
        {
            DrawList->AddCircle(Centre, 5.0f, TintColor, 0, Thickness);
        }
    }
}


const OutlinerContentProfile& ResolveSketchContentProfile()
{
    static const OutlinerContentProfile Profile =
    {
        "SKETCH",                                                       // HeaderCaption
        "Filter sketch...",                                             // SearchHint
        ClassRows,        (int)(sizeof(ClassRows) / sizeof(ClassRows[0])),
        AddRows,          (int)(sizeof(AddRows) / sizeof(AddRows[0])),
        FilterClassIds,   (int)(sizeof(FilterClassIds) / sizeof(FilterClassIds[0])),
        TintFacets,       (int)(sizeof(TintFacets) / sizeof(TintFacets[0])),
        &SeedSketchSample,
        &PaintSketchGlyph,
    };
    return Profile;
}

}   // namespace Frontier::SketchOutlinerUi
