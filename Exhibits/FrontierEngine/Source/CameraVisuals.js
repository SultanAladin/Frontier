//============================================================================================================================================
//                                                            CAMERAVISUALS.JS
//============================================================================================================================================
// 🧩 1. Low-poly 3D camera geometry (Vehicle Rig Camera + Cinema Production Camera) in Frontier's +X forward, +Y left, +Z up frame.
//    2. Exact FrontierEditor interactive camera visual cards (SensorField, 8-blade Iris, draggable DepthOfField) for FrontierEngine.

import { CourseMaterial } from "./MaterialSpecification.js";
import { EmitBoxFacets, EmitQuad } from "./CourseStructure.js";

function TranslateFacets(Facets, Dx, Dy, Dz)
{
    return Facets.map((Facet) => ({
        ...Facet,
        Corners: Facet.Corners.map(([X, Y, Z]) => [X + Dx, Y + Dy, Z + Dz])
    }));
}

// Cylindrical prism aligned along +X (lens optical axis), centred at [Cx, Cy, Cz].
function EmitLensCylinderX(X0, X1, Radius0, Radius1, Segments, Family, CapFrontFamily = -1)
{
    const Facets = [];
    for (let Index = 0; Index < Segments; ++Index)
    {
        const A0 = (2.0 * Math.PI * Index) / Segments;
        const A1 = (2.0 * Math.PI * (Index + 1)) / Segments;
        const Y0A = Math.cos(A0) * Radius0, Z0A = Math.sin(A0) * Radius0;
        const Y0B = Math.cos(A1) * Radius0, Z0B = Math.sin(A1) * Radius0;
        const Y1A = Math.cos(A0) * Radius1, Z1A = Math.sin(A0) * Radius1;
        const Y1B = Math.cos(A1) * Radius1, Z1B = Math.sin(A1) * Radius1;
        EmitQuad(Facets, [X0, Y0A, Z0A], [X1, Y1A, Z1A], [X1, Y1B, Z1B], [X0, Y0B, Z0B], Family);
        if (CapFrontFamily >= 0)
        {
            Facets.push({
                Corners: [[X1, 0.0, 0.0], [X1, Y1A, Z1A], [X1, Y1B, Z1B]],
                Family: CapFrontFamily
            });
        }
    }
    return Facets;
}

// Cylindrical drum aligned along +Y (film reel magazine axis), centred at [Cx, 0, Cz].
function EmitReelDrumY(Cx, Cz, HalfWidth, Radius, Segments, Family)
{
    const Facets = [];
    const Y0 = -HalfWidth, Y1 = HalfWidth;
    for (let Index = 0; Index < Segments; ++Index)
    {
        const A0 = (2.0 * Math.PI * Index) / Segments;
        const A1 = (2.0 * Math.PI * (Index + 1)) / Segments;
        const X0 = Cx + Math.cos(A0) * Radius, Z0 = Cz + Math.sin(A0) * Radius;
        const X1 = Cx + Math.cos(A1) * Radius, Z1 = Cz + Math.sin(A1) * Radius;
        EmitQuad(Facets, [X0, Y0, Z0], [X1, Y0, Z1], [X1, Y1, Z1], [X0, Y1, Z0], Family);
        Facets.push({ Corners: [[Cx, Y1, Cz], [X0, Y1, Z0], [X1, Y1, Z1]], Family });
        Facets.push({ Corners: [[Cx, Y0, Cz], [X1, Y0, Z1], [X0, Y0, Z0]], Family });
    }
    return Facets;
}

