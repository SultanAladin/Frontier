/*====================================================================================================================================
                                                        WORKSPACEHOST.JS
====================================================================================================================================*/
// 🧩 Editor workspace chrome: docked panel layout, scene directory, inspector binding, transform tools, and viewport transport.

import { ViewportRasterization } from "./ViewportRasterization.js";
import { PrimitiveCategory, PrimitiveTessellation } from "./PolygonSurfaceConstruction.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE RECORDS
//------------------------------------------------------------------------------------------------------------------------

const SceneRecords = [
    {
        Token: 1,
        Name: "Knot study",
        Category: "geometry",
        Primitive: "knot",
        VisibilityEnabled: true,
        Placement: { Position: [ 0.0, 0.55, 0.0 ], Rotation: [ 0.0, 0.0, 0.0 ], Scaling: [ 1.0, 1.0, 1.0 ] },
        Surface: {
            BaseColor: [ 0.07, 0.27, 0.20 ],
            RimColor: [ 0.56, 0.90, 0.68 ],
            RimStrength: 0.85,
            WireStrength: 0.15,
            SweepStrength: 0.80
        }
    },
    {
        Token: 2,
        Name: "Orbit ring",
        Category: "geometry",
        Primitive: "halo",
        VisibilityEnabled: true,
        Placement: { Position: [ 0.0, 0.55, 0.0 ], Rotation: [ 70.0, 0.0, 16.0 ], Scaling: [ 1.0, 1.0, 1.0 ] },
        Surface: {
            BaseColor: [ 0.05, 0.17, 0.13 ],
            RimColor: [ 0.55, 0.86, 0.72 ],
            RimStrength: 0.55,
            WireStrength: 0.0,
            SweepStrength: 0.30
        }
    },
    {
        Token: 3,
        Name: "Ground grid",
        Category: "grid",
        Primitive: null,
        VisibilityEnabled: true,
        Placement: { Position: [ 0.0, -1.62, 0.0 ], Rotation: [ 0.0, 0.0, 0.0 ], Scaling: [ 1.0, 1.0, 1.0 ] },
        Surface: null
    },
    {
        Token: 4,
        Name: "Key light",
        Category: "light",
        Primitive: null,
        VisibilityEnabled: true,
        Placement: { Position: [ 3.2, 4.6, 2.8 ], Rotation: [ -38.0, 42.0, 0.0 ], Scaling: [ 1.0, 1.0, 1.0 ] },
        Surface: null
    },
    {
        Token: 5,
        Name: "Main camera",
        Category: "camera",
        Primitive: null,
        VisibilityEnabled: true,
        Placement: { Position: [ 0.0, 0.35, 7.1 ], Rotation: [ 0.0, 0.0, 0.0 ], Scaling: [ 1.0, 1.0, 1.0 ] },
        Surface: null
    }
];

const CategoryLabels = {
    geometry: "Geometry record",
    grid: "World grid",
    light: "Luminous record",
    camera: "View record"
};

const PrimitiveSymbols = {
    knot: "IconRing",
    halo: "IconRing",
    sphere: "IconSphere",
    torus: "IconRing",
    cube: "IconCube",
    cylinder: "IconCylinder"
};

