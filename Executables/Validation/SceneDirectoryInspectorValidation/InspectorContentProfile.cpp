/*==============================================================================================================================================
                                                    INSPECTORCONTENTPROFILE.CPP
==============================================================================================================================================*/
// 🧩 The scene-directory inspector's content profile for the shared outliner panel. Every table here is derived from SceneDirectoryInspector's own
//    resolvers (ClassificationLabel · ClassificationHue · ClassificationKey · AddChoices) rather than restated, so the outliner's vocabulary and
//    the property cards' vocabulary cannot drift: the opaque ClassificationId the panel stores on a row IS the SDI RecordClassification. A row
//    born in the add menu therefore already carries the classification the cards read, with no side-table to populate. SeedSample deliberately
//    leaves the tree EMPTY — the workspace opens with no authored content, and the shared panel's add path lands rows at the root region when
//    RootRegion is empty, so the first add works with nothing seeded.

#include "InspectorContentProfile.h"

#include "InspectorGlyphs.h"

// 📝 SeedInspectorSample touches SketchOutlinerState's fields (RootRegion / SelectionSet / FilterChips / …), so it needs the full struct — the
//    content-profile header only forward-declares it. This is the panel header that defines it.
#include "EngineContext/Interface/WorkspaceHost/SketchOutliner/SketchOutlinerPanel.h"

#include "imgui.h"

#include <cstring>

namespace SceneDirectoryInspectorValidation
{

namespace SO = Frontier::SketchOutlinerUi;

//------------------------------------------------------------------------------------------------------------------------
//                                                      INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    //---------------------------------------------------- TABLE ASSEMBLY ----------------------------------------------------

    // 📝 The ten classifications in enum order. The profile's tables are built over this list so adding a classification to the model shows up
    //    in the outliner's rows, filter chips and colour swatches without a second edit here.
    const RecordClassification ClassificationOrder[10] =
    {
        RecordClassification::Scene,    RecordClassification::Folder,   RecordClassification::Sketch,
        RecordClassification::Solid,    RecordClassification::Cylinder, RecordClassification::Sphere,
        RecordClassification::Cone,     RecordClassification::Revolve,  RecordClassification::Loft,
        RecordClassification::Workplane
    };

    // The glyph keys the rows carry. ClassificationGlyphKey returns a std::string by value, so the key text is held here for the profile's
    // lifetime — OutlinerClassRow stores a bare const char*, which must outlive every frame that reads it.
    std::string ClassKeyStore[10];

    // The assembled tables, filled once by BuildTables below and pointed at by the returned profile.
    SO::OutlinerClassRow  ClassRows[10]     = {};
    SO::OutlinerAddRow    AddRows[10]       = {};
    int                   FilterClassIds[9] = {};
    SO::OutlinerTintFacet TintFacets[9]     = {};

    // 📝 Scene is the document root and Folder is a body: both hold nested rows, so the panel treats them as containers (a twisty, a drop target,
    //    and the destination an add resolves to). Every other classification is a leaf.
    bool ContainerClassification(RecordClassification Classification)
    {
        return Classification == RecordClassification::Scene || Classification == RecordClassification::Folder;
    }

    // The tint a row draws with: the SDI hue, opaque. ClassificationHue already packs 0xAABBGGRR with a full alpha, so this is its value.
    ImU32 ClassificationTint(RecordClassification Classification)
    {
        return static_cast<ImU32>(0xFF000000u | ClassificationHue(Classification));
    }

    //---------------------------------------------------- SAMPLE SEED ----------------------------------------------------

    // 📝 The empty pose: no rows, no selection, no rename, token counter back to 1. This is the authored opening content — the workspace starts
    //    blank and everything in the tree arrives through the add menu. 🔴 Deliberately seeds NOTHING; the shared panel's ResolveAddDestination
    //    falls through to the root region when RootRegion is empty, so an add from this pose lands a top-tier row rather than being dropped.
    void SeedInspectorSample(SO::SketchOutlinerState& State, const SO::OutlinerContentProfile& /*Profile*/)
    {
        State.RootRegion.clear();
        State.SelectionSet.clear();
        State.FilterChips.clear();
        State.RangeAnchor   = 0;
        State.RenameTarget  = 0;
        State.NextToken     = 1;
        State.SearchText[0] = '\0';
    }

    //---------------------------------------------------- FALLBACK ART ----------------------------------------------------