// 📝 Low-poly Vehicle Rig Camera (+X forward, +Y left, +Z up): compact matte body + lens barrel + optical glass + mount foot.
export function EmitVehicleCameraFacets()
{
    const Body   = CourseMaterial.StudioFrame;
    const Barrel = CourseMaterial.CabinLeather;
    const Accent = CourseMaterial.DisplayPaintA;
    const Glass  = CourseMaterial.VehicleGlass;

    const Facets = [
        // Main camera body housing
        ...TranslateFacets(EmitBoxFacets(0.30, 0.22, 0.20, Body), -0.06, 0.0, -0.10),
        // Top cold-shoe & tally accent bar
        ...TranslateFacets(EmitBoxFacets(0.16, 0.08, 0.04, Accent), -0.04, 0.0, 0.10),
        // Under-body rig mounting stem
        ...TranslateFacets(EmitBoxFacets(0.08, 0.08, 0.14, Barrel), -0.06, 0.0, -0.24),
        // Stepped prime lens barrel + flared hood + optical front element
        ...EmitLensCylinderX(0.09, 0.23, 0.075, 0.075, 12, Barrel, -1),
        ...EmitLensCylinderX(0.23, 0.31, 0.075, 0.105, 12, Body, -1),
        ...EmitLensCylinderX(0.21, 0.24, 0.068, 0.068, 12, Glass, Glass)
    ];
    return Facets;
}

// 📝 Low-poly Cinema Production Camera (+X forward, +Y left, +Z up): body + dual top film reels + matte box + prime lens.
export function EmitCineCameraFacets()
{
    const Body   = CourseMaterial.StudioFrame;
    const Trim   = CourseMaterial.CabinLeather;
    const Accent = CourseMaterial.DisplayPaintB;
    const Glass  = CourseMaterial.VehicleGlass;

    const Facets = [
        // Cinema body block
        ...TranslateFacets(EmitBoxFacets(0.46, 0.26, 0.28, Body), -0.10, 0.0, -0.14),
        // Side electronics / timecode module
        ...TranslateFacets(EmitBoxFacets(0.28, 0.30, 0.16, Trim), -0.10, 0.0, -0.08),
        // Top carry handle rail
        ...TranslateFacets(EmitBoxFacets(0.30, 0.05, 0.04, Accent), -0.08, 0.0, 0.18),
        // Dual coaxial 35mm film magazines
        ...EmitReelDrumY(-0.02, 0.24, 0.055, 0.115, 14, Body),
        ...EmitReelDrumY(-0.23, 0.22, 0.055, 0.105, 14, Body),
        // Cinema prime lens barrel + front glass
        ...EmitLensCylinderX(0.13, 0.30, 0.090, 0.090, 14, Trim, -1),
        ...EmitLensCylinderX(0.28, 0.31, 0.082, 0.082, 14, Glass, Glass),
        // Flared 4-sided cinema matte box
        ...EmitLensCylinderX(0.30, 0.44, 0.095, 0.155, 4, Body, -1)
    ];
    return Facets;
}

//------------------------------------------------------------------------------------------------------------------------
//                                  THIN-LENS OPTICS (exact port of FrontierEditor/optics.js)
//------------------------------------------------------------------------------------------------------------------------

export function ComputeCameraOptics(FocalMm, Aperture, FocusMetres, SensorWidth = 36, SensorHeight = 24)
{
    const Focal = Math.max(8, Number(FocalMm) || 35);
    const FStop = Math.max(1.0, Number(Aperture) || 2.8);
    const Focus = Math.max(0.5, Number(FocusMetres) || 10);
    const Coc = 0.03;
    const S = Focus * 1000.0;
    const HyperfocalMm = (Focal * Focal) / (FStop * Coc) + Focal;
    const Near = (HyperfocalMm * S) / (HyperfocalMm + S - Focal) / 1000.0;
    const FarDenominator = HyperfocalMm - (S - Focal);
    const Far = FarDenominator <= 0 ? Infinity : (HyperfocalMm * S) / FarDenominator / 1000.0;
    return {
        HorizontalFov: (2.0 * Math.atan(SensorWidth / (2.0 * Focal)) * 180.0) / Math.PI,
        VerticalFov:   (2.0 * Math.atan(SensorHeight / (2.0 * Focal)) * 180.0) / Math.PI,
        Pupil:         Focal / FStop,
        Near,
        Far,
        Depth:         Far - Near,
        Hyperfocal:    HyperfocalMm / 1000.0,
        FrameWidth:    (Focus * SensorWidth) / Focal,
        FrameHeight:   (Focus * SensorHeight) / Focal
    };
}

