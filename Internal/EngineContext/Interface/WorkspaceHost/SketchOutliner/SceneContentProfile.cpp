/*==============================================================================================================================================
                                                            SCENECONTENTPROFILE.CPP
==============================================================================================================================================*/
// 🧩 The scene content profile for the shared outliner panel: the Scene / Environment / Cameras / Lights / Geometry vocabulary, the "scene-"
//    registry icon keys, the scene add catalogue and filter facets, the default demonstration tree, and the procedural stroke art for every
//    scene glyph. Lifted verbatim from the retired SceneDirectoryPanel so the scene outliner keeps its own icons + labels + sample while running
//    the one shared panel. The panel names none of this — it reads it through the OutlinerContentProfile ResolveSceneContentProfile() returns.

#include "OutlinerContentProfile.h"
#include "SketchOutlinerPanel.h"

#include "imgui.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace Frontier::SketchOutlinerUi
{

namespace
{
    //---------------------------------------------------- CLASSIFICATION IDS ----------------------------------------------------

    // 📝 The scene classification vocabulary as opaque ids (its own numbering, unrelated to the sketch profile's).
    enum SceneClass : int
    {
        SceneRoot = 0,
        EnclosureFolder,
        PolygonSurface,
        LightEmitter,
        CameraLens,
        EnvironmentDome,
    };

    // -- Per-classification icon tints (the scene stroke colours) --
    const ImU32 TintScene       = IM_COL32(0xc9, 0xc9, 0xcf, 255);
    const ImU32 TintEnvironment = IM_COL32(0x4f, 0xb0, 0xe0, 255);
    const ImU32 TintCamera      = IM_COL32(0x5a, 0x95, 0xdd, 255);
    const ImU32 TintSun         = IM_COL32(0xe0, 0xb6, 0x4f, 255);
    const ImU32 TintArea        = IM_COL32(0xc7, 0x74, 0xe0, 255);
    const ImU32 TintSurface     = IM_COL32(0xb9, 0xb9, 0xc0, 255);
    const ImU32 TintFolder      = IM_COL32(0xd0, 0x8a, 0x4f, 255);


    //---------------------------------------------------- PROFILE TABLES ----------------------------------------------------

    // 📝 Classification table: the scene row's icon key / tint / label / container flag by id.
    const OutlinerClassRow ClassRows[] =
    {
        { SceneRoot,        "Scene",       "scene-root",        TintScene,       true  },
        { EnclosureFolder,  "Folders",     "g-folder",          TintFolder,      true  },
        { PolygonSurface,   "Meshes",      "scene-mesh",        TintSurface,     false },
        { LightEmitter,     "Lights",      "scene-sun",         TintSun,         false },
        { CameraLens,       "Cameras",     "scene-camera",      TintCamera,      false },
        { EnvironmentDome,  "Environment", "scene-environment", TintEnvironment, false },
    };

    // 📝 The scene add-object catalogue: Geometry (Mesh, Folder) · Lights (Sun, Area) · Scene (Camera, Environment).
    const OutlinerAddRow AddRows[] =
    {
        { "Geometry", nullptr,        nullptr,       SceneRoot        },
        { nullptr,    "Mesh",         "Mesh",        PolygonSurface   },
        { nullptr,    "Folder",       "Folder",      EnclosureFolder  },
        { "Lights",   nullptr,        nullptr,       SceneRoot        },
        { nullptr,    "Sun Light",    "Light",       LightEmitter     },
        { nullptr,    "Area Light",   "Light",       LightEmitter     },
        { "Scene",    nullptr,        nullptr,       SceneRoot        },
        { nullptr,    "Camera",       "Camera",      CameraLens       },
        { nullptr,    "Environment",  "Environment", EnvironmentDome  },
    };

    // 📝 Which classification ids appear in the TYPES filter section (the Scene/Folder containers are excluded).
    const int FilterClassIds[] = { PolygonSurface, LightEmitter, CameraLens, EnvironmentDome, EnclosureFolder };

    // 📝 The COLOURS filter swatches.
    const OutlinerTintFacet TintFacets[] =
    {
        { TintSurface,     "Grey" },
        { TintCamera,      "Blue" },
        { TintEnvironment, "Sky" },
        { TintSun,         "Yellow" },
        { TintArea,        "Purple" },
        { TintFolder,      "Orange" },
    };


    //---------------------------------------------------- SAMPLE SEED ----------------------------------------------------

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

    // 📝 The Scene → Environment / Cameras / Lights / Geometry demonstration tree, lifted from SceneDirectoryPanel; SM_Body pre-selected.
    void SeedSceneSample(SketchOutlinerState& State, const OutlinerContentProfile& /*Profile*/)
    {
        State.RootRegion.clear();
        State.SelectionSet.clear();

        RecordEntry Scene = MakeEntry(State, "Scene", SceneRoot, "scene-root", TintScene, true);

        RecordEntry Environment = MakeEntry(State, "Environment", EnclosureFolder, "scene-environment", TintEnvironment, true);
        Environment.NestedRegion.push_back(MakeEntry(State, "HDRI_Studio", EnvironmentDome, "scene-environment", TintEnvironment, false));

        RecordEntry Cameras = MakeEntry(State, "Cameras", EnclosureFolder, "scene-camera", TintCamera, true);
        Cameras.NestedRegion.push_back(MakeEntry(State, "Camera_Main", CameraLens, "scene-camera", TintCamera, false));

        RecordEntry Lights = MakeEntry(State, "Lights", EnclosureFolder, "scene-sun", TintSun, true);
        Lights.NestedRegion.push_back(MakeEntry(State, "Sun_Key", LightEmitter, "scene-sun", TintSun, false));
        Lights.NestedRegion.push_back(MakeEntry(State, "Area_Softbox", LightEmitter, "scene-area", TintArea, false));

        RecordEntry Geometry = MakeEntry(State, "Geometry", EnclosureFolder, "g-folder", TintSurface, true);
        RecordEntry CarGroup = MakeEntry(State, "Car", EnclosureFolder, "g-folder", TintFolder, true);
        CarGroup.NestedRegion.push_back(MakeEntry(State, "SM_Body", PolygonSurface, "scene-mesh", TintSurface, false));
        CarGroup.NestedRegion.push_back(MakeEntry(State, "SM_Wheels", PolygonSurface, "scene-mesh", TintSurface, false));
        Geometry.NestedRegion.push_back(std::move(CarGroup));

        Scene.NestedRegion.push_back(std::move(Environment));
        Scene.NestedRegion.push_back(std::move(Cameras));
        Scene.NestedRegion.push_back(std::move(Lights));
        Scene.NestedRegion.push_back(std::move(Geometry));

        State.RootRegion.push_back(std::move(Scene));

        struct Locator
        {
            static RecordToken Resolve(const std::vector<RecordEntry>& Region)
            {
                for (const RecordEntry& Entry : Region)
                {
                    if (Entry.Label == "SM_Body") { return Entry.Token; }
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

    // 📝 The procedural stroke fallback for every scene icon key, drawn centred in an 18px box at Origin, tinted by TintColor. Switches on the
    //    registry key the row carries directly (scene-root / scene-mesh / scene-sun / scene-area / scene-camera / scene-environment / g-folder).
    void PaintSceneGlyph(ImDrawList* DrawList, const char* IconKey, ImVec2 Origin, ImU32 TintColor)
    {
        const float Box = 18.0f;
        ImVec2 Centre = ImVec2(Origin.x + Box * 0.5f, Origin.y + Box * 0.5f);
        const float Thickness = 1.3f;
        if (IconKey == nullptr) { IconKey = ""; }

        auto Rect = [&](float HalfW, float HalfH, float Rounding)
        {
            DrawList->AddRect(ImVec2(Centre.x - HalfW, Centre.y - HalfH),
                              ImVec2(Centre.x + HalfW, Centre.y + HalfH), TintColor, Rounding, 0, Thickness);
        };

        if (std::strcmp(IconKey, "scene-root") == 0)
        {
            DrawList->AddCircle(Centre, 6.3f, TintColor, 0, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - 6.3f, Centre.y), ImVec2(Centre.x + 6.3f, Centre.y), TintColor, 1.1f);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - 6.3f), ImVec2(Centre.x, Centre.y + 6.3f), TintColor, 1.1f);
            DrawList->AddBezierQuadratic(ImVec2(Centre.x - 6.3f, Centre.y), Centre, ImVec2(Centre.x + 6.3f, Centre.y), TintColor, 1.1f);
        }
        else if (std::strcmp(IconKey, "g-folder") == 0 || std::strcmp(IconKey, "g-folder-open") == 0)
        {
            ImVec2 Min = ImVec2(Centre.x - 6.5f, Centre.y - 4.5f);
            ImVec2 Max = ImVec2(Centre.x + 6.5f, Centre.y + 5.0f);
            DrawList->AddRect(Min, Max, TintColor, 1.5f, 0, Thickness);
            DrawList->AddLine(ImVec2(Min.x, Min.y), ImVec2(Min.x + 3.5f, Min.y - 2.0f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Min.x + 3.5f, Min.y - 2.0f), ImVec2(Min.x + 6.0f, Min.y), TintColor, Thickness);
        }
        else if (std::strcmp(IconKey, "scene-mesh") == 0)
        {
            float H = 6.2f;
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - H), ImVec2(Centre.x + H, Centre.y - H * 0.45f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H, Centre.y - H * 0.45f), ImVec2(Centre.x + H, Centre.y + H * 0.55f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x + H, Centre.y + H * 0.55f), ImVec2(Centre.x, Centre.y + H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y + H), ImVec2(Centre.x - H, Centre.y + H * 0.55f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - H, Centre.y + H * 0.55f), ImVec2(Centre.x - H, Centre.y - H * 0.45f), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x - H, Centre.y - H * 0.45f), ImVec2(Centre.x, Centre.y - H), TintColor, Thickness);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y - H), ImVec2(Centre.x, Centre.y + H), TintColor, 1.1f);
            DrawList->AddLine(ImVec2(Centre.x - H, Centre.y - H * 0.45f), ImVec2(Centre.x, Centre.y), TintColor, 1.1f);
            DrawList->AddLine(ImVec2(Centre.x, Centre.y), ImVec2(Centre.x + H, Centre.y - H * 0.45f), TintColor, 1.1f);
        }
        else if (std::strcmp(IconKey, "scene-sun") == 0)
        {
            DrawList->AddCircle(Centre, 3.0f, TintColor, 0, Thickness);
            for (int Ray = 0; Ray < 8; ++Ray)
            {
                float Angle = (float)Ray * 0.785398f;
                ImVec2 Inner = ImVec2(Centre.x + std::cos(Angle) * 5.0f, Centre.y + std::sin(Angle) * 5.0f);
                ImVec2 Outer = ImVec2(Centre.x + std::cos(Angle) * 7.0f, Centre.y + std::sin(Angle) * 7.0f);
                DrawList->AddLine(Inner, Outer, TintColor, 1.2f);
            }
        }
        else if (std::strcmp(IconKey, "scene-area") == 0)
        {
            Rect(5.0f, 5.0f, 1.0f);
        }
        else if (std::strcmp(IconKey, "scene-camera") == 0)
        {
            Rect(6.5f, 4.0f, 1.5f);
            DrawList->AddCircle(Centre, 2.3f, TintColor, 0, Thickness);
        }
        else if (std::strcmp(IconKey, "scene-environment") == 0)
        {
            ImVec2 Min = ImVec2(Centre.x - 6.0f, Centre.y - 5.0f);
            ImVec2 Max = ImVec2(Centre.x + 6.0f, Centre.y + 5.0f);
            DrawList->AddRect(Min, Max, TintColor, 1.5f, 0, Thickness);
            DrawList->AddCircleFilled(ImVec2(Min.x + 3.0f, Min.y + 3.0f), 1.4f, TintColor);
            DrawList->AddLine(ImVec2(Min.x, Max.y - 1.0f), ImVec2(Min.x + 4.0f, Centre.y), TintColor, 1.2f);
            DrawList->AddLine(ImVec2(Min.x + 4.0f, Centre.y), ImVec2(Max.x - 3.0f, Max.y - 2.0f), TintColor, 1.2f);
            DrawList->AddLine(ImVec2(Max.x - 3.0f, Max.y - 2.0f), ImVec2(Max.x, Centre.y + 1.0f), TintColor, 1.2f);
        }
        else
        {
            DrawList->AddCircle(Centre, 5.0f, TintColor, 0, Thickness);
        }
    }
}


const OutlinerContentProfile& ResolveSceneContentProfile()
{
    static const OutlinerContentProfile Profile =
    {
        "SCENE",                                                        // HeaderCaption
        "Filter scene...",                                             // SearchHint
        ClassRows,        (int)(sizeof(ClassRows) / sizeof(ClassRows[0])),
        AddRows,          (int)(sizeof(AddRows) / sizeof(AddRows[0])),
        FilterClassIds,   (int)(sizeof(FilterClassIds) / sizeof(FilterClassIds[0])),
        TintFacets,       (int)(sizeof(TintFacets) / sizeof(TintFacets[0])),
        &SeedSceneSample,
        &PaintSceneGlyph,
    };
    return Profile;
}

}   // namespace Frontier::SketchOutlinerUi
