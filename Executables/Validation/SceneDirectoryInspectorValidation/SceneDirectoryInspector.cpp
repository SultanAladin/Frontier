/*==============================================================================================================================================
                                                    SCENEDIRECTORYINSPECTOR.CPP
==============================================================================================================================================*/
// 🧩 The property + history half of the scene-directory inspector, ported 1:1 from the prototype JS. Classification hue / label / glyph
//    tables, the establishProfile field seeds, the profileCards card tables (the load-bearing part — each classification's exact cards and
//    field rows), and the branching RevisionStore with fork-on-record. 🔴 The directory tree lives in the reused SketchOutliner panel, so the
//    resolvers take the selection it surfaces (classification + child count + visibility), not an SDI tree node.

#include "SceneDirectoryInspector.h"

#include <string>

namespace SceneDirectoryInspectorValidation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

const RecordClassification AddChoices[9] =
{
    RecordClassification::Folder,  RecordClassification::Sketch, RecordClassification::Solid,
    RecordClassification::Cylinder,RecordClassification::Sphere, RecordClassification::Cone,
    RecordClassification::Revolve, RecordClassification::Loft,   RecordClassification::Workplane
};

namespace
{
    // 📝 The palette (C in the prototype) packed as 0xAABBGGRR ImU32. Each hex #RRGGBB becomes 0xFF00'0000 | B<<16 | G<<8 | R.
    constexpr std::uint32_t Packed(std::uint8_t R, std::uint8_t G, std::uint8_t B)
    {
        return 0xFF000000u | (static_cast<std::uint32_t>(B) << 16) | (static_cast<std::uint32_t>(G) << 8) | R;
    }

    // The classification hues (CLASSIFICATION_HUE) — sky / violet / cyan / amber / green / pink / red / earth / blue.
    constexpr std::uint32_t HueScene    = Packed(0x7e, 0xc8, 0xff);  // sky
    constexpr std::uint32_t HueFolder   = Packed(0xb9, 0x8b, 0xff);  // violet
    constexpr std::uint32_t HueSketch   = Packed(0x37, 0xd6, 0xd6);  // cyan
    constexpr std::uint32_t HueSolid    = Packed(0xff, 0xb2, 0x4d);  // amber
    constexpr std::uint32_t HueCylinder = Packed(0x4f, 0xd1, 0x8b);  // green
    constexpr std::uint32_t HueSphere   = Packed(0xff, 0x7a, 0xb8);  // pink
    constexpr std::uint32_t HueCone     = Packed(0xff, 0x6b, 0x6b);  // red
    constexpr std::uint32_t HueRevolve  = Packed(0xc9, 0x9b, 0x6a);  // earth
    constexpr std::uint32_t HueLoft     = Packed(0x5b, 0x8c, 0xff);  // blue
    constexpr std::uint32_t HueWorkplane= Packed(0x3f, 0xc8, 0xc0);  // teal
    constexpr std::uint32_t HueDim      = Packed(0x8a, 0x8a, 0x99);  // dim (fallback)
}

std::uint32_t ClassificationHue(RecordClassification Classification)
{
    switch (Classification)
    {
        case RecordClassification::Scene:    return HueScene;
        case RecordClassification::Folder:   return HueFolder;
        case RecordClassification::Sketch:   return HueSketch;
        case RecordClassification::Solid:    return HueSolid;
        case RecordClassification::Cylinder: return HueCylinder;
        case RecordClassification::Sphere:   return HueSphere;
        case RecordClassification::Cone:     return HueCone;
        case RecordClassification::Revolve:  return HueRevolve;
        case RecordClassification::Loft:     return HueLoft;
        case RecordClassification::Workplane:return HueWorkplane;
    }
    return HueDim;
}

const char* ClassificationLabel(RecordClassification Classification)
{
    // CLASSIFICATION_LABEL — note scene reads "Part" and folder reads "Body".
    switch (Classification)
    {
        case RecordClassification::Scene:    return "Part";
        case RecordClassification::Folder:   return "Body";
        case RecordClassification::Sketch:   return "Sketch";
        case RecordClassification::Solid:    return "Solid";
        case RecordClassification::Cylinder: return "Cylinder";
        case RecordClassification::Sphere:   return "Sphere";
        case RecordClassification::Cone:     return "Cone";
        case RecordClassification::Revolve:  return "Revolve";
        case RecordClassification::Loft:     return "Loft";
        case RecordClassification::Workplane:return "Workplane";
    }
    return "Record";
}