export function FocalLengthFromVerticalFov(VerticalFovDegrees, SensorHeight = 24)
{
    const Rad = (Math.max(8, Math.min(130, VerticalFovDegrees)) * Math.PI) / 180.0;
    return SensorHeight / (2.0 * Math.tan(0.5 * Rad));
}

export function VerticalFovFromFocalLength(FocalMm, SensorHeight = 24)
{
    return (2.0 * Math.atan(SensorHeight / (2.0 * Math.max(8, FocalMm))) * 180.0) / Math.PI;
}

export function FormatOpticDistance(Metres)
{
    return Number.isFinite(Metres) ? `${Metres.toFixed(1)} m` : "∞";
}

//------------------------------------------------------------------------------------------------------------------------
//                      INTERACTIVE FRONTIEREDITOR CAMERA CARDS (SensorField + Iris + DepthOfField)
//------------------------------------------------------------------------------------------------------------------------

let SvgInstanceCounter = 0;

function RenderSensorFieldSvg(Focal, Aperture, Focus, Optics, GradId, SensorLabel = "36 × 24 mm")
{
    const Extent = 34 + Optics.HorizontalFov * 0.85;
    const Gap = 8 + Math.min(38, Optics.Pupil * 0.75);
    const Ticks = [0, 1, 2, 3, 4, 5].map((I) => `<path d="M71 ${108 + I * 13} H77" stroke="#d0dae2" stroke-width=".6"/>`).join("");
    return `<svg viewBox="0 0 640 290" role="img" aria-label="Full-frame sensor schematic">
        <defs><linearGradient id="${GradId}"><stop stop-color="#b59acc" stop-opacity=".02"/><stop offset="1" stop-color="#b59acc" stop-opacity=".12"/></linearGradient></defs>
        <path d="M30 141 H610" stroke="#b3a5bf25" stroke-dasharray="3 6"/>
        <path class="optical-envelope" d="M235 141 L553 ${(141 - Extent).toFixed(1)} V${(141 + Extent).toFixed(1)} Z" fill="url(#${GradId})" stroke="#ae98c7" stroke-width="1"/>
        <path d="M78 107 L235 141 L553 ${(141 + Extent).toFixed(1)} M78 175 L235 141 L553 ${(141 - Extent).toFixed(1)}" fill="none" stroke="#b79dd0" stroke-opacity=".36" stroke-width=".8"/>
        <rect x="68" y="100" width="12" height="82" rx="3" fill="#a4b6c51f" stroke="#b9c7d6" stroke-width="1.2"/>
        ${Ticks}
        <path d="M64 94 H57 V187 H64" stroke="#6d7b8a" fill="none" stroke-width=".6"/>
        <path class="pupil-gate" d="M235 72 V${(141 - Gap).toFixed(1)} M235 ${(141 + Gap).toFixed(1)} V210" stroke="#b9a5cd" stroke-width="7" stroke-linecap="round"/>
        <path d="M225 ${(141 - Gap).toFixed(1)} H245 M225 ${(141 + Gap).toFixed(1)} H245" stroke="#e2d0f4" stroke-width="1.2"/>
        <path d="M553 ${(141 - Extent).toFixed(1)} V${(141 + Extent).toFixed(1)}" stroke="#d6c6e8" stroke-width="2"/>
        <path d="M545 ${(141 - Extent).toFixed(1)} H562 M545 ${(141 + Extent).toFixed(1)} H562" stroke="#d6c6e8" stroke-width="1"/>
        <path d="M320 111 Q328 141 320 171" stroke="#a68abb" fill="none" stroke-width=".7"/>
        <text x="342" y="134" fill="#ddcbea" font-size="21" font-weight="300">${Optics.HorizontalFov.toFixed(1)}°</text>
        <text x="343" y="152" fill="#8f829d" font-size="8" letter-spacing="1">HORIZONTAL FOV</text>
        <g text-anchor="middle">
            <text x="75" y="236" fill="#afbbc8" font-size="10">Sensor</text>
            <text x="75" y="254" fill="#737b85" font-size="9">${SensorLabel}</text>
            <text x="235" y="236" fill="#c5afd9" font-size="10">ƒ/${Number(Aperture).toFixed(1)}</text>
            <text x="235" y="254" fill="#8a7c97" font-size="9">Ø ${Optics.Pupil.toFixed(1)} mm pupil</text>
            <text x="553" y="270" fill="#b5a5c4" font-size="9">Focus plane · ${Number(Focus).toFixed(0)} m</text>
        </g>
        <path d="M84 211 H225" stroke="#5d5267" stroke-width=".6"/>
        <text x="154" y="206" fill="#8f819b" font-size="9" text-anchor="middle">${Number(Focal).toFixed(0)} mm</text>
    </svg>
    <div class="fe-sensor-caption"><span>Optical schematic · not to scale</span><span>Full-frame / 3:2</span></div>`;
}