const CategorySymbols = {
    grid: "IconGrid",
    light: "IconLight",
    camera: "IconCamera"
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOM RETRIEVAL
//------------------------------------------------------------------------------------------------------------------------

function Query(Identity)
{
    const Node = document.getElementById(Identity);
    if (!Node)
    {
        throw new Error(`missing workspace node: ${Identity}`);
    }
    return Node;
}

function CreateIcon(SymbolIdentity, Size = 14)
{
    const Namespace = "http://www.w3.org/2000/svg";
    const Icon = document.createElementNS(Namespace, "svg");
    Icon.setAttribute("width", String(Size));
    Icon.setAttribute("height", String(Size));
    Icon.setAttribute("aria-hidden", "true");
    const Reference = document.createElementNS(Namespace, "use");
    Reference.setAttribute("href", `#${SymbolIdentity}`);
    Icon.appendChild(Reference);
    return Icon;
}

function ClampOperand(Operand, Minimum, Maximum)
{
    return Math.min(Maximum, Math.max(Minimum, Operand));
}

function FormatCount(Value)
{
    return Math.round(Value).toLocaleString("en-US").replace(/,/g, " ");
}

function FormatDecimal(Value, Digits)
{
    return Value.toFixed(Digits);
}

function HexFromColor(Color)
{
    const Channel = (Component) => Math.round(ClampOperand(Component, 0.0, 1.0) * 255).toString(16).padStart(2, "0");
    return `#${Channel(Color[0])}${Channel(Color[1])}${Channel(Color[2])}`;
}

function ColorFromHex(Hex)
{
    const Parsed = parseInt(Hex.replace("#", ""), 16);
    return [ ((Parsed >> 16) & 255) / 255.0, ((Parsed >> 8) & 255) / 255.0, (Parsed & 255) / 255.0 ];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    WORKSPACE STATE
//------------------------------------------------------------------------------------------------------------------------

const WorkspaceState = {
    SelectedToken: 1,
    FilterCategory: "all",
    SearchQuery: "",
    TransformMode: "select",
    UniformScale: true,
    GridEnabled: true,
    AutoOrbit: false,
    SelectionAxes: true,
    InspectorTab: "object",
    OutlinerTab: "scene",
    TokenSequence: SceneRecords.length
};

const SurfacePresets = [
    { Identity: "holographic", Label: "Holographic", RimStrength: 0.85, WireStrength: 0.15, SweepStrength: 0.80 },
    { Identity: "wireframe",   Label: "Wireframe",   RimStrength: 0.45, WireStrength: 0.92, SweepStrength: 0.55 },
    { Identity: "solid",       Label: "Solid shell", RimStrength: 0.20, WireStrength: 0.00, SweepStrength: 0.30 }
];

let SurfacePresetIndex = 0;

const HostNodes = {
    DockRow: Query("DockRow"),
    OutlinerPanel: Query("OutlinerPanel"),
    InspectorPanel: Query("InspectorPanel"),
    TreeRows: Query("TreeRows"),
    TreeGroup: Query("TreeGroupWorld"),
    TreeGroupCount: Query("TreeGroupCount"),
    HeroRecordCount: Query("HeroRecordCount"),
    OutlinerFootCount: Query("OutlinerFootCount"),
    OutlinerSelectionMetric: Query("OutlinerSelectionMetric"),
    OutlinerSceneMetric: Query("OutlinerSceneMetric"),
    SearchField: Query("OutlinerSearch"),
    FilterRow: Query("FilterRow"),
    SceneTabContent: Query("SceneTabContent"),
    LibraryTabContent: Query("LibraryTabContent"),
    PrimitiveMenu: Query("PrimitiveMenu"),
    AddRecordButton: Query("AddRecordButton"),
    InspectorName: Query("InspectorName"),
    InspectorType: Query("InspectorType"),
    InspectorGlyph: Query("InspectorGlyph"),
    PlacementSection: Query("PlacementSection"),
    SurfaceSection: Query("SurfaceSection"),
    PlacementVertexCount: Query("PlacementVertexCount"),
    PlacementTriangleCount: Query("PlacementTriangleCount"),
    PlacementToken: Query("PlacementToken"),
    BoundsExtentX: Query("BoundsExtentX"),
    BoundsExtentY: Query("BoundsExtentY"),
    BoundsExtentZ: Query("BoundsExtentZ"),
    BoundsClassification: Query("BoundsClassification"),
    SurfaceIdentityLabel: Query("SurfaceIdentityLabel"),
    SurfacePresetRow: Query("SurfacePresetRow"),
    TessellationLabel: Query("TessellationLabel"),
    TessellationSegments: Query("TessellationSegments"),
    TessellationRings: Query("TessellationRings"),
    TessellationTriangles: Query("TessellationTriangles"),
    BaseColorInput: Query("BaseColorInput"),
    BaseColorCode: Query("BaseColorCode"),
    RimColorInput: Query("RimColorInput"),
    RimColorCode: Query("RimColorCode"),
    SelectionFrameBrackets: Query("SelectionFrameBrackets"),
    SelectionLeader: Query("SelectionLeader"),
    SelectionAnnotation: Query("SelectionAnnotation"),
    AnnotationName: Query("AnnotationName"),
    AnnotationMeta: Query("AnnotationMeta"),
    Stage: Query("ViewportStage"),
    TimelineProgress: Query("TimelineProgress"),
    TimelinePlayhead: Query("TimelinePlayhead"),
    TimelineTime: Query("TimelineTime"),
    TimelineRuler: Query("TimelineRuler"),
    TimelinePlay: Query("TimelinePlay"),
    TelemetryVertices: Query("TelemetryVertices"),
    TelemetryRecords: Query("TelemetryRecords"),
    TelemetrySpark: Query("TelemetrySpark"),
    TelemetrySparkRecords: Query("TelemetrySparkRecords"),
    MetricVertices: Query("MetricVertices"),
    MetricRecords: Query("MetricRecords"),
    MetricFrameRate: Query("MetricFrameRate"),
    SurfaceFrameRate: Query("SurfaceFrameRate"),
    RenderSummary: Query("RenderSummary"),
    CameraSummary: Query("CameraSummary"),
    SelectionSummary: Query("SelectionSummary"),
    BreadcrumbRecord: Query("BreadcrumbRecord"),
    RasterTag: Query("RasterTag"),
    RasterizationTag: Query("RasterizationTag"),
    DiagnosticToast: Query("DiagnosticToast"),
    SweepSwitch: Query("SweepSwitch"),
    GridToggle: Query("GridToggle"),
    AutoOrbitButton: Query("AutoOrbitButton"),
    ResetViewButton: Query("ResetViewButton"),
    FrameButton: Query("FrameButton"),
    DuplicateButton: Query("DuplicateButton"),
    InspectorFocus: Query("InspectorFocus"),
    ResetSelectionButton: Query("ResetSelectionButton"),
    DuplicateRecordButton: Query("DuplicateRecordButton"),
    TimelineRestart: Query("TimelineRestart"),
    TimelineAdvance: Query("TimelineAdvance")
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

const Renderer = new ViewportRasterization(Query("ViewportCanvas"), SceneRecords, {
    OnDiagnostics: ReflectDiagnostics,
    OnFrameRate: ReflectFrameRate,
    OnSelectionRect: ReflectSelectionRect
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    DIAGNOSTIC SURFACING
//------------------------------------------------------------------------------------------------------------------------

function ReflectDiagnostics(Diagnostics)
{
    if (!Diagnostics || Diagnostics.length === 0)
    {
        return;
    }
    HostNodes.DiagnosticToast.hidden = false;
    HostNodes.DiagnosticToast.textContent = `Rasterization diagnostics: ${Diagnostics[0]}`;
    HostNodes.RasterTag.textContent = "fallback";
    HostNodes.RasterizationTag.textContent = "unavailable";

    const FallbackNotice = document.createElement("div");
    FallbackNotice.style.cssText = "position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);max-width:320px;padding:18px 20px;border-radius:16px;border:1px solid rgba(255,255,255,0.12);background:rgba(14,18,15,0.85);color:#c9cdc2;font-size:12px;line-height:1.55;text-align:center;z-index:4";
    FallbackNotice.textContent = "WebGL2 is unavailable in this browser. Panels, inspector, and directory remain interactive; the holographic viewport cannot rasterize.";
    HostNodes.Stage.appendChild(FallbackNotice);
}

function ReflectFrameRate(FrameRate)
{
    const Rounded = Math.round(FrameRate);
    HostNodes.MetricFrameRate.textContent = String(Rounded);
    HostNodes.SurfaceFrameRate.textContent = `${Rounded} fps`;
    HostNodes.RenderSummary.textContent = `WebGL2 · ${Rounded} fps`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RECORD RETRIEVAL
//------------------------------------------------------------------------------------------------------------------------

function RetrieveSelectedRecord()
{
    return SceneRecords.find((RecordEntry) => RecordEntry.Token === WorkspaceState.SelectedToken) || null;
}

function RetrieveRecordSymbol(RecordEntry)
{
    if (RecordEntry.Category === "geometry")
    {
        return PrimitiveSymbols[RecordEntry.Primitive] || "IconCube";
    }
    return CategorySymbols[RecordEntry.Category] || "IconCube";
}

function DescribeRecord(RecordEntry)
{
    if (RecordEntry.Category === "geometry")
    {
        return PrimitiveCategory[RecordEntry.Primitive] || "Surface record";
    }
    return CategoryLabels[RecordEntry.Category] || "Record";
}

function RetrieveFilteredRecords()
{
    const NormalizedQuery = WorkspaceState.SearchQuery.trim().toLowerCase();

    return SceneRecords.filter((RecordEntry) =>
    {
        if (WorkspaceState.FilterCategory === "geometry" && RecordEntry.Category !== "geometry")
        {
            return false;
        }
        if (WorkspaceState.FilterCategory === "light" && RecordEntry.Category !== "light")
        {
            return false;
        }
        if (!NormalizedQuery)
        {
            return true;
        }
        return RecordEntry.Name.toLowerCase().includes(NormalizedQuery)
            || DescribeRecord(RecordEntry).toLowerCase().includes(NormalizedQuery);
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    OUTLINER RENDERING
//------------------------------------------------------------------------------------------------------------------------

function RenderRecordTree()
{
    HostNodes.TreeRows.replaceChildren();
    const VisibleRecords = RetrieveFilteredRecords();

    if (VisibleRecords.length === 0)
    {
        const EmptyNote = document.createElement("p");
        EmptyNote.className = "tree-empty";
        EmptyNote.textContent = "No records match this filter.";
        HostNodes.TreeRows.appendChild(EmptyNote);
    }

    for (const RecordEntry of VisibleRecords)
    {
        const Row = document.createElement("button");
        Row.type = "button";
        Row.className = "tree-row";
        Row.dataset.token = String(RecordEntry.Token);
        Row.setAttribute("role", "treeitem");
        Row.setAttribute("aria-selected", RecordEntry.Token === WorkspaceState.SelectedToken ? "true" : "false");

        if (RecordEntry.Token === WorkspaceState.SelectedToken)
        {
            Row.classList.add("is-selected");
        }
        if (!RecordEntry.VisibilityEnabled)
        {
            Row.classList.add("is-hidden", "is-dimmed");
        }

        const Glyph = document.createElement("span");
        Glyph.className = "tree-glyph";
        Glyph.appendChild(CreateIcon(RetrieveRecordSymbol(RecordEntry), 13));

        const Labels = document.createElement("span");
        Labels.className = "tree-labels";
        const NameNode = document.createElement("span");
        NameNode.className = "tree-name";
        NameNode.textContent = RecordEntry.Name;
        const CaptionNode = document.createElement("span");
        CaptionNode.className = "tree-caption";
        CaptionNode.textContent = DescribeRecord(RecordEntry);
        Labels.append(NameNode, CaptionNode);

        const Badge = document.createElement("span");
        Badge.className = "tree-badge";
        if (RecordEntry.Category === "geometry" && Renderer.Context)
        {
            const Statistics = Renderer.RetrieveRecordStatistics(RecordEntry.Primitive);
            Badge.textContent = `${(Statistics.VertexTotal / 1000).toFixed(1)}k`;
        }
        else
        {
            Badge.textContent = RecordEntry.Category === "camera" ? "view" : RecordEntry.Category;
        }

        const Visibility = document.createElement("span");
        Visibility.className = "tree-visibility";
        Visibility.setAttribute("role", "button");
        Visibility.setAttribute("tabindex", "0");
        Visibility.setAttribute("title", RecordEntry.VisibilityEnabled ? "Hide record" : "Show record");
        Visibility.appendChild(CreateIcon(RecordEntry.VisibilityEnabled ? "IconEye" : "IconEyeOff", 12));

        const HandleVisibility = (Event) =>
        {
            Event.preventDefault();
            Event.stopPropagation();
            if (RecordEntry.Category === "grid")
            {
                WorkspaceState.GridEnabled = !WorkspaceState.GridEnabled;
                Renderer.ConfigureGridVisibility(WorkspaceState.GridEnabled);
                HostNodes.GridToggle.classList.toggle("is-active", WorkspaceState.GridEnabled);
                SynchronizeViewControls();
                return;
            }
            RecordEntry.VisibilityEnabled = !RecordEntry.VisibilityEnabled;
            RenderRecordTree();
        };

        Visibility.addEventListener("click", HandleVisibility);
        Visibility.addEventListener("keydown", (Event) =>
        {
            if (Event.key === "Enter" || Event.key === " ")
            {
                HandleVisibility(Event);
            }
        });

        Row.addEventListener("click", () => SelectRecord(RecordEntry.Token, false));
        Row.addEventListener("dblclick", () =>
        {
            SelectRecord(RecordEntry.Token, true);
        });

        Row.append(Glyph, Labels, Badge, Visibility);
        HostNodes.TreeRows.appendChild(Row);
    }

    HostNodes.TreeGroupCount.textContent = String(VisibleRecords.length);
    SynchronizeCounts();
}

function SynchronizeCounts()
{
    const Total = SceneRecords.length;
    const GeometryTotal = SceneRecords.filter((RecordEntry) => RecordEntry.Category === "geometry").length;
    const LightTotal = SceneRecords.filter((RecordEntry) => RecordEntry.Category === "light").length;

    HostNodes.HeroRecordCount.textContent = `${Total} records`;
    HostNodes.OutlinerFootCount.textContent = `${Total} records · ${RetrieveSelectedRecord() ? 1 : 0} selected`;
    Query("FilterCountAll").textContent = String(Total);
    Query("FilterCountGeometry").textContent = String(GeometryTotal);
    Query("FilterCountLight").textContent = String(LightTotal);
    HostNodes.MetricRecords.textContent = String(Total);
    HostNodes.TelemetryRecords.textContent = String(SceneRecords.filter((RecordEntry) => RecordEntry.Category === "geometry" && RecordEntry.VisibilityEnabled).length);
    HostNodes.SelectionSummary.textContent = RetrieveSelectedRecord() ? "1 record" : "none";

    if (RetrieveSelectedRecord())
    {
        HostNodes.BreadcrumbRecord.textContent = RetrieveSelectedRecord().Name;
    }
    else
    {
        HostNodes.BreadcrumbRecord.textContent = "no selection";
    }

    SynchronizeTelemetry();
}

function SynchronizeTelemetry()
{
    let VertexTotal = 0;
    if (Renderer.Context)
    {
        const Statistics = Renderer.RetrieveSceneStatistics();
        VertexTotal = Statistics.VertexTotal;
    }

    HostNodes.MetricVertices.textContent = FormatCount(VertexTotal);
    HostNodes.OutlinerSceneMetric.textContent = VertexTotal >= 1000 ? `${(VertexTotal / 1000).toFixed(1)}k` : String(VertexTotal);
    HostNodes.TelemetryVertices.textContent = VertexTotal > 9999
        ? `${(VertexTotal / 1000).toFixed(1)}k`
        : FormatCount(VertexTotal);

    const SparkBar = (Container, Count) =>
    {
        Container.replaceChildren();
        for (let Index = 0; Index < 7; Index += 1)
        {
            const Bar = document.createElement("span");
            const Height = 22 + ((Index * 37 + Count * 13) % 66);
            Bar.style.height = `${Height}%`;
            Container.appendChild(Bar);
        }
    };
    SparkBar(HostNodes.TelemetrySpark, VertexTotal);
    SparkBar(HostNodes.TelemetrySparkRecords, SceneRecords.length);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SELECTION
//------------------------------------------------------------------------------------------------------------------------

function SelectRecord(Token, FrameSelection)
{
    WorkspaceState.SelectedToken = Token;
    Renderer.ConfigureSelection(Token);

    if (FrameSelection)
    {
        Renderer.FrameSelection();
    }

    for (const Row of HostNodes.TreeRows.querySelectorAll(".tree-row"))
    {
        const RowSelected = Number(Row.dataset.token) === Token;
        Row.classList.toggle("is-selected", RowSelected);
        Row.setAttribute("aria-selected", RowSelected ? "true" : "false");
    }

    RefreshInspector(true);
    SynchronizeCounts();

    if (WorkspaceState.InspectorTab === "surface" && (!RetrieveSelectedRecord() || RetrieveSelectedRecord().Category !== "geometry"))
    {
        ConfigureInspectorTab("object");
    }
}

function DuplicateSelectedRecord()
{
    const SourceRecord = RetrieveSelectedRecord();
    if (!SourceRecord || SourceRecord.Category === "grid" || SourceRecord.Category === "camera")
    {
        return;
    }

    WorkspaceState.TokenSequence += 1;
    const Clone = {
        Token: WorkspaceState.TokenSequence,
        Name: `${SourceRecord.Name} copy`,
        Category: SourceRecord.Category,
        Primitive: SourceRecord.Primitive,
        VisibilityEnabled: true,
        Placement: {
            Position: [ SourceRecord.Placement.Position[0] + 0.55, SourceRecord.Placement.Position[1], SourceRecord.Placement.Position[2] + 0.35 ],
            Rotation: [ ...SourceRecord.Placement.Rotation ],
            Scaling: [ ...SourceRecord.Placement.Scaling ]
        },
        Surface: SourceRecord.Surface
            ? {
                BaseColor: [ ...SourceRecord.Surface.BaseColor ],
                RimColor: [ ...SourceRecord.Surface.RimColor ],
                RimStrength: SourceRecord.Surface.RimStrength,
                WireStrength: SourceRecord.Surface.WireStrength,
                SweepStrength: SourceRecord.Surface.SweepStrength
            }
            : null
    };

    SceneRecords.push(Clone);
    RenderRecordTree();
    SelectRecord(Clone.Token, true);
}

function RemoveSelectedRecord()
{
    const SelectedRecord = RetrieveSelectedRecord();
    if (!SelectedRecord || SelectedRecord.Category === "grid" || SelectedRecord.Category === "camera")
    {
        return;
    }

    const RemovalIndex = SceneRecords.indexOf(SelectedRecord);
    SceneRecords.splice(RemovalIndex, 1);

    const NextRecord = SceneRecords[Math.min(RemovalIndex, SceneRecords.length - 1)] || null;
    WorkspaceState.SelectedToken = NextRecord ? NextRecord.Token : null;
    Renderer.ConfigureSelection(WorkspaceState.SelectedToken);
    RenderRecordTree();
    RefreshInspector(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RECORD CREATION
//------------------------------------------------------------------------------------------------------------------------

function AddPrimitiveRecord(PrimitiveIdentity)
{
    WorkspaceState.TokenSequence += 1;

    const OffsetPattern = [ -1.85, 1.85, 0.0, -2.6, 2.6 ];
    const OffsetIndex = (WorkspaceState.TokenSequence - 1) % OffsetPattern.length;
    const BasePalette = {
        knot:     { BaseColor: [ 0.07, 0.27, 0.20 ], RimColor: [ 0.56, 0.90, 0.68 ], RimStrength: 0.85, WireStrength: 0.15 },
        halo:     { BaseColor: [ 0.05, 0.17, 0.13 ], RimColor: [ 0.55, 0.86, 0.72 ], RimStrength: 0.72, WireStrength: 0.0 },
        sphere:   { BaseColor: [ 0.10, 0.28, 0.26 ], RimColor: [ 0.50, 0.94, 0.82 ], RimStrength: 1.05, WireStrength: 0.28 },
        torus:    { BaseColor: [ 0.14, 0.26, 0.17 ], RimColor: [ 0.68, 0.96, 0.60 ], RimStrength: 0.95, WireStrength: 0.22 },
        cube:     { BaseColor: [ 0.12, 0.22, 0.20 ], RimColor: [ 0.56, 0.92, 0.74 ], RimStrength: 0.90, WireStrength: 0.40 },
        cylinder: { BaseColor: [ 0.12, 0.27, 0.21 ], RimColor: [ 0.60, 0.92, 0.66 ], RimStrength: 1.00, WireStrength: 0.30 }
    };
    const Palette = BasePalette[PrimitiveIdentity] || BasePalette.sphere;

    const CreatedRecord = {
        Token: WorkspaceState.TokenSequence,
        Name: `${PrimitiveCategory[PrimitiveIdentity] || "Surface"} ${String(WorkspaceState.TokenSequence).padStart(2, "0")}`,
        Category: "geometry",
        Primitive: PrimitiveIdentity,
        VisibilityEnabled: true,
        Placement: {
            Position: [ OffsetPattern[OffsetIndex], 0.55, PrimitiveIdentity === "halo" ? -0.9 : 0.0 ],
            Rotation: [ 0.0, PrimitiveIdentity === "halo" ? 64.0 : 0.0, PrimitiveIdentity === "halo" ? 12.0 : 0.0 ],
            Scaling: [ 0.72, 0.72, 0.72 ]
        },
        Surface: {
            BaseColor: [ ...Palette.BaseColor ],
            RimColor: [ ...Palette.RimColor ],
            RimStrength: Palette.RimStrength,
            WireStrength: Palette.WireStrength,
            SweepStrength: 0.85
        }
    };

    SceneRecords.push(CreatedRecord);
    RenderRecordTree();
    SelectRecord(CreatedRecord.Token, false);
    RefreshInspector(true);
    ClosePrimitiveMenu();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR BINDING
//------------------------------------------------------------------------------------------------------------------------

function RefreshInspector(RefreshedSelection)
{
    const SelectedRecord = RetrieveSelectedRecord();

    if (!SelectedRecord)
    {
        HostNodes.InspectorName.textContent = "No record";
        HostNodes.InspectorType.textContent = "Nothing selected";
        HostNodes.PlacementSection.classList.add("is-disabled");
        HostNodes.SurfaceSection.classList.add("is-disabled");
        return;
    }

    if (RefreshedSelection)
    {
        HostNodes.InspectorName.textContent = SelectedRecord.Name;
        HostNodes.InspectorType.textContent = DescribeRecord(SelectedRecord);
        HostNodes.InspectorGlyph.replaceChildren(CreateIcon(RetrieveRecordSymbol(SelectedRecord), 18));
        HostNodes.PlacementToken.textContent = `#${String(SelectedRecord.Token).padStart(2, "0")}`;

        const GeometryAvailable = SelectedRecord.Category === "geometry";
        HostNodes.PlacementSection.classList.toggle("is-disabled", false);
        HostNodes.SurfaceSection.classList.toggle("is-disabled", !GeometryAvailable);

        HostNodes.BoundsClassification.textContent = SelectedRecord.Category === "geometry"
            ? "Extruded surface"
            : CategoryLabels[SelectedRecord.Category] || "Record";

        if (Renderer.Context && GeometryAvailable)
        {
            const Statistics = Renderer.RetrieveRecordStatistics(SelectedRecord.Primitive);
            HostNodes.PlacementVertexCount.textContent = FormatCount(Statistics.VertexTotal);
            HostNodes.PlacementTriangleCount.textContent = FormatCount(Statistics.TriangleTotal);

            const Tessellation = PrimitiveTessellation[SelectedRecord.Primitive] || { Segments: null, Rings: null };
            HostNodes.TessellationSegments.textContent = Tessellation.Segments === null ? "—" : FormatCount(Tessellation.Segments);
            HostNodes.TessellationRings.textContent = Tessellation.Rings === null ? "—" : FormatCount(Tessellation.Rings);
            HostNodes.TessellationTriangles.textContent = FormatCount(Statistics.TriangleTotal);
            HostNodes.TessellationLabel.textContent = SelectedRecord.Category === "geometry"
                ? (PrimitiveCategory[SelectedRecord.Primitive] || "surface")
                : "record";

            const Bounds = Renderer.RetrieveWorldBounds(SelectedRecord);
            HostNodes.BoundsExtentX.textContent = FormatDecimal(Bounds.MaximumBoundary[0] - Bounds.MinimumBoundary[0], 2);
            HostNodes.BoundsExtentY.textContent = FormatDecimal(Bounds.MaximumBoundary[1] - Bounds.MinimumBoundary[1], 2);
            HostNodes.BoundsExtentZ.textContent = FormatDecimal(Bounds.MaximumBoundary[2] - Bounds.MinimumBoundary[2], 2);
        }
        else
        {
            HostNodes.PlacementVertexCount.textContent = "—";
            HostNodes.PlacementTriangleCount.textContent = "—";
            HostNodes.BoundsExtentX.textContent = "—";
            HostNodes.BoundsExtentY.textContent = "—";
            HostNodes.BoundsExtentZ.textContent = "—";
        }
    }

    for (const Field of document.querySelectorAll(".numeric-input"))
    {
        const PlacementKey = Field.dataset.placement;
        const AxisIndex = Number(Field.dataset.axis);
        const DigitCount = PlacementKey === "Rotation" ? 1 : 2;
        const Coordinate = SelectedRecord.Placement[PlacementKey][AxisIndex];
        if (document.activeElement !== Field)
        {
            Field.value = FormatDecimal(Coordinate, DigitCount);
        }
    }

    if (SelectedRecord.Surface)
    {
        HostNodes.BaseColorInput.value = HexFromColor(SelectedRecord.Surface.BaseColor);
        HostNodes.BaseColorCode.textContent = HexFromColor(SelectedRecord.Surface.BaseColor).toUpperCase();
        HostNodes.RimColorInput.value = HexFromColor(SelectedRecord.Surface.RimColor);
        HostNodes.RimColorCode.textContent = HexFromColor(SelectedRecord.Surface.RimColor).toUpperCase();

        for (const Slider of document.querySelectorAll("[data-surface-property]"))
        {
            SynchronizeSliderPosition(Slider, SelectedRecord.Surface[Slider.dataset.surfaceProperty], 2);
        }
    }

    if (Renderer.Context && SelectedRecord.Category === "geometry")
    {
        const SelectionStatistics = Renderer.RetrieveRecordStatistics(SelectedRecord.Primitive);
        HostNodes.OutlinerSelectionMetric.textContent = SelectionStatistics.VertexTotal >= 1000
            ? `${(SelectionStatistics.VertexTotal / 1000).toFixed(1)}k`
            : String(SelectionStatistics.VertexTotal);
    }
    else
    {
        HostNodes.OutlinerSelectionMetric.textContent = "—";
    }

    SynchronizeCameraFields();
    SynchronizeViewControls();
}

function SynchronizeSliderPosition(Row, Value, Digits)
{
    const Minimum = Number(Row.dataset.min);
    const Maximum = Number(Row.dataset.max);
    const Ratio = ClampOperand((Value - Minimum) / (Maximum - Minimum), 0.0, 1.0);
    const Track = Row.querySelector(".slider-track");
    const Fill = Row.querySelector(".slider-fill");
    const Thumb = Row.querySelector(".slider-thumb");
    const ValueLabel = Row.querySelector(".slider-value");
    const Suffix = Row.dataset.suffix || "";

    Fill.style.width = `${Ratio * 100}%`;
    Thumb.style.left = `${Ratio * 100}%`;
    const DisplayValue = Row.dataset.cameraProperty === "FieldOfView" ? String(Math.round(Value)) : FormatDecimal(Value, Digits);
    ValueLabel.textContent = `${DisplayValue}${Suffix}`;
    Track.dataset.ratio = String(Ratio);
}

function ApplySliderValue(Row, Ratio)
{
    const Minimum = Number(Row.dataset.min);
    const Maximum = Number(Row.dataset.max);
    const Value = Minimum + (Maximum - Minimum) * ClampOperand(Ratio, 0.0, 1.0);

    if (Row.dataset.surfaceProperty)
    {
        const SelectedRecord = RetrieveSelectedRecord();
        if (!SelectedRecord || !SelectedRecord.Surface)
        {
            return;
        }
        SelectedRecord.Surface[Row.dataset.surfaceProperty] = Value;
        SynchronizeSliderPosition(Row, Value, 2);
        return;
    }

    if (Row.dataset.cameraProperty === "FieldOfView")
    {
        Renderer.Camera.FieldOfView = Value;
    }
    else if (Row.dataset.cameraProperty === "Distance")
    {
        Renderer.Camera.Distance = Value;
    }
    SynchronizeSliderPosition(Row, Value, Row.dataset.cameraProperty === "Distance" ? 1 : 2);
    SynchronizeCameraFields();
}

function SynchronizeCameraFields()
{
    HostNodes.CameraSummary.textContent = `yaw ${FormatDecimal(Renderer.Camera.Yaw, 2)} · pitch ${FormatDecimal(Renderer.Camera.Pitch, 2)}`;

    for (const Row of document.querySelectorAll("[data-camera-property]"))
    {
        const Value = Renderer.Camera[Row.dataset.cameraProperty];
        const Digits = Row.dataset.cameraProperty === "Distance" ? 1 : 2;
        SynchronizeSliderPosition(Row, Value, Digits);
    }
}

function SynchronizeViewControls()
{
    HostNodes.GridToggle.classList.toggle("is-active", WorkspaceState.GridEnabled);
    HostNodes.AutoOrbitButton.classList.toggle("is-active", WorkspaceState.AutoOrbit);

    for (const Switch of document.querySelectorAll("[data-view-toggle]"))
    {
        const ToggleIdentity = Switch.dataset.viewToggle;
        let ToggleValue = false;
        if (ToggleIdentity === "GridEnabled")
        {
            ToggleValue = WorkspaceState.GridEnabled;
        }
        else if (ToggleIdentity === "AutoOrbit")
        {
            ToggleValue = WorkspaceState.AutoOrbit;
        }
        else if (ToggleIdentity === "SelectionAxes")
        {
            ToggleValue = WorkspaceState.SelectionAxes;
        }
        Switch.classList.toggle("is-on", ToggleValue);
        Switch.setAttribute("aria-pressed", ToggleValue ? "true" : "false");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NUMERIC FIELD INTERACTION
//------------------------------------------------------------------------------------------------------------------------

function CommitNumericField(Field)
{
    const SelectedRecord = RetrieveSelectedRecord();
    if (!SelectedRecord)
    {
        return;
    }

    const PlacementKey = Field.dataset.placement;
    const AxisIndex = Number(Field.dataset.axis);
    const ParsedValue = Number.parseFloat(Field.value);
    if (!Number.isFinite(ParsedValue))
    {
        RefreshInspector(false);
        return;
    }

    if (PlacementKey === "Scaling")
    {
        const GuardedValue = Math.max(0.05, ParsedValue);
        if (WorkspaceState.UniformScale)
        {
            SelectedRecord.Placement.Scaling = [ GuardedValue, GuardedValue, GuardedValue ];
        }
        else
        {
            SelectedRecord.Placement.Scaling[AxisIndex] = GuardedValue;
        }
    }
    else
    {
        SelectedRecord.Placement[PlacementKey][AxisIndex] = ParsedValue;
    }

    RefreshInspector(false);
}

function AttachNumericField(Field)
{
    const Scratch = { Active: false, StartX: 0, StartValue: 0, Scrub: false };

    Field.addEventListener("focus", () => Field.select());

    Field.addEventListener("change", () => CommitNumericField(Field));

    Field.addEventListener("keydown", (Event) =>
    {
        if (Event.key === "Enter")
        {
            CommitNumericField(Field);
            Field.blur();
        }
        if (Event.key === "Escape")
        {
            RefreshInspector(false);
            Field.blur();
        }
    });

    Field.addEventListener("pointerdown", (Event) =>
    {
        if (document.activeElement === Field)
        {
            return;
        }
        Scratch.Active = true;
        Scratch.Scrub = false;
        Scratch.StartX = Event.clientX;
        Scratch.StartValue = Number.parseFloat(Field.value) || 0.0;
        Field.setPointerCapture(Event.pointerId);
    });

    Field.addEventListener("pointermove", (Event) =>
    {
        if (!Scratch.Active)
        {
            return;
        }
        const Delta = Event.clientX - Scratch.StartX;
        if (Math.abs(Delta) < 4 && !Scratch.Scrub)
        {
            return;
        }
        Scratch.Scrub = true;
        const Step = Field.dataset.placement === "Rotation" ? 0.35 : 0.012;
        const Digits = Field.dataset.placement === "Rotation" ? 1 : 2;
        Field.value = FormatDecimal(Scratch.StartValue + Delta * Step, Digits);
        CommitNumericField(Field);
    });

    const ConcludeScrub = () =>
    {
        if (!Scratch.Active)
        {
            return;
        }
        Scratch.Active = false;
        if (Scratch.Scrub)
        {
            Field.blur();
        }
    };

    Field.addEventListener("pointerup", ConcludeScrub);
    Field.addEventListener("pointercancel", ConcludeScrub);

    Field.parentElement.addEventListener("wheel", (Event) =>
    {
        Event.preventDefault();
        const Increment = Field.dataset.placement === "Rotation" ? 1.0 : 0.05;
        const CurrentValue = Number.parseFloat(Field.value) || 0.0;
        Field.value = String(CurrentValue + (Event.deltaY > 0 ? -Increment : Increment));
        CommitNumericField(Field);
    }, { passive: false });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SLIDER INTERACTION
//------------------------------------------------------------------------------------------------------------------------

function AttachSlider(Row)
{
    const Track = Row.querySelector(".slider-track");
    const ApplyFromPointer = (Event) =>
    {
        const Bounds = Track.getBoundingClientRect();
        const Ratio = (Event.clientX - Bounds.left) / Math.max(1.0, Bounds.width);
        ApplySliderValue(Row, Ratio);
    };

    Row.addEventListener("pointerdown", (Event) =>
    {
        Event.preventDefault();
        Row.setPointerCapture(Event.pointerId);
        Row.dataset.dragging = "true";
        ApplyFromPointer(Event);
    });

    Row.addEventListener("pointermove", (Event) =>
    {
        if (Row.dataset.dragging === "true")
        {
            ApplyFromPointer(Event);
        }
    });

    const ConcludeDrag = () =>
    {
        Row.dataset.dragging = "false";
    };
    Row.addEventListener("pointerup", ConcludeDrag);
    Row.addEventListener("pointercancel", ConcludeDrag);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TAB ROUTING
//------------------------------------------------------------------------------------------------------------------------

function ConfigureInspectorTab(TabIdentity)
{
    WorkspaceState.InspectorTab = TabIdentity;

    for (const Tab of document.querySelectorAll("[data-inspector-tab]"))
    {
        Tab.classList.toggle("is-active", Tab.dataset.inspectorTab === TabIdentity);
    }

    for (const Section of document.querySelectorAll("[data-inspector-section]"))
    {
        Section.hidden = Section.dataset.inspectorSection !== TabIdentity;
    }

    if (TabIdentity === "view")
    {
        SynchronizeCameraFields();
    }
    if (TabIdentity === "surface" || TabIdentity === "object")
    {
        RefreshInspector(true);
    }
}

function ConfigureOutlinerTab(TabIdentity)
{
    WorkspaceState.OutlinerTab = TabIdentity;

    for (const Tab of document.querySelectorAll("[data-outliner-tab]"))
    {
        Tab.classList.toggle("is-active", Tab.dataset.outlinerTab === TabIdentity);
    }

    HostNodes.SceneTabContent.hidden = TabIdentity !== "scene";
    HostNodes.LibraryTabContent.hidden = TabIdentity !== "library";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIMITIVE MENU
//------------------------------------------------------------------------------------------------------------------------

function OpenPrimitiveMenu()
{
    HostNodes.PrimitiveMenu.hidden = false;
}

function ClosePrimitiveMenu()
{
    HostNodes.PrimitiveMenu.hidden = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFACE PRESETS
//------------------------------------------------------------------------------------------------------------------------

function ApplySurfacePreset(PresetIndex)
{
    const Preset = SurfacePresets[PresetIndex];
    for (const RecordEntry of SceneRecords)
    {
        if (RecordEntry.Category !== "geometry" || !RecordEntry.Surface)
        {
            continue;
        }
        RecordEntry.Surface.RimStrength = Preset.RimStrength;
        RecordEntry.Surface.WireStrength = Preset.WireStrength;
        RecordEntry.Surface.SweepStrength = Preset.SweepStrength;
    }
    Query("SurfaceModeChip").firstChild.textContent = `${Preset.Label} `;
    HostNodes.SurfaceIdentityLabel.textContent = Preset.Label.toLowerCase();
    for (const Pill of HostNodes.SurfacePresetRow.querySelectorAll("[data-surface-preset]"))
    {
        Pill.classList.toggle("is-active", Number(Pill.dataset.surfacePreset) === PresetIndex);
    }
    RefreshInspector(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSFORM TOOLS
//------------------------------------------------------------------------------------------------------------------------

function ConfigureTransformMode(ModeIdentity)
{
    WorkspaceState.TransformMode = ModeIdentity;
    for (const ToolButton of document.querySelectorAll("[data-tool]"))
    {
        ToolButton.classList.toggle("is-active", ToolButton.dataset.tool === ModeIdentity);
    }

    const Hints = {
        select: "Drag to orbit · scroll to dolly",
        move: "Drag selected record to move",
        rotate: "Drag horizontally to rotate",
        scale: "Drag vertically to scale"
    };
    Query("ViewHint").textContent = Hints[ModeIdentity] || Hints.select;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VIEWPORT POINTER CONTROL
//------------------------------------------------------------------------------------------------------------------------

const CanvasPointer = {
    Identity: null,
    Mode: "orbit",
    StartX: 0,
    StartY: 0,
    LastX: 0,
    LastY: 0,
    Moved: false,
    StartPosition: null,
    StartRotationY: 0,
    StartScale: 1
};

function AttachViewportInteraction()
{
    const Canvas = Query("ViewportCanvas");

    Canvas.addEventListener("contextmenu", (Event) => Event.preventDefault());

    Canvas.addEventListener("pointerdown", (Event) =>
    {
        if (CanvasPointer.Identity !== null)
        {
            return;
        }

        Canvas.focus();
        CanvasPointer.Identity = Event.pointerId;
        CanvasPointer.StartX = Event.clientX;
        CanvasPointer.StartY = Event.clientY;
        CanvasPointer.LastX = Event.clientX;
        CanvasPointer.LastY = Event.clientY;
        CanvasPointer.Moved = false;

        const SelectedRecord = RetrieveSelectedRecord();
        const PanRequested = Event.button === 1 || Event.button === 2 || Event.shiftKey;

        if (!PanRequested && WorkspaceState.TransformMode !== "select" && SelectedRecord)
        {
            const StageBounds = HostNodes.Stage.getBoundingClientRect();
            const HitRecord = Renderer.SelectEntryAtScreen(Event.clientX - StageBounds.left, Event.clientY - StageBounds.top);
            if (HitRecord && HitRecord.Token === SelectedRecord.Token)
            {
                CanvasPointer.Mode = "transform";
                CanvasPointer.StartPosition = [ ...SelectedRecord.Placement.Position ];
                CanvasPointer.StartRotationY = SelectedRecord.Placement.Rotation[1];
                CanvasPointer.StartScale = SelectedRecord.Placement.Scaling[0];
                Canvas.classList.add("is-transforming");
                Canvas.setPointerCapture(Event.pointerId);
                return;
            }
        }

        CanvasPointer.Mode = PanRequested ? "pan" : "orbit";
        Canvas.classList.add("is-grabbing");
        Canvas.setPointerCapture(Event.pointerId);
    });

    Canvas.addEventListener("pointermove", (Event) =>
    {
        if (CanvasPointer.Identity !== Event.pointerId)
        {
            return;
        }

        const LateralDelta = Event.clientX - CanvasPointer.LastX;
        const ElevationDelta = Event.clientY - CanvasPointer.LastY;
        CanvasPointer.LastX = Event.clientX;
        CanvasPointer.LastY = Event.clientY;

        if (Math.hypot(Event.clientX - CanvasPointer.StartX, Event.clientY - CanvasPointer.StartY) > 4)
        {
            CanvasPointer.Moved = true;
        }

        if (CanvasPointer.Mode === "orbit")
        {
            Renderer.OrbitCamera(LateralDelta, ElevationDelta);
            SynchronizeCameraFields();
            return;
        }
        if (CanvasPointer.Mode === "pan")
        {
            Renderer.PanCamera(LateralDelta, ElevationDelta);
            return;
        }
        if (CanvasPointer.Mode === "transform")
        {
            ApplyTransformDrag(Event);
        }
    });

    const ConcludePointer = (Event) =>
    {
        if (CanvasPointer.Identity !== Event.pointerId)
        {
            return;
        }

        const ConcludedMode = CanvasPointer.Mode;
        CanvasPointer.Identity = null;
        Canvas.classList.remove("is-grabbing", "is-transforming");

        if (!CanvasPointer.Moved && ConcludedMode !== "transform")
        {
            const StageBounds = HostNodes.Stage.getBoundingClientRect();
            const HitRecord = Renderer.SelectEntryAtScreen(Event.clientX - StageBounds.left, Event.clientY - StageBounds.top);
            SelectRecord(HitRecord ? HitRecord.Token : null, false);
        }

        if (ConcludedMode === "transform")
        {
            RefreshInspector(false);
        }
    };

    Canvas.addEventListener("pointerup", ConcludePointer);
    Canvas.addEventListener("pointercancel", ConcludePointer);

    Canvas.addEventListener("wheel", (Event) =>
    {
        Event.preventDefault();
        Renderer.DollyCamera(Event.deltaY);
        SynchronizeCameraFields();
    }, { passive: false });

    Canvas.addEventListener("dblclick", () =>
    {
        Renderer.FrameSelection();
        SynchronizeCameraFields();
    });

    const ViewScaleChips = document.querySelectorAll(".view-scale-chips span");
    for (const Chip of ViewScaleChips)
    {
        Chip.addEventListener("click", () =>
        {
            for (const Sibling of ViewScaleChips)
            {
                Sibling.classList.toggle("is-active", Sibling === Chip);
            }
            const Multiplier = Number.parseFloat(Chip.textContent) || 1.0;
            Renderer.Camera.FieldOfView = ClampOperand(34.0 / Multiplier, 14.0, 72.0);
            SynchronizeCameraFields();
        });
    }
}

function ApplyTransformDrag(Event)
{
    const SelectedRecord = RetrieveSelectedRecord();
    if (!SelectedRecord)
    {
        return;
    }

    const LateralDelta = Event.clientX - CanvasPointer.StartX;
    const ElevationDelta = Event.clientY - CanvasPointer.StartY;

    if (WorkspaceState.TransformMode === "move")
    {
        const Basis = Renderer.RetrieveCameraBasis();
        const Scalar = Renderer.Camera.Distance * 0.0016;
        SelectedRecord.Placement.Position = [
            CanvasPointer.StartPosition[0] + (Basis.Right[0] * LateralDelta - Basis.Up[0] * ElevationDelta) * Scalar,
            CanvasPointer.StartPosition[1] + (Basis.Right[1] * LateralDelta - Basis.Up[1] * ElevationDelta) * Scalar,
            CanvasPointer.StartPosition[2] + (Basis.Right[2] * LateralDelta - Basis.Up[2] * ElevationDelta) * Scalar
        ];
    }
    else if (WorkspaceState.TransformMode === "rotate")
    {
        SelectedRecord.Placement.Rotation[1] = CanvasPointer.StartRotationY + LateralDelta * 0.45;
    }
    else if (WorkspaceState.TransformMode === "scale")
    {
        const Factor = Math.exp(-ElevationDelta * 0.0055);
        const ScaleValue = ClampOperand(CanvasPointer.StartScale * Factor, 0.08, 6.0);
        SelectedRecord.Placement.Scaling = [ ScaleValue, ScaleValue, ScaleValue ];
    }

    RefreshInspector(false);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL SPLITTERS
//------------------------------------------------------------------------------------------------------------------------

function AttachSplitters()
{
    const DefaultWidths = { Outliner: 308, Inspector: 344 };

    for (const Splitter of document.querySelectorAll(".splitter"))
    {
        const PanelIdentity = Splitter.dataset.splitter;
        const PanelNode = PanelIdentity === "Outliner" ? HostNodes.OutlinerPanel : HostNodes.InspectorPanel;
        const VariableIdentity = PanelIdentity === "Outliner" ? "--Outliner-Width" : "--Inspector-Width";
        const DragScratch = { Active: false, StartX: 0, StartWidth: 0 };

        Splitter.addEventListener("pointerdown", (Event) =>
        {
            Event.preventDefault();
            DragScratch.Active = true;
            DragScratch.StartX = Event.clientX;
            DragScratch.StartWidth = PanelNode.getBoundingClientRect().width;
            Splitter.setPointerCapture(Event.pointerId);
            document.body.classList.add("is-resizing");
        });

        Splitter.addEventListener("pointermove", (Event) =>
        {
            if (!DragScratch.Active)
            {
                return;
            }

            const Delta = Event.clientX - DragScratch.StartX;
            const ProposedWidth = PanelIdentity === "Outliner"
                ? DragScratch.StartWidth + Delta
                : DragScratch.StartWidth - Delta;

            const Bounds = PanelIdentity === "Outliner" ? { Minimum: 236, Maximum: 438 } : { Minimum: 268, Maximum: 470 };
            const Width = ClampOperand(ProposedWidth, Bounds.Minimum, Bounds.Maximum);
            HostNodes.DockRow.style.setProperty(VariableIdentity, `${Width}px`);
            SynchronizeCameraFields();
        });

        const ConcludeDrag = (Event) =>
        {
            if (!DragScratch.Active)
            {
                return;
            }
            DragScratch.Active = false;
            document.body.classList.remove("is-resizing");
            if (Splitter.hasPointerCapture(Event.pointerId))
            {
                Splitter.releasePointerCapture(Event.pointerId);
            }
        };

        Splitter.addEventListener("pointerup", ConcludeDrag);
        Splitter.addEventListener("pointercancel", ConcludeDrag);
        Splitter.addEventListener("dblclick", () =>
        {
            HostNodes.DockRow.style.setProperty(VariableIdentity, `${DefaultWidths[PanelIdentity]}px`);
        });
    }

    for (const PanelButton of document.querySelectorAll("[data-panel-action]"))
    {
        PanelButton.addEventListener("click", () =>
        {
            const PanelNode = Query(PanelButton.dataset.target);
            const Collapsed = PanelButton.dataset.panelAction === "collapse";
            PanelNode.classList.toggle("is-collapsed", Collapsed);
        });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SELECTION RECTANGLE REFLECTION
//------------------------------------------------------------------------------------------------------------------------

let LastSelectionSignature = "";

function ReflectSelectionRect(SelectionRect)
{
    const Brackets = HostNodes.SelectionFrameBrackets;
    const SelectedRecord = RetrieveSelectedRecord();

    if (!SelectionRect || !SelectedRecord)
    {
        Brackets.replaceChildren();
        HostNodes.SelectionAnnotation.style.opacity = "0";
        HostNodes.SelectionLeader.setAttribute("x1", "0");
        HostNodes.SelectionLeader.setAttribute("y1", "0");
        HostNodes.SelectionLeader.setAttribute("x2", "0");
        HostNodes.SelectionLeader.setAttribute("y2", "0");
        LastSelectionSignature = "";
        return;
    }

    const Signature = [ SelectionRect.Left, SelectionRect.Top, SelectionRect.Width, SelectionRect.Height ]
        .map((Component) => Component.toFixed(1))
        .join(":");

    if (Signature === LastSelectionSignature)
    {
        return;
    }
    LastSelectionSignature = Signature;

    const { Left, Top, Width, Height } = SelectionRect;
    const CornerLength = Math.min(20.0, Math.max(10.0, Math.min(Width, Height) * 0.16));
    const Corners = [
        [ Left, Top, 1, 1 ],
        [ Left + Width, Top, -1, 1 ],
        [ Left, Top + Height, 1, -1 ],
        [ Left + Width, Top + Height, -1, -1 ]
    ];

    const Namespace = "http://www.w3.org/2000/svg";
    Brackets.replaceChildren();

    const DashedRectangle = document.createElementNS(Namespace, "rect");
    DashedRectangle.setAttribute("x", String(Left));
    DashedRectangle.setAttribute("y", String(Top));
    DashedRectangle.setAttribute("width", String(Math.max(0.0, Width)));
    DashedRectangle.setAttribute("height", String(Math.max(0.0, Height)));
    DashedRectangle.setAttribute("class", "frame-dashed");
    Brackets.appendChild(DashedRectangle);

    for (const [CornerX, CornerY, DirectionX, DirectionY] of Corners)
    {
        const Path = document.createElementNS(Namespace, "path");
        Path.setAttribute("d", `M ${CornerX + DirectionX * CornerLength} ${CornerY} L ${CornerX} ${CornerY} L ${CornerX} ${CornerY + DirectionY * CornerLength}`);
        Path.setAttribute("class", "frame-bracket");
        Brackets.appendChild(Path);
    }

    const HandleCenters = [
        [ Left + Width * 0.5, Top ],
        [ Left + Width * 0.5, Top + Height ],
        [ Left, Top + Height * 0.5 ],
        [ Left + Width, Top + Height * 0.5 ]
    ];
    for (const [HandleX, HandleY] of HandleCenters)
    {
        const Handle = document.createElementNS(Namespace, "rect");
        Handle.setAttribute("x", String(HandleX - 2.5));
        Handle.setAttribute("y", String(HandleY - 2.5));
        Handle.setAttribute("width", "5");
        Handle.setAttribute("height", "5");
        Handle.setAttribute("rx", "1");
        Handle.setAttribute("class", "frame-handle");
        Brackets.appendChild(Handle);
    }

    const StageBounds = HostNodes.Stage.getBoundingClientRect();
    const AnnotationWidth = 168.0;
    const TelemetryReserve = 174.0;
    const RightLimit = Math.max(8.0, StageBounds.width - TelemetryReserve - AnnotationWidth);

    let AnnotationLeft = SelectionRect.AnchorX + 26.0;
    if (AnnotationLeft > RightLimit)
    {
        AnnotationLeft = SelectionRect.AnchorX - 26.0 - AnnotationWidth;
    }
    AnnotationLeft = ClampOperand(AnnotationLeft, 8.0, Math.max(8.0, StageBounds.width - AnnotationWidth - 8.0));
    const AnnotationTop = ClampOperand(SelectionRect.AnchorY, 26.0, Math.max(26.0, StageBounds.height - 120.0));

    HostNodes.SelectionAnnotation.style.left = `${AnnotationLeft}px`;
    HostNodes.SelectionAnnotation.style.top = `${AnnotationTop}px`;
    HostNodes.SelectionAnnotation.style.opacity = "1";
    HostNodes.SelectionLeader.setAttribute("x1", String(SelectionRect.AnchorX));
    HostNodes.SelectionLeader.setAttribute("y1", String(SelectionRect.AnchorY));
    HostNodes.SelectionLeader.setAttribute("x2", String(AnnotationLeft));
    HostNodes.SelectionLeader.setAttribute("y2", String(AnnotationTop));

    HostNodes.AnnotationName.textContent = SelectedRecord.Name;
    if (SelectedRecord.Category === "geometry" && Renderer.Context)
    {
        const Statistics = Renderer.RetrieveRecordStatistics(SelectedRecord.Primitive);
        HostNodes.AnnotationMeta.textContent = `${FormatCount(Statistics.VertexTotal)} v`;
    }
    else
    {
        HostNodes.AnnotationMeta.textContent = SelectedRecord.Category;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TIMELINE REFLECTION
//------------------------------------------------------------------------------------------------------------------------

function BuildTimelineRuler()
{
    HostNodes.TimelineRuler.replaceChildren();
    for (let Index = 0; Index < 25; Index += 1)
    {
        const Tick = document.createElement("span");
        Tick.className = Index % 6 === 0 ? "timeline-tick is-major" : "timeline-tick";
        HostNodes.TimelineRuler.appendChild(Tick);
    }
}

function FormatTimelineClock(Seconds)
{
    const Whole = Math.max(0, Math.floor(Seconds));
    return `00:${String(Whole).padStart(2, "0")}`;
}

function ReflectTimeline()
{
    const Progress = Renderer.SweepPhase;
    HostNodes.TimelineProgress.style.width = `${Progress * 100}%`;
    HostNodes.TimelinePlayhead.style.left = `${Progress * 100}%`;
    HostNodes.TimelineTime.textContent = `${FormatTimelineClock(Progress * 20.0)} / 00:20`;
    HostNodes.TimelinePlay.classList.toggle("is-playing", Renderer.SweepPlaying);
    HostNodes.SweepSwitch.classList.toggle("is-on", Renderer.SweepPlaying);
    HostNodes.SweepSwitch.setAttribute("aria-pressed", Renderer.SweepPlaying ? "true" : "false");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HOST FRAME LOOP
//------------------------------------------------------------------------------------------------------------------------

let PreviousHostTimestamp = 0.0;

function EvaluateHostFrame(Timestamp)
{
    const Elapsed = PreviousHostTimestamp ? Math.min(0.06, (Timestamp - PreviousHostTimestamp) / 1000.0) : 0.016;
    PreviousHostTimestamp = Timestamp;

    if (WorkspaceState.AutoOrbit)
    {
        Renderer.Camera.Yaw += Elapsed * 0.34;
        SynchronizeCameraFields();
    }

    ReflectTimeline();

    if (WorkspaceState.InspectorTab === "view")
    {
        HostNodes.RenderSummary.textContent = `WebGL2 · ${Math.round(Renderer.FrameRateEstimate || 60)} fps`;
    }

    requestAnimationFrame(EvaluateHostFrame);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    KEYBOARD ROUTING
//------------------------------------------------------------------------------------------------------------------------

function AttachKeyboardRouting()
{
    window.addEventListener("keydown", (Event) =>
    {
        const ActiveTag = document.activeElement ? document.activeElement.tagName : "";
        if (ActiveTag === "INPUT" || ActiveTag === "TEXTAREA")
        {
            return;
        }

        const ModifierPressed = Event.ctrlKey || Event.metaKey;

        if (ModifierPressed && Event.key.toLowerCase() === "d")
        {
            Event.preventDefault();
            DuplicateSelectedRecord();
            return;
        }
        if (ModifierPressed && Event.key.toLowerCase() === "k")
        {
            Event.preventDefault();
            HostNodes.SearchField.focus();
            return;
        }

        switch (Event.key.toLowerCase())
        {
            case "q":
                ConfigureTransformMode("select");
                break;
            case "w":
                ConfigureTransformMode("move");
                break;
            case "e":
                ConfigureTransformMode("rotate");
                break;
            case "r":
                ConfigureTransformMode("scale");
                break;
            case "f":
                Renderer.FrameSelection();
                SynchronizeCameraFields();
                break;
            case "g":
                WorkspaceState.GridEnabled = !WorkspaceState.GridEnabled;
                Renderer.ConfigureGridVisibility(WorkspaceState.GridEnabled);
                SynchronizeViewControls();
                break;
            case " ":
                Event.preventDefault();
                Renderer.ConfigureSweepPlaying(!Renderer.SweepPlaying);
                ReflectTimeline();
                break;
            case "delete":
            case "backspace":
                RemoveSelectedRecord();
                break;
            case "escape":
                SelectRecord(null, false);
                ClosePrimitiveMenu();
                break;
            default:
                break;
        }
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERFACE BINDING
//------------------------------------------------------------------------------------------------------------------------

function AttachInterface()
{
    for (const Field of document.querySelectorAll(".numeric-input"))
    {
        AttachNumericField(Field);
    }
    for (const Row of document.querySelectorAll("[data-surface-property], [data-camera-property]"))
    {
        AttachSlider(Row);
    }

    for (const Tab of document.querySelectorAll("[data-inspector-tab]"))
    {
        Tab.addEventListener("click", () => ConfigureInspectorTab(Tab.dataset.inspectorTab));
    }

    HostNodes.SurfacePresetRow.addEventListener("click", (Event) =>
    {
        const Pill = Event.target.closest("[data-surface-preset]");
        if (!Pill)
        {
            return;
        }
        SurfacePresetIndex = Number(Pill.dataset.surfacePreset);
        ApplySurfacePreset(SurfacePresetIndex);
    });
    for (const Tab of document.querySelectorAll("[data-outliner-tab]"))
    {
        Tab.addEventListener("click", () => ConfigureOutlinerTab(Tab.dataset.outlinerTab));
    }
    for (const Pill of document.querySelectorAll(".switch-pill"))
    {
        Pill.addEventListener("click", () =>
        {
            for (const Sibling of document.querySelectorAll(".switch-pill"))
            {
                Sibling.classList.toggle("is-active", Sibling === Pill);
            }
        });
    }

    HostNodes.AddRecordButton.addEventListener("click", (Event) =>
    {
        Event.stopPropagation();
        if (HostNodes.PrimitiveMenu.hidden)
        {
            OpenPrimitiveMenu();
        }
        else
        {
            ClosePrimitiveMenu();
        }
    });
    document.addEventListener("click", (Event) =>
    {
        if (!HostNodes.PrimitiveMenu.hidden && !HostNodes.PrimitiveMenu.contains(Event.target) && Event.target !== HostNodes.AddRecordButton)
        {
            ClosePrimitiveMenu();
        }
    });

    for (const PrimitiveButton of document.querySelectorAll("[data-primitive]"))
    {
        PrimitiveButton.addEventListener("click", () => AddPrimitiveRecord(PrimitiveButton.dataset.primitive));
    }

    for (const ZeroButton of document.querySelectorAll("[data-zero-field]"))
    {
        ZeroButton.addEventListener("click", () =>
        {
            const SelectedRecord = RetrieveSelectedRecord();
            if (!SelectedRecord)
            {
                return;
            }
            const PlacementKey = ZeroButton.dataset.zeroField;
            SelectedRecord.Placement[PlacementKey] = PlacementKey === "Scaling" ? [ 1.0, 1.0, 1.0 ] : [ 0.0, 0.0, 0.0 ];
            RefreshInspector(false);
        });
    }

    for (const ToolButton of document.querySelectorAll("[data-tool]"))
    {
        ToolButton.addEventListener("click", () => ConfigureTransformMode(ToolButton.dataset.tool));
    }

    for (const Switch of document.querySelectorAll("[data-toggle]"))
    {
        Switch.addEventListener("click", () =>
        {
            WorkspaceState.UniformScale = !WorkspaceState.UniformScale;
            Switch.classList.toggle("is-on", WorkspaceState.UniformScale);
            Switch.setAttribute("aria-pressed", WorkspaceState.UniformScale ? "true" : "false");
        });
    }

    for (const Switch of document.querySelectorAll("[data-view-toggle]"))
    {
        Switch.addEventListener("click", () =>
        {
            const ToggleIdentity = Switch.dataset.viewToggle;
            if (ToggleIdentity === "GridEnabled")
            {
                WorkspaceState.GridEnabled = !WorkspaceState.GridEnabled;
                Renderer.ConfigureGridVisibility(WorkspaceState.GridEnabled);
            }
            else if (ToggleIdentity === "AutoOrbit")
            {
                WorkspaceState.AutoOrbit = !WorkspaceState.AutoOrbit;
            }
            else if (ToggleIdentity === "SelectionAxes")
            {
                WorkspaceState.SelectionAxes = !WorkspaceState.SelectionAxes;
                Renderer.ConfigureSelectionAxes(WorkspaceState.SelectionAxes);
            }
            SynchronizeViewControls();
        });
    }

    HostNodes.BaseColorInput.addEventListener("input", () =>
    {
        const SelectedRecord = RetrieveSelectedRecord();
        if (SelectedRecord && SelectedRecord.Surface)
        {
            SelectedRecord.Surface.BaseColor = ColorFromHex(HostNodes.BaseColorInput.value);
            HostNodes.BaseColorCode.textContent = HostNodes.BaseColorInput.value.toUpperCase();
        }
    });
    HostNodes.RimColorInput.addEventListener("input", () =>
    {
        const SelectedRecord = RetrieveSelectedRecord();
        if (SelectedRecord && SelectedRecord.Surface)
        {
            SelectedRecord.Surface.RimColor = ColorFromHex(HostNodes.RimColorInput.value);
            HostNodes.RimColorCode.textContent = HostNodes.RimColorInput.value.toUpperCase();
        }
    });

    HostNodes.GridToggle.addEventListener("click", () =>
    {
        WorkspaceState.GridEnabled = !WorkspaceState.GridEnabled;
        Renderer.ConfigureGridVisibility(WorkspaceState.GridEnabled);
        SynchronizeViewControls();
    });
    HostNodes.AutoOrbitButton.addEventListener("click", () =>
    {
        WorkspaceState.AutoOrbit = !WorkspaceState.AutoOrbit;
        SynchronizeViewControls();
    });
    HostNodes.ResetViewButton.addEventListener("click", () =>
    {
        Renderer.ResetView();
        SynchronizeCameraFields();
    });
    HostNodes.FrameButton.addEventListener("click", () =>
    {
        Renderer.FrameSelection();
        SynchronizeCameraFields();
    });
    HostNodes.InspectorFocus.addEventListener("click", () =>
    {
        Renderer.FrameSelection();
        SynchronizeCameraFields();
    });
    HostNodes.DuplicateButton.addEventListener("click", DuplicateSelectedRecord);
    HostNodes.DuplicateRecordButton.addEventListener("click", DuplicateSelectedRecord);
    HostNodes.ResetSelectionButton.addEventListener("click", () =>
    {
        const SelectedRecord = RetrieveSelectedRecord();
        if (!SelectedRecord)
        {
            return;
        }
        SelectedRecord.Placement.Rotation = [ 0.0, 0.0, 0.0 ];
        SelectedRecord.Placement.Scaling = [ 1.0, 1.0, 1.0 ];
        if (SelectedRecord.Surface)
        {
            SelectedRecord.Surface.RimStrength = 0.85;
            SelectedRecord.Surface.WireStrength = 0.15;
            SelectedRecord.Surface.SweepStrength = 0.80;
        }
        RefreshInspector(true);
    });

    HostNodes.SweepSwitch.addEventListener("click", () =>
    {
        Renderer.ConfigureSweepPlaying(!Renderer.SweepPlaying);
        ReflectTimeline();
    });
    HostNodes.TimelinePlay.addEventListener("click", () =>
    {
        Renderer.ConfigureSweepPlaying(!Renderer.SweepPlaying);
        ReflectTimeline();
    });
    HostNodes.TimelineRestart.addEventListener("click", () =>
    {
        Renderer.SweepPhase = 0.0;
        ReflectTimeline();
    });
    HostNodes.TimelineAdvance.addEventListener("click", () =>
    {
        Renderer.SweepPhase = (Renderer.SweepPhase + 0.12) % 1.0;
        ReflectTimeline();
    });

    HostNodes.SearchField.addEventListener("input", () =>
    {
        WorkspaceState.SearchQuery = HostNodes.SearchField.value;
        RenderRecordTree();
    });

    for (const FilterPill of document.querySelectorAll("[data-filter]"))
    {
        FilterPill.addEventListener("click", () =>
        {
            WorkspaceState.FilterCategory = FilterPill.dataset.filter;
            for (const Sibling of document.querySelectorAll("[data-filter]"))
            {
                Sibling.classList.toggle("is-active", Sibling === FilterPill);
            }
            RenderRecordTree();
        });
    }

    Query("TreeGroupWorld").querySelector(".tree-group-head").addEventListener("click", () =>
    {
        HostNodes.TreeGroup.classList.toggle("is-folded");
    });

    Query("ViewModeChip").addEventListener("click", () =>
    {
        const OrthographicRequested = Renderer.ProjectionMode !== "orthographic";
        Renderer.ConfigureProjectionMode(OrthographicRequested ? "orthographic" : "perspective");
        Query("ViewModeChip").firstChild.textContent = OrthographicRequested ? "Orthographic " : "Perspective ";
    });

    Query("SurfaceModeChip").addEventListener("click", () =>
    {
        SurfacePresetIndex = (SurfacePresetIndex + 1) % SurfacePresets.length;
        ApplySurfacePreset(SurfacePresetIndex);
    });

    for (const RateChip of Query("TimelineRate").querySelectorAll("span"))
    {
        RateChip.addEventListener("click", () =>
        {
            for (const Sibling of Query("TimelineRate").querySelectorAll("span"))
            {
                Sibling.classList.toggle("is-active", Sibling === RateChip);
            }
            Renderer.ConfigureSweepRate(Number.parseFloat(RateChip.dataset.rate) || 1.0);
        });
    }

    AttachSplitters();
    AttachViewportInteraction();
    AttachKeyboardRouting();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INITIALIZATION
//------------------------------------------------------------------------------------------------------------------------

function InitializeWorkspace()
{
    BuildTimelineRuler();
    AttachInterface();
    RenderRecordTree();
    RefreshInspector(true);
    ConfigureInspectorTab("object");
    ConfigureOutlinerTab("scene");
    ConfigureTransformMode("select");
    SynchronizeViewControls();
    SynchronizeCameraFields();
    Renderer.ConfigureSelection(WorkspaceState.SelectedToken);
    Renderer.FrameSelection();
    SynchronizeCameraFields();
    ReflectTimeline();
    requestAnimationFrame(EvaluateHostFrame);
}

InitializeWorkspace();