const char* ClassificationKey(RecordClassification Classification)
{
    switch (Classification)
    {
        case RecordClassification::Scene:    return "scene";
        case RecordClassification::Folder:   return "folder";
        case RecordClassification::Sketch:   return "sketch";
        case RecordClassification::Solid:    return "solid";
        case RecordClassification::Cylinder: return "cylinder";
        case RecordClassification::Sphere:   return "sphere";
        case RecordClassification::Cone:     return "cone";
        case RecordClassification::Revolve:  return "revolve";
        case RecordClassification::Loft:     return "loft";
        case RecordClassification::Workplane:return "workplane";
    }
    return "solid";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          RECORD PROFILE
//------------------------------------------------------------------------------------------------------------------------

RecordProfile& EstablishProfile(RecordClassification Classification, int NestedTally, bool Visible, RecordProfile& Profile)
{
    if (Profile.Populated) return Profile;      // the JS `if(entry.profile) return entry.profile`

    // Base defaults already sit on the struct; only Visible + the classification-specific seeds need per-record work.
    Profile.Visible = Visible;

    switch (Classification)
    {
        case RecordClassification::Scene:
            Profile.Units            = 1;
            Profile.ToleranceLinear  = 0.01f;
            Profile.ToleranceAngular = 0.5f;
            Profile.DocumentPath     = "/Projects/Bracket_Rev4.wsdoc";
            break;
        case RecordClassification::Folder:
            Profile.NestedTally = NestedTally;
            Profile.BooleanMode = 0;
            Profile.Suppressed  = false;
            break;
        case RecordClassification::Sketch:
            Profile.PlaneChoice      = 0;
            Profile.ConstraintTally  = 12;
            Profile.CurveTally       = 8;
            Profile.FullyConstrained = false;
            Profile.GridSnap         = 0.5f;
            break;
        case RecordClassification::Solid:
            Profile.ExtrudeDepth  = 12.5f;
            Profile.DraftAngle    = 0.0f;
            Profile.WallThickness = 2.5f;
            Profile.CappedEnds    = true;
            break;
        case RecordClassification::Cylinder:
            Profile.Radius       = 6.25f;
            Profile.Height       = 18.0f;
            Profile.SegmentTally = 32;
            Profile.CappedEnds   = true;
            break;
        case RecordClassification::Sphere:
            Profile.Radius       = 8.4f;
            Profile.SegmentTally = 48;
            Profile.RingTally    = 24;
            break;
        case RecordClassification::Cone:
            Profile.BaseRadius   = 7.0f;
            Profile.TipRadius    = 0.0f;
            Profile.Height       = 16.0f;
            Profile.SegmentTally = 32;
            break;
        case RecordClassification::Revolve:
            Profile.SweepAngle    = 360.0f;
            Profile.AxisChoice    = 1;
            Profile.ProfileClosed = true;
            break;
        case RecordClassification::Loft:
            Profile.SectionTally  = 3;
            Profile.TangencyStart = 0.0f;
            Profile.TangencyEnd   = 0.0f;
            Profile.Ruled         = false;
            break;
        case RecordClassification::Workplane:
            Profile.PlaneMethod      = 0;      // XY default
            Profile.PlaneOffset      = 0.0f;
            Profile.PlaneAngle       = 0.0f;
            Profile.PlaneAnglePivot  = 0;      // U-axis hinge (tilt forward / back)
            Profile.FlipNormal       = false;
            Profile.PlaneExtent      = 2000.0f;  // [mm] 2 m half → 4 m sheet, readable at the ~18 m boot orbit (200 mm was invisibly small)
            Profile.PlaneGrid        = true;
            Profile.PlaneGridSpacing = 200.0f;   // [mm] 0.2 m cells → ~20 across the sheet (matches Workplane.h default)
            Profile.PlaneSnap        = true;
            Profile.PlaneLock        = false;
            break;
    }

    Profile.Populated = true;
    return Profile;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          CARD TABLES
//------------------------------------------------------------------------------------------------------------------------
// 📝 The card + field spec tables, transcribed 1:1 from profileCards / TRANSFORM_CARD / APPEARANCE_CARD. Small builder helpers keep the
//    field literals readable; every step / fmt / min / max / unit / option list matches the prototype exactly.

namespace
{
    FieldSpec Text(const char* Key, const char* Label)
    {
        FieldSpec F; F.Control = FieldControl::Text; F.Key = Key; F.Label = Label; return F;
    }
    FieldSpec Boolean(const char* Key, const char* Label)
    {
        FieldSpec F; F.Control = FieldControl::Boolean; F.Key = Key; F.Label = Label; return F;
    }
    FieldSpec Vector(const char* Key, const char* Label, float Step, int Format)
    {
        FieldSpec F; F.Control = FieldControl::Vector; F.Key = Key; F.Label = Label; F.Step = Step; F.Format = Format; return F;
    }
    FieldSpec Scalar(const char* Key, const char* Label, float Step, int Format, const char* Unit)
    {
        FieldSpec F; F.Control = FieldControl::Scalar; F.Key = Key; F.Label = Label; F.Step = Step; F.Format = Format; F.Unit = Unit; return F;
    }
    FieldSpec Slider(const char* Key, const char* Label, float Minimum, float Maximum, int Format, const char* Unit)
    {
        FieldSpec F; F.Control = FieldControl::Slider; F.Key = Key; F.Label = Label;
        F.Minimum = Minimum; F.Maximum = Maximum; F.Format = Format; F.Unit = Unit; return F;
    }
    FieldSpec Colour(const char* Key, const char* Label)
    {
        FieldSpec F; F.Control = FieldControl::Colour; F.Key = Key; F.Label = Label; return F;
    }
    FieldSpec Path(const char* Key, const char* Label)
    {
        FieldSpec F; F.Control = FieldControl::Path; F.Key = Key; F.Label = Label; return F;
    }
    FieldSpec Options(FieldControl Control, const char* Key, const char* Label,
                      std::initializer_list<const char*> Opts)
    {
        FieldSpec F; F.Control = Control; F.Key = Key; F.Label = Label;
        int Index = 0;
        for (const char* Opt : Opts) { if (Index < 8) F.Options[Index++] = Opt; }
        F.OptionCount = Index;
        return F;
    }

    CardSpec MakeCard(const char* Title, std::initializer_list<FieldSpec> Fields)
    {
        CardSpec Card; Card.Title = Title; Card.FieldCount = 0;
        for (const FieldSpec& Field : Fields) { if (Card.FieldCount < 8) Card.Fields[Card.FieldCount++] = Field; }
        return Card;
    }

    // TRANSFORM_CARD and APPEARANCE_CARD, shared by the solid-family classifications.
    CardSpec TransformCard()
    {
        return MakeCard("Transform", {
            Vector("Position", "Position", 0.1f,  2),
            Vector("Rotation", "Rotation", 0.5f,  1),
            Vector("Scale",    "Scale",    0.01f, 3),
        });
    }
    CardSpec AppearanceCard()
    {
        return MakeCard("Appearance", {
            Colour("Albedo", "Albedo"),
            Slider("Roughness", "Roughness", 0.0f, 1.0f, 2, "\xC2\xB7"),
            Slider("Metalness", "Metalness", 0.0f, 1.0f, 2, "\xC2\xB7"),
            Options(FieldControl::Dropdown, "ShadingMode", "Shading", { "Smooth", "Faceted", "Flat" }),
        });
    }
}

int ResolveProfileCards(RecordClassification Classification, CardSpec OutCards[8])
{
    int Count = 0;
    auto Push = [&](const CardSpec& Card) { if (Count < 8) OutCards[Count++] = Card; };

    // The Record identity card first, always (with a per-classification extra pushed for scene).
    CardSpec Identity = MakeCard("Record", {
        Text("Name", "Name"),
        Boolean("Visible", "Visible"),
    });

    switch (Classification)
    {
        case RecordClassification::Scene:
            // scene pushes a Units dropdown onto the identity card, then a Tolerance card.
            Identity.Fields[Identity.FieldCount++] =
                Options(FieldControl::Dropdown, "Units", "Units",
                        { "Inches", "Millimetres", "Centimetres", "Metres" });
            Push(Identity);
            Push(MakeCard("Tolerance", {
                Scalar("ToleranceLinear",  "Linear",  0.001f, 3, "mm"),
                Scalar("ToleranceAngular", "Angular", 0.1f,   1, "\xC2\xB0"),
                Path("DocumentPath", "Document"),
            }));
            break;

        case RecordClassification::Folder:
            Push(Identity);
            Push(MakeCard("Group", {
                Options(FieldControl::Selection, "BooleanMode", "Boolean", { "Union", "Subtract", "Intersect" }),
                Boolean("Suppressed", "Suppress"),
                Scalar("NestedTally", "Records", 1.0f, 0, "ct"),
            }));
            Push(TransformCard());
            break;

        case RecordClassification::Sketch:
            Push(Identity);
            Push(MakeCard("Sketch", {
                Options(FieldControl::Dropdown, "PlaneChoice", "Plane", { "XY Plane", "XZ Plane", "YZ Plane", "Custom" }),
                Slider("GridSnap", "Snap", 0.1f, 5.0f, 2, "mm"),
                Scalar("CurveTally", "Curves", 1.0f, 0, "ct"),
                Scalar("ConstraintTally", "Constraints", 1.0f, 0, "ct"),
                Boolean("FullyConstrained", "Constrained"),
            }));
            Push(TransformCard());
            break;

        case RecordClassification::Solid:
            Push(Identity);
            Push(MakeCard("Extrusion", {
                Scalar("ExtrudeDepth", "Depth", 0.1f, 2, "mm"),
                Slider("DraftAngle", "Draft", -30.0f, 30.0f, 1, "\xC2\xB0"),
                Scalar("WallThickness", "Wall", 0.1f, 2, "mm"),
                Boolean("CappedEnds", "Cap ends"),
            }));
            Push(TransformCard());
            Push(AppearanceCard());
            break;

        case RecordClassification::Cylinder:
            Push(Identity);
            Push(MakeCard("Cylinder", {
                Scalar("Radius", "Radius", 0.05f, 2, "mm"),
                Scalar("Height", "Height", 0.1f,  2, "mm"),
                Slider("SegmentTally", "Segments", 6.0f, 128.0f, 0, "ct"),
                Boolean("CappedEnds", "Cap ends"),
            }));
            Push(TransformCard());
            Push(AppearanceCard());
            break;

        case RecordClassification::Sphere:
            Push(Identity);
            Push(MakeCard("Sphere", {
                Scalar("Radius", "Radius", 0.05f, 2, "mm"),
                Slider("SegmentTally", "Segments", 6.0f, 128.0f, 0, "ct"),
                Slider("RingTally", "Rings", 3.0f, 64.0f, 0, "ct"),
            }));
            Push(TransformCard());
            Push(AppearanceCard());
            break;

        case RecordClassification::Cone:
            Push(Identity);
            Push(MakeCard("Cone", {
                Scalar("BaseRadius", "Base R", 0.05f, 2, "mm"),
                Scalar("TipRadius",  "Tip R",  0.05f, 2, "mm"),
                Scalar("Height",     "Height", 0.1f,  2, "mm"),
                Slider("SegmentTally", "Segments", 6.0f, 128.0f, 0, "ct"),
            }));
            Push(TransformCard());
            Push(AppearanceCard());
            break;

        case RecordClassification::Revolve:
            Push(Identity);
            Push(MakeCard("Revolve", {
                Slider("SweepAngle", "Sweep", 1.0f, 360.0f, 0, "\xC2\xB0"),
                Options(FieldControl::Selection, "AxisChoice", "Axis", { "X", "Y", "Z" }),
                Boolean("ProfileClosed", "Closed"),
            }));
            Push(TransformCard());
            Push(AppearanceCard());
            break;

        case RecordClassification::Loft:
            Push(Identity);
            Push(MakeCard("Loft", {
                Scalar("SectionTally", "Sections", 1.0f, 0, "ct"),
                Slider("TangencyStart", "Tan start", 0.0f, 1.0f, 2, "\xC2\xB7"),
                Slider("TangencyEnd",   "Tan end",   0.0f, 1.0f, 2, "\xC2\xB7"),
                Boolean("Ruled", "Ruled"),
            }));
            Push(TransformCard());
            Push(AppearanceCard());
            break;

        case RecordClassification::Workplane:
            // 📝 Two cards mirroring WorkplaneExplainer.html: Definition (parametric — method + offset/angle + flip) and Display
            //    (viewport — extent, grid + spacing, snap, lock). No Transform / Appearance card (the frame IS the transform, solved).
            Push(Identity);
            Push(MakeCard("Definition", {
                Options(FieldControl::Dropdown, "PlaneMethod", "Method",
                        { "XY", "XZ", "YZ", "Offset", "Angle", "3-Point", "Midplane", "Tangent", }),
                Scalar("PlaneOffset", "Offset", 0.1f, 2, "mm"),
                Slider("PlaneAngle",  "Angle", -180.0f, 180.0f, 1, "\xC2\xB0"),
                Options(FieldControl::Dropdown, "PlaneAnglePivot", "Angle axis",
                        { "Tilt (U)", "Tilt (V)", "Roll (N)" }),
                Boolean("FlipNormal", "Flip normal"),
            }));
            Push(MakeCard("Display", {
                Slider("PlaneExtent", "Extent", 200.0f, 10000.0f, 0, "mm"),
                Boolean("PlaneGrid", "Grid"),
                Slider("PlaneGridSpacing", "Spacing", 10.0f, 1000.0f, 0, "mm"),
                Boolean("PlaneSnap", "Snap"),
                Boolean("PlaneLock", "Lock"),
            }));
            break;
    }

    return Count;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          REVISION STORE
//------------------------------------------------------------------------------------------------------------------------

const char* RevisionGlyphName(RevisionCategory Category)
{
    // REVISION_CLASS.glyph — the timeline node icon per category.
    switch (Category)
    {
        case RevisionCategory::Start:     return "Cube";
        case RevisionCategory::Feature:   return "Cube";
        case RevisionCategory::Param:     return "Cylinder";
        case RevisionCategory::Sketch:    return "Draw Sketch";
        case RevisionCategory::Transform: return "Loft";
        case RevisionCategory::Body:      return "Folder";
        case RevisionCategory::Add:       return "Layers";
        case RevisionCategory::Edit:      return "Revolve";
        case RevisionCategory::Drop:      return "Cone";
    }
    return "Cube";
}

const char* RevisionLabel(RevisionCategory Category)
{
    switch (Category)
    {
        case RevisionCategory::Start:     return "Start";
        case RevisionCategory::Feature:   return "Feature";
        case RevisionCategory::Param:     return "Params";
        case RevisionCategory::Sketch:    return "Sketch";
        case RevisionCategory::Transform: return "Relocate";
        case RevisionCategory::Body:      return "Group";
        case RevisionCategory::Add:       return "Create";
        case RevisionCategory::Edit:      return "Edit";
        case RevisionCategory::Drop:      return "Drop";
    }
    return "Edit";
}

RevisionTone RevisionToneOf(RevisionCategory Category)
{
    switch (Category)
    {
        case RevisionCategory::Feature:   return RevisionTone::Parametric;
        case RevisionCategory::Param:     return RevisionTone::Parametric;
        case RevisionCategory::Drop:      return RevisionTone::Parametric;
        case RevisionCategory::Sketch:    return RevisionTone::Generative;
        case RevisionCategory::Transform: return RevisionTone::Generative;
        case RevisionCategory::Add:       return RevisionTone::Generative;
        case RevisionCategory::Body:      return RevisionTone::Material;
        case RevisionCategory::Edit:      return RevisionTone::Material;
        case RevisionCategory::Start:     return RevisionTone::None;
    }
    return RevisionTone::None;
}

std::uint32_t RevisionHue(RevisionCategory Category)
{
    // REVISION_HUE — reuses the classification palette so the two panes speak one language.
    switch (Category)
    {
        case RevisionCategory::Start:     return HueScene;   // sky
        case RevisionCategory::Feature:   return HueSolid;   // amber
        case RevisionCategory::Param:     return HueCylinder;// green
        case RevisionCategory::Sketch:    return HueSketch;  // cyan
        case RevisionCategory::Transform: return HueLoft;    // blue
        case RevisionCategory::Body:      return HueFolder;  // violet
        case RevisionCategory::Add:       return HueScene;   // sky
        case RevisionCategory::Edit:      return HueRevolve; // earth
        case RevisionCategory::Drop:      return HueCone;    // red
    }
    return HueDim;
}

namespace
{
    Revision MakeRevision(RevisionCategory Category, const char* Title, const char* Subtitle, const char* TimeText)
    {
        Revision R;
        R.Category = Category;
        R.Title    = Title ? Title : "";
        R.Subtitle = Subtitle ? Subtitle : "";
        R.TimeText = TimeText ? TimeText : "";
        return R;
    }

    RevisionBranch NewBranch(RevisionStore& Store, const char* Name)
    {
        RevisionBranch Branch;
        ++Store.BranchSeq;
        Branch.Name   = Name ? Name : ("Branch " + std::to_string(Store.BranchSeq));
        Branch.Cursor = -1;
        return Branch;
    }
}

void SeedRevisions(RevisionStore& Store)
{
    Store.Branches.clear();
    Store.Active    = 0;
    Store.BranchSeq = 0;

    RevisionBranch Trunk = NewBranch(Store, "Trunk");
    Trunk.Revisions.push_back(MakeRevision(RevisionCategory::Start,   "Document opened", "Bracket_Rev4.wsdoc", "09:00"));
    Trunk.Revisions.push_back(MakeRevision(RevisionCategory::Sketch,  "SK_BasePlate",    "8 curves \xC2\xB7 XY plane", "09:02"));
    Trunk.Revisions.push_back(MakeRevision(RevisionCategory::Feature, "Extrude SOL_Plate", "12.5 mm", "09:05"));
    Trunk.Revisions.push_back(MakeRevision(RevisionCategory::Param,   "Radius 6.25 mm",  "SOL_Boss", "09:07"));
    Trunk.Cursor = static_cast<int>(Trunk.Revisions.size()) - 1;
    Store.Branches.push_back(std::move(Trunk));
    Store.Active = 0;
}

void RecordRevision(RevisionStore& Store, RevisionCategory Category, const char* Title, const char* Subtitle, const char* TimeText)
{
    if (Store.Active < 0 || Store.Active >= static_cast<int>(Store.Branches.size())) return;
    RevisionBranch& Branch = Store.Branches[Store.Active];

    if (Branch.Cursor < static_cast<int>(Branch.Revisions.size()) - 1)
    {
        // 📝 Recording behind the tip forks a lateral branch from the retained prefix rather than truncating the redoable tail.
        RevisionBranch Fork = NewBranch(Store, nullptr);   // "Branch N" auto-name
        Fork.Revisions.assign(Branch.Revisions.begin(), Branch.Revisions.begin() + (Branch.Cursor + 1));
        Fork.Revisions.push_back(MakeRevision(Category, Title, Subtitle, TimeText));
        Fork.Cursor = static_cast<int>(Fork.Revisions.size()) - 1;
        Store.Branches.push_back(std::move(Fork));
        Store.Active = static_cast<int>(Store.Branches.size()) - 1;
    }
    else
    {
        Branch.Revisions.push_back(MakeRevision(Category, Title, Subtitle, TimeText));
        Branch.Cursor = static_cast<int>(Branch.Revisions.size()) - 1;
    }
}

namespace
{
    int Clamp(int Value, int Low, int High) { return Value < Low ? Low : (Value > High ? High : Value); }
}

void StepBack(RevisionStore& Store)
{
    if (Store.Active < 0 || Store.Active >= static_cast<int>(Store.Branches.size())) return;
    RevisionBranch& Branch = Store.Branches[Store.Active];
    if (Branch.Cursor >= 0) --Branch.Cursor;
}

void StepForward(RevisionStore& Store)
{
    if (Store.Active < 0 || Store.Active >= static_cast<int>(Store.Branches.size())) return;
    RevisionBranch& Branch = Store.Branches[Store.Active];
    if (Branch.Cursor < static_cast<int>(Branch.Revisions.size()) - 1) ++Branch.Cursor;
}

void JumpToRevision(RevisionStore& Store, int Index)
{
    if (Store.Active < 0 || Store.Active >= static_cast<int>(Store.Branches.size())) return;
    RevisionBranch& Branch = Store.Branches[Store.Active];
    Branch.Cursor = Clamp(Index, -1, static_cast<int>(Branch.Revisions.size()) - 1);
}

void ForkBranch(RevisionStore& Store)
{
    if (Store.Active < 0 || Store.Active >= static_cast<int>(Store.Branches.size())) return;
    RevisionBranch& Branch = Store.Branches[Store.Active];
    RevisionBranch Fork = NewBranch(Store, nullptr);
    Fork.Revisions.assign(Branch.Revisions.begin(), Branch.Revisions.begin() + (Branch.Cursor + 1));
    Fork.Cursor = static_cast<int>(Fork.Revisions.size()) - 1;
    Store.Branches.push_back(std::move(Fork));
    Store.Active = static_cast<int>(Store.Branches.size()) - 1;
}

void DropBranch(RevisionStore& Store, int Index)
{
    if (Store.Branches.size() <= 1) return;   // the × is hidden with one branch
    if (Index < 0 || Index >= static_cast<int>(Store.Branches.size())) return;
    Store.Branches.erase(Store.Branches.begin() + Index);
    Store.Active = Clamp(Store.Active, 0, static_cast<int>(Store.Branches.size()) - 1);
}

}   // namespace SceneDirectoryInspectorValidation