function RenderIrisSvg(Aperture, GradId)
{
    const Opening = Math.max(0, Math.min(1, (16 / Math.max(1.4, Aperture) - 1) / (16 / 1.4 - 1)));
    const Radius = 10 + Opening * 47;
    const Point = (Angle, R) => [(110 + Math.cos(Angle) * R).toFixed(2), (110 + Math.sin(Angle) * R).toFixed(2)];
    const Ticks = Array.from({ length: 48 }, (_, I) =>
        `<path d="M110 16 V20" transform="rotate(${I * 7.5} 110 110)" stroke="#7a747e" stroke-width=".6"/>`
    ).join("");
    const Blades = Array.from({ length: 8 }, (_, I) => {
        const A = (I * Math.PI) / 4;
        const P = Point(A, Radius), Q = Point(A + Math.PI / 4, Radius), O = Point(A + 1.07, 86), N = Point(A + 0.29, 86);
        return `<path d="M${P} L${Q} L${O} A86 86 0 0 0 ${N}Z" fill="${I % 2 ? "#39363e" : "#343238"}" stroke="#79717e" stroke-width=".65"/>`;
    }).join("");
    const Poly = Array.from({ length: 8 }, (_, I) => Point((I * Math.PI) / 4, Radius).join(",")).join(" ");
    return `<svg class="fe-iris-svg" viewBox="0 0 220 220" role="img" aria-label="Aperture blades">
        <defs><radialGradient id="${GradId}"><stop stop-color="#434047"/><stop offset="1" stop-color="#232225"/></radialGradient></defs>
        <circle cx="110" cy="110" r="97" fill="#171717" stroke="#525052"/>
        <circle cx="110" cy="110" r="88" fill="url(#${GradId})" stroke="#454247"/>
        ${Ticks}
        ${Blades}
        <polygon points="${Poly}" fill="#101014" stroke="#b7a7c4" stroke-width="1"/>
        <circle cx="110" cy="110" r="${(Radius * 0.55).toFixed(2)}" fill="#9690b60d"/>
        <circle cx="110" cy="110" r="2" fill="#c1b6cd" opacity=".6"/>
    </svg>`;
}