    // 📝 The stroke fallback for an SDI classification glyph, drawn centred in an 18 px box at Origin. It matches the reference mark the
    //    inspector's own glyph pack rasterizes (a rounded square with a centred dot), so a row reads identically whether the registry uploaded
    //    or not. The classification still reads through the tint, which is per-classification.
    void PaintInspectorGlyph(ImDrawList* DrawList, const char* /*IconKey*/, ImVec2 Origin, ImU32 TintColor)
    {
        const float Box = 18.0f;                                                   // [px] - the row's glyph cell
        const ImVec2 Min = ImVec2(Origin.x + Box * 0.18f, Origin.y + Box * 0.18f);
        const ImVec2 Max = ImVec2(Origin.x + Box * 0.82f, Origin.y + Box * 0.82f);
        DrawList->AddRect(Min, Max, TintColor, Box * 0.16f, 0, 1.4f);
        DrawList->AddCircleFilled(ImVec2(Origin.x + Box * 0.5f, Origin.y + Box * 0.5f), Box * 0.09f, TintColor);
    }

    //---------------------------------------------------- ASSEMBLY ----------------------------------------------------

    // 📝 Fill every table from the model's own resolvers. Runs once, under the static initializer in ResolveInspectorContentProfile.
    void BuildTables()
    {
        for (int Index = 0; Index < 10; ++Index)
        {
            const RecordClassification Classification = ClassificationOrder[Index];
            ClassKeyStore[Index] = ClassificationGlyphKey(Classification);

            ClassRows[Index].ClassificationId = static_cast<int>(Classification);
            ClassRows[Index].Label            = ClassificationLabel(Classification);
            ClassRows[Index].IconKey          = ClassKeyStore[Index].c_str();
            ClassRows[Index].Tint             = ClassificationTint(Classification);
            ClassRows[Index].Container        = ContainerClassification(Classification);
        }

        // 📝 The type chips + colour swatches cover the nine creatable classifications, in the model's own AddChoices order; Scene is the
        //    document root — it is never added and never worth filtering for, so it appears in neither facet.
        for (int Index = 0; Index < 9; ++Index)
        {
            const RecordClassification Classification = AddChoices[Index];
            FilterClassIds[Index]  = static_cast<int>(Classification);
            TintFacets[Index].Tint  = ClassificationTint(Classification);
            TintFacets[Index].Label = ClassificationLabel(Classification);
        }

        // 📝 The add catalogue: one section head, then the nine creatable classifications in the model's own AddChoices order. The stem is the
        //    classification label (a string literal out of the model, so it outlives the profile) — the shared panel's ResolveUniqueName suffixes
        //    it, so "Body", "Body 2", "Body 3" fall out on repeat adds.
        AddRows[0].Section          = "Create";
        AddRows[0].Label            = nullptr;
        AddRows[0].Stem             = nullptr;
        AddRows[0].ClassificationId = static_cast<int>(RecordClassification::Scene);
        for (int Index = 0; Index < 9; ++Index)
        {
            const RecordClassification Classification = AddChoices[Index];
            AddRows[Index + 1].Section          = nullptr;
            AddRows[Index + 1].Label            = ClassificationLabel(Classification);
            AddRows[Index + 1].Stem             = ClassificationLabel(Classification);
            AddRows[Index + 1].ClassificationId = static_cast<int>(Classification);
        }
    }
}


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

const SO::OutlinerContentProfile& ResolveInspectorContentProfile()
{
    static const SO::OutlinerContentProfile Profile = []() -> SO::OutlinerContentProfile
    {
        BuildTables();
        SO::OutlinerContentProfile Assembled = {};
        Assembled.HeaderCaption      = "DIRECTORY";
        Assembled.SearchHint         = "Filter records...";
        Assembled.ClassRows          = ClassRows;
        Assembled.ClassRowCount      = 10;
        Assembled.AddRows            = AddRows;
        Assembled.AddRowCount        = 10;
        Assembled.FilterClassIds     = FilterClassIds;
        Assembled.FilterClassIdCount = 9;
        Assembled.TintFacets         = TintFacets;
        Assembled.TintFacetCount     = 9;
        Assembled.SeedSample         = &SeedInspectorSample;
        Assembled.PaintFallbackGlyph = &PaintInspectorGlyph;
        return Assembled;
    }();
    return Profile;
}


RecordClassification ResolveClassificationOfId(int ClassificationId)
{
    if (ClassificationId >= static_cast<int>(RecordClassification::Scene) &&
        ClassificationId <= static_cast<int>(RecordClassification::Workplane))
    {
        return static_cast<RecordClassification>(ClassificationId);
    }
    return RecordClassification::Solid;   // a safe card schema when a row carries an id outside the vocabulary
}

}   // namespace SceneDirectoryInspectorValidation