function RenderDepthOfFieldSvg(Focus, Optics, Enabled)
{
    const MaxDist = 100;
    const MapX = (M) => 22 + (Math.log1p(Math.min(MaxDist, Math.max(0, M))) / Math.log1p(MaxDist)) * 316;
    const NearX = Enabled ? MapX(Optics.Near) : 22;
    const FarX  = Enabled ? MapX(Number.isFinite(Optics.Far) ? Optics.Far : MaxDist) : 338;
    const PlaneX = MapX(Focus);
    const ScaleTicks = [1, 3, 10, 30, 100].map((M) => {
        const X = MapX(M).toFixed(2);
        return `<g><path d="M${X} 120 V125" stroke="#6e788a"/><text x="${X}" y="144" text-anchor="middle" font-size="9" fill="#7e899b">${M} m</text></g>`;
    }).join("");
    const Bars = Array.from({ length: 17 }, (_, I) => {
        const Xx = 30 + I * 18.5;
        const Sharp = Xx >= NearX && Xx <= FarX;
        return `<path d="M${(Xx - 3).toFixed(1)} 62 H${(Xx + 3).toFixed(1)} M${Xx.toFixed(1)} 55 V93 M${(Xx - 3).toFixed(1)} 86 H${(Xx + 3).toFixed(1)}" stroke="#aabbd1" stroke-width=".7" opacity="${Sharp ? "0.78" : "0.2"}" />`;
    }).join("");
    return `<svg class="fe-dof-svg" viewBox="0 0 360 174" role="img" aria-label="Draggable focal plane diagram">
        <rect x="22" y="31" width="316" height="90" rx="9" fill="#18191c" stroke="#ffffff12"/>
        <rect x="${NearX.toFixed(2)}" y="32" width="${Math.max(1, FarX - NearX).toFixed(2)}" height="88" fill="#8eacd326"/>
        <path d="M${NearX.toFixed(2)} 32 V120 M${FarX.toFixed(2)} 32 V120" stroke="#a3badb" stroke-opacity=".55" stroke-dasharray="3 5"/>
        ${ScaleTicks}
        ${Bars}
        <path d="M${PlaneX.toFixed(2)} 21 V121" stroke="#d2dff1" stroke-width="1.3"/>
        <path d="M${(PlaneX - 5).toFixed(2)} 16 L${(PlaneX + 5).toFixed(2)} 16 L${PlaneX.toFixed(2)} 23Z" fill="#d2dff1"/>
        <circle cx="${PlaneX.toFixed(2)}" cy="76" r="9" fill="#1f2937" stroke="#c4d7ee"/>
        <path d="M${(PlaneX - 3).toFixed(2)} 76 H${(PlaneX + 3).toFixed(2)} M${PlaneX.toFixed(2)} 73 V79" stroke="#e3edf7"/>
        <text x="22" y="168" fill="#7e899b" font-size="8" letter-spacing="1">DISTANCE · LOG SCALE (DRAG PLANE)</text>
    </svg>`;
}

// Builds the 3 FrontierEditor interactive visual camera cards into `Body` and binds live updates through `Controls`.
export function BuildInteractiveCameraCards(Controls, Body, ConfigOrTarget, MaybeKeys = null)
{
    let Target;
    let FocalKey = "FocalLength", ApertureKey = "Aperture", FocusKey = "FocusDistance", DofKey = "DofEnabled";
    let Accent = "#bb9ae4", StartIndex = 2, SensorLabel = "36 × 24 mm", OnOpticsChanged = null;

    if (ConfigOrTarget && typeof ConfigOrTarget.ReadState === "function")
    {
        const { ReadState, WriteState } = ConfigOrTarget;
        Accent = ConfigOrTarget.Accent || Accent;
        StartIndex = ConfigOrTarget.StartIndex ?? 2;
        SensorLabel = ConfigOrTarget.SensorLabel || SensorLabel;
        OnOpticsChanged = ConfigOrTarget.OnOpticsChanged || null;
        Target = {
            get FocalLength()   { return ReadState().FocalLength ?? 35; },
            set FocalLength(V)  { WriteState({ FocalLength: V }); },
            get Aperture()      { return ReadState().Aperture ?? 2.8; },
            set Aperture(V)     { WriteState({ Aperture: V }); },
            get FocusDistance() { return ReadState().FocusDistance ?? 8.0; },
            set FocusDistance(V){ WriteState({ FocusDistance: V }); },
            get DofEnabled()    { return ReadState().DofEnabled !== false; },
            set DofEnabled(V)   { WriteState({ DofEnabled: V }); }
        };
    }
    else
    {
        Target = ConfigOrTarget;
        if (MaybeKeys)
        {
            FocalKey    = MaybeKeys.FocalKey || FocalKey;
            ApertureKey = MaybeKeys.ApertureKey || ApertureKey;
            FocusKey    = MaybeKeys.FocusKey || FocusKey;
            DofKey      = MaybeKeys.DofKey ?? null;
            Accent      = MaybeKeys.Accent || Accent;
            StartIndex  = MaybeKeys.StartIndex ?? 1;
            SensorLabel = MaybeKeys.SensorLabel || SensorLabel;
            OnOpticsChanged = MaybeKeys.OnOpticsChanged || null;
        }
    }

    const GradBase = `cam-grad-${++SvgInstanceCounter}`;
    const ReadOptics = () => ComputeCameraOptics(Target[FocalKey], Target[ApertureKey], Target[FocusKey]);
    const IsDofEnabled = () => (DofKey ? Target[DofKey] !== false : true);
    let RefreshDiagrams = () => {};

    // 1. Lens & Field of View Card (SensorField)
    const LensCard = Controls.Card(Body, "Lens & field of view", "Camera", { Accent, Index: StartIndex, Subtitle: "FULL-FRAME 3:2 OPTICAL SENSOR" });
    Controls.Metric(LensCard, () => Target[FocalKey], " mm", 0, () => {
        const Opt = ReadOptics();
        return [`${Opt.HorizontalFov.toFixed(1)}° horizontal`, "live"];
    });
    const SensorBox = document.createElement("div");
    SensorBox.className = "fe-camera-diagram fe-sensor-box";
    const SensorSvgWrap = document.createElement("div");
    SensorSvgWrap.className = "fe-camera-diagram-svg";
    SensorBox.appendChild(SensorSvgWrap);
    LensCard.appendChild(SensorBox);

    Controls.Slider(LensCard, Target, FocalKey, {
        Label: "Focal length", Minimum: 16, Maximum: 135, Step: 1, Unit: "mm", Digits: 0,
        Labels: ["Wide angle · 16 mm", "Telephoto · 135 mm"],
        OnInput: () => RefreshDiagrams()
    });
    Controls.Stats(LensCard, [
        ["Vertical FOV",   () => `${ReadOptics().VerticalFov.toFixed(1)}°`],
        ["Frame at focus", () => { const O = ReadOptics(); return `${O.FrameWidth.toFixed(1)}×${O.FrameHeight.toFixed(1)} m`; }],
        ["Entrance pupil", () => `${ReadOptics().Pupil.toFixed(1)} mm`]
    ], 3);

    // 2. Aperture Card (8-blade Iris)
    const ApertureCard = Controls.Card(Body, "Aperture", "Aperture", { Accent, Index: StartIndex + 1, Subtitle: "LIGHT INTAKE & DEPTH OF FIELD" });
    Controls.Metric(ApertureCard, () => `ƒ / ${Number(Target[ApertureKey]).toFixed(1)}`, "", 0);
    if (DofKey)
    {
        Controls.Switch(ApertureCard, Target, DofKey, {
            Label: "Enable depth of field",
            Detail: "Thin-lens circle of confusion 0.03 mm",
            OnChange: () => RefreshDiagrams()
        });
    }

    const IrisBox = document.createElement("div");
    IrisBox.className = "fe-camera-diagram fe-iris-box";
    const IrisSvgWrap = document.createElement("div");
    IrisSvgWrap.className = "fe-camera-diagram-svg";
    IrisBox.appendChild(IrisSvgWrap);
    ApertureCard.appendChild(IrisBox);

    Controls.Slider(ApertureCard, Target, ApertureKey, {
        Label: "f-stop", Minimum: 1.4, Maximum: 16.0, Step: 0.1, Unit: "", Digits: 1,
        Labels: ["Wide open · ƒ/1.4", "Stopped down · ƒ/16"],
        OnInput: () => RefreshDiagrams()
    });
    Controls.Stats(ApertureCard, [
        ["Pupil diameter", () => `${ReadOptics().Pupil.toFixed(1)} mm`],
        ["In-focus depth", () => (IsDofEnabled() ? FormatOpticDistance(ReadOptics().Depth) : "∞")]
    ], 2);

    // 3. Focus Distance Card (Draggable DepthOfField log-scale plane)
    const FocusCard = Controls.Card(Body, "Focus distance", "Scan", { Accent, Index: StartIndex + 2, Subtitle: "DRAG FOCAL PLANE OR SLIDER" });
    Controls.Metric(FocusCard, () => Target[FocusKey], " m", 1);

    const DofBox = document.createElement("div");
    DofBox.className = "fe-camera-diagram fe-camera-diagram-dof fe-dof-box";
    const DofSvgWrap = document.createElement("div");
    DofSvgWrap.className = "fe-camera-diagram-svg";
    DofBox.appendChild(DofSvgWrap);
    FocusCard.appendChild(DofBox);

    Controls.Slider(FocusCard, Target, FocusKey, {
        Label: "Subject distance", Minimum: 1.0, Maximum: 100.0, Step: 0.5, Unit: "m", Digits: 1,
        Labels: ["Near · 1 m", "Far · 100 m"],
        OnInput: () => RefreshDiagrams()
    });
    Controls.Stats(FocusCard, [
        ["Near limit", () => (IsDofEnabled() ? FormatOpticDistance(ReadOptics().Near) : "—")],
        ["Far limit",  () => (IsDofEnabled() ? FormatOpticDistance(ReadOptics().Far) : "—")],
        ["Hyperfocal", () => `${FormatOpticDistance(ReadOptics().Hyperfocal)}`]
    ], 3);

    RefreshDiagrams = () =>
    {
        const Opt = ReadOptics();
        SensorSvgWrap.innerHTML = RenderSensorFieldSvg(Target[FocalKey], Target[ApertureKey], Target[FocusKey], Opt, `${GradBase}-s`, SensorLabel);
        IrisSvgWrap.innerHTML   = RenderIrisSvg(Target[ApertureKey], `${GradBase}-i`);
        DofSvgWrap.innerHTML    = RenderDepthOfFieldSvg(Target[FocusKey], Opt, IsDofEnabled());
        if (OnOpticsChanged)
            OnOpticsChanged(Opt);
    };

    // Interactive pointer drag on DepthOfField SVG
    let DraggingDof = false;
    const ScrubDofFromEvent = (Event) =>
    {
        if (!DofBox.getBoundingClientRect)
            return;
        const Rect = DofBox.getBoundingClientRect();
        if (!Rect.width || !Rect.height)
            return;
        const Scale = Math.min(Rect.width / 360, Rect.height / 174);
        if (!Scale)
            return;
        const Px = (Event.clientX - Rect.left - (Rect.width - 360 * Scale) * 0.5) / Scale;
        const Dist = Math.max(1.0, Math.min(100.0, Math.expm1(((Px - 22) / 316) * Math.log1p(100))));
        Target[FocusKey] = Math.round(Dist * 2) / 2;
        Controls.Synchronise();
        RefreshDiagrams();
        Controls.Notify(FocusKey, Target);
    };

    DofBox.addEventListener("pointerdown", (Event) =>
    {
        DraggingDof = true;
        try { DofBox.setPointerCapture?.(Event.pointerId); } catch {}
        ScrubDofFromEvent(Event);
    });
    DofBox.addEventListener("pointermove", (Event) =>
    {
        if (DraggingDof)
            ScrubDofFromEvent(Event);
    });
    DofBox.addEventListener("pointerup", () => { DraggingDof = false; });
    DofBox.addEventListener("pointercancel", () => { DraggingDof = false; });

    RefreshDiagrams();
    Controls.Bindings.push(RefreshDiagrams);
    Controls.Live.push(RefreshDiagrams);
}
