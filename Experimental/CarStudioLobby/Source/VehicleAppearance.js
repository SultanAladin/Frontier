//============================================================================================================================================
//                                                          VEHICLEAPPEARANCE.JS
//============================================================================================================================================
// 🧩 Live GPU material configurator for the Car Studio Lobby: body paint, finish, rims, calipers, headlights, glass, and studio presets.

import { CourseMaterial, EncodeMaterialRecords } from './MaterialSpecification.js';

export const PaintColours = Object.freeze([
    { Name: 'Slate Grey',     Css: '#585f68', Albedo: [0.095, 0.110, 0.130], Tag: 'Ref 2 · Carrera Grey' },
    { Name: 'Obsidian Black', Css: '#111317', Albedo: [0.007, 0.008, 0.010], Tag: 'Ref 1 · GT3 RS Black' },
    { Name: 'Guards Red',     Css: '#c81d18', Albedo: [0.580, 0.018, 0.015], Tag: 'Factory Weissach' },
    { Name: 'Chalk White',    Css: '#e4e6ea', Albedo: [0.790, 0.805, 0.830], Tag: 'Crayon Monochrome' },
    { Name: 'Miami Blue',     Css: '#1888b8', Albedo: [0.025, 0.240, 0.480], Tag: 'PTS Riviera' },
    { Name: 'Solar Yellow',   Css: '#ebae14', Albedo: [0.840, 0.440, 0.015], Tag: 'Racing Yellow' },
    { Name: 'Racing Green',   Css: '#14442a', Albedo: [0.012, 0.068, 0.028], Tag: 'British Heritage' },
    { Name: 'Viola Metallic', Css: '#4c2668', Albedo: [0.085, 0.022, 0.165], Tag: 'Ultraviolet PTS' },
    { Name: 'Signal Green',   Css: '#28b446', Albedo: [0.032, 0.440, 0.065], Tag: 'RS Track Spec' },
    { Name: 'GT Silver',      Css: '#9aa0a8', Albedo: [0.360, 0.385, 0.420], Tag: 'Liquid Metallic' }
]);

export const FinishPresets = Object.freeze([
    { Id: 'gloss',    Name: 'Mirror Gloss',   Roughness: 0.065, Metalness: 0.15, CoatWeight: 1.00, CoatRoughness: 0.018 },
    { Id: 'metallic', Name: 'Metallic Flake', Roughness: 0.095, Metalness: 0.45, CoatWeight: 1.00, CoatRoughness: 0.028 },
    { Id: 'satin',    Name: 'Satin Pearl',    Roughness: 0.240, Metalness: 0.35, CoatWeight: 0.45, CoatRoughness: 0.140 },
    { Id: 'matte',    Name: 'Stealth Matte',  Roughness: 0.460, Metalness: 0.10, CoatWeight: 0.05, CoatRoughness: 0.320 }
]);

export const RimPresets = Object.freeze([
    { Id: 'satin_black', Name: 'Satin Black',       Css: '#1d1f24', Albedo: [0.024, 0.025, 0.028], Roughness: 0.20, Metalness: 0.88 },
    { Id: 'weissach_au', Name: 'Weissach Gold',     Css: '#bfa15a', Albedo: [0.540, 0.380, 0.140], Roughness: 0.16, Metalness: 0.92 },
    { Id: 'silver',      Name: 'Forged Silver',     Css: '#c8cdd4', Albedo: [0.640, 0.660, 0.700], Roughness: 0.12, Metalness: 0.94 },
    { Id: 'gunmetal',    Name: 'Titanium Gunmetal', Css: '#525760', Albedo: [0.110, 0.120, 0.138], Roughness: 0.18, Metalness: 0.90 },
    { Id: 'mag_blue',    Name: 'Magnesium Indigo',  Css: '#25427a', Albedo: [0.032, 0.085, 0.260], Roughness: 0.17, Metalness: 0.88 }
]);

export const CaliperPresets = Object.freeze([
    { Id: 'red',    Name: 'Guards Red',   Css: '#d91c1c', Albedo: [0.680, 0.025, 0.025] },
    { Id: 'yellow', Name: 'Speed Yellow', Css: '#f0b818', Albedo: [0.850, 0.480, 0.018] },
    { Id: 'black',  Name: 'Stealth Black',Css: '#202328', Albedo: [0.028, 0.030, 0.035] },
    { Id: 'acid',   Name: 'Acid Green',   Css: '#88d618', Albedo: [0.280, 0.680, 0.018] },
    { Id: 'cobalt', Name: 'Cobalt Blue',  Css: '#1e5ce6', Albedo: [0.025, 0.140, 0.720] }
]);

export const HeadlightOpticsPresets = Object.freeze([
    { Id: 'amber', Name: 'GT Endurance Amber', Css: '#ffae19', Albedo: [1.00, 0.72, 0.14], Emission: [38.0, 20.0, 2.2], BeamColour: [1.00, 0.72, 0.16] },
    { Id: 'xenon', Name: 'Pure Xenon 6000K',   Css: '#eef4ff', Albedo: [0.94, 0.97, 1.00], Emission: [26.0, 28.0, 32.0], BeamColour: [0.94, 0.96, 1.00] },
    { Id: 'laser', Name: 'Laser Ice Blue',     Css: '#62b6ff', Albedo: [0.52, 0.82, 1.00], Emission: [14.0, 26.0, 42.0], BeamColour: [0.55, 0.82, 1.00] }
]);

export const StudioEnvironmentPresets = Object.freeze({
    softbox: {
        Id:          'softbox',
        Name:        'Industrial Softbox Loft (Ref 2)',
        Description: 'Exposed dark steel truss ceiling with giant suspended rectangular LED softbox over an angled white stage pad and dark concrete surround.',
        FloorPad:    { Albedo: [0.86, 0.87, 0.89], Roughness: 0.18, Metalness: 0.00, CoatWeight: 0.55, CoatRoughness: 0.09, SpecularWeight: 0.90, Emission: [0.0, 0.0, 0.0] },
        FloorDark:   { Albedo: [0.026, 0.028, 0.033], Roughness: 0.22, Metalness: 0.00, CoatWeight: 0.40, CoatRoughness: 0.12, SpecularWeight: 0.85, Emission: [0.0, 0.0, 0.0] },
        WallChalk:   { Albedo: [0.82, 0.83, 0.85], Roughness: 0.42, Metalness: 0.00, CoatWeight: 0.00, CoatRoughness: 0.04, SpecularWeight: 0.50, Emission: [0.18, 0.19, 0.20] },
        SoftboxFace: { Albedo: [0.98, 0.99, 1.00], Roughness: 0.14, Metalness: 0.00, CoatWeight: 0.00, CoatRoughness: 0.04, SpecularWeight: 0.50, Emission: [28.0, 28.5, 30.5] },
        TrussDark:   { Albedo: [0.016, 0.018, 0.022], Roughness: 0.34, Metalness: 0.72, CoatWeight: 0.00, CoatRoughness: 0.04, SpecularWeight: 0.65, Emission: [0.0, 0.0, 0.0] },
        Ring:        { Albedo: [0.80, 0.81, 0.83], Roughness: 0.22, Metalness: 0.00, CoatWeight: 0.30, CoatRoughness: 0.12, SpecularWeight: 0.80, Emission: [0.0, 0.0, 0.0] }
    },
    cyclorama: {
        Id:          'cyclorama',
        Name:        'White Infinity Cyclorama (Ref 1)',
        Description: 'Seamless pure-white curved cyclorama with high-gloss reflective turntable floor, circular turntable ring, and diffused canopy.',
        FloorPad:    { Albedo: [0.90, 0.91, 0.93], Roughness: 0.032, Metalness: 0.00, CoatWeight: 1.00, CoatRoughness: 0.014, SpecularWeight: 1.00, Emission: [0.08, 0.085, 0.09] },
        FloorDark:   { Albedo: [0.90, 0.91, 0.93], Roughness: 0.032, Metalness: 0.00, CoatWeight: 1.00, CoatRoughness: 0.014, SpecularWeight: 1.00, Emission: [0.08, 0.085, 0.09] },
        WallChalk:   { Albedo: [0.94, 0.95, 0.97], Roughness: 0.28, Metalness: 0.00, CoatWeight: 0.00, CoatRoughness: 0.04, SpecularWeight: 0.50, Emission: [1.65, 1.70, 1.80] },
        SoftboxFace: { Albedo: [0.99, 0.99, 1.00], Roughness: 0.12, Metalness: 0.00, CoatWeight: 0.00, CoatRoughness: 0.04, SpecularWeight: 0.50, Emission: [22.0, 22.5, 24.0] },
        TrussDark:   { Albedo: [0.92, 0.93, 0.95], Roughness: 0.30, Metalness: 0.00, CoatWeight: 0.00, CoatRoughness: 0.04, SpecularWeight: 0.50, Emission: [2.10, 2.15, 2.28] },
        Ring:        { Albedo: [0.15, 0.16, 0.18], Roughness: 0.22, Metalness: 0.45, CoatWeight: 0.00, CoatRoughness: 0.04, SpecularWeight: 0.65, Emission: [0.0, 0.0, 0.0] }
    }
});

function HexToLinearRgb(Hex)
{
    const Clean = String(Hex || '#585f68').replace('#', '');
    const Num = parseInt(Clean.length === 6 ? Clean : '585f68', 16);
    const Srgb = [(Num >> 16) & 255, (Num >> 8) & 255, Num & 255].map((V) => V / 255.0);
    return Srgb.map((C) => (C <= 0.04045 ? C / 12.92 : Math.pow((C + 0.055) / 1.055, 2.4)));
}

export class VehicleAppearance
{
    constructor(Materials, WriteMaterialData, OnStudioChanged = null)
    {
        this.Materials         = Materials;
        this.WriteMaterialData = WriteMaterialData;
        this.OnStudioChanged   = OnStudioChanged;

        this.Index             = 0;
        this.FinishId          = 'metallic';
        this.RimId             = 'satin_black';
        this.CaliperId         = 'red';
        this.OpticsId          = 'amber';
        this.WindowSmoke       = false;
        this.StudioPresetId    = 'softbox';
    }

    UploadMaterialFamily(Family)
    {
        const Encoded = EncodeMaterialRecords(this.Materials);
        const First = Family * 12;
        this.WriteMaterialData?.(First * 4, Encoded.subarray(First, First + 12));
    }

    ApplyCoat(Entry, Roughness = null)
    {
        const Coat = this.Materials[CourseMaterial.BodyCoat];
        if (!Coat)
            return;
        if (Entry?.Albedo)
            Coat.Albedo = [...Entry.Albedo];
        if (Roughness !== null)
            Coat.Roughness = Roughness;
        this.UploadMaterialFamily(CourseMaterial.BodyCoat);
    }

    SelectIndex(Index)
    {
        this.Index = ((Index % PaintColours.length) + PaintColours.length) % PaintColours.length;
        const Finish = FinishPresets.find((F) => F.Id === this.FinishId) || FinishPresets[1];
        const Coat = this.Materials[CourseMaterial.BodyCoat];
        if (Coat)
        {
            Coat.Albedo        = [...PaintColours[this.Index].Albedo];
            Coat.Roughness     = Finish.Roughness;
            Coat.Metalness     = Finish.Metalness;
            Coat.CoatWeight    = Finish.CoatWeight;
            Coat.CoatRoughness = Finish.CoatRoughness;
            this.UploadMaterialFamily(CourseMaterial.BodyCoat);
        }
        return this.Index;
    }

    SelectCustomHex(Hex)
    {
        const Coat = this.Materials[CourseMaterial.BodyCoat];
        if (!Coat)
            return;
        Coat.Albedo = HexToLinearRgb(Hex);
        this.UploadMaterialFamily(CourseMaterial.BodyCoat);
    }

    SelectFinish(FinishId)
    {
        const Preset = FinishPresets.find((F) => F.Id === FinishId) || FinishPresets[0];
        this.FinishId = Preset.Id;
        const Coat = this.Materials[CourseMaterial.BodyCoat];
        if (Coat)
        {
            Coat.Roughness     = Preset.Roughness;
            Coat.Metalness     = Preset.Metalness;
            Coat.CoatWeight    = Preset.CoatWeight;
            Coat.CoatRoughness = Preset.CoatRoughness;
            this.UploadMaterialFamily(CourseMaterial.BodyCoat);
        }
        return Preset;
    }

    SelectRim(RimId)
    {
        const Preset = RimPresets.find((R) => R.Id === RimId) || RimPresets[0];
        this.RimId = Preset.Id;
        for (const Family of [CourseMaterial.RimBase, CourseMaterial.Hub])
        {
            const Mat = this.Materials[Family];
            if (Mat)
            {
                Mat.Albedo    = [...Preset.Albedo];
                Mat.Roughness = Preset.Roughness;
                Mat.Metalness = Preset.Metalness;
                this.UploadMaterialFamily(Family);
            }
        }
        return Preset;
    }

    SelectCaliper(CaliperId)
    {
        const Preset = CaliperPresets.find((C) => C.Id === CaliperId) || CaliperPresets[0];
        this.CaliperId = Preset.Id;
        const Mat = this.Materials[CourseMaterial.Brake];
        if (Mat)
        {
            Mat.Albedo = [...Preset.Albedo];
            this.UploadMaterialFamily(CourseMaterial.Brake);
        }
        return Preset;
    }

    SelectHeadlightOptics(OpticsId)
    {
        const Preset = HeadlightOpticsPresets.find((O) => O.Id === OpticsId) || HeadlightOpticsPresets[0];
        this.OpticsId = Preset.Id;
        const Mat = this.Materials[CourseMaterial.Headlight];
        if (Mat)
        {
            Mat.Albedo   = [...Preset.Albedo];
            Mat.Emission = [...Preset.Emission];
            this.UploadMaterialFamily(CourseMaterial.Headlight);
        }
        return Preset;
    }

    SetWindowSmoke(Smoked)
    {
        this.WindowSmoke = Boolean(Smoked);
        const Glass = this.Materials[CourseMaterial.VehicleGlass];
        if (Glass)
        {
            Glass.Albedo    = this.WindowSmoke ? [0.008, 0.009, 0.011] : [0.026, 0.028, 0.030];
            Glass.Roughness = this.WindowSmoke ? 0.015 : 0.02;
            this.UploadMaterialFamily(CourseMaterial.VehicleGlass);
        }
    }

    SelectStudioEnvironment(PresetId)
    {
        const Preset = StudioEnvironmentPresets[PresetId] || StudioEnvironmentPresets.softbox;
        this.StudioPresetId = Preset.Id;
        const Assign = (Family, Spec) =>
        {
            const Mat = this.Materials[Family];
            if (!Mat || !Spec)
                return;
            Mat.Albedo        = [...Spec.Albedo];
            Mat.Roughness     = Spec.Roughness;
            Mat.Metalness     = Spec.Metalness;
            Mat.CoatWeight    = Spec.CoatWeight;
            Mat.CoatRoughness = Spec.CoatRoughness;
            Mat.SpecularLevel = 0.04 * (Spec.SpecularWeight ?? 1.0);
            Mat.Emission      = [...Spec.Emission];
            this.UploadMaterialFamily(Family);
        };
        Assign(CourseMaterial.StudioFloorPad,      Preset.FloorPad);
        Assign(CourseMaterial.StudioFloorDark,     Preset.FloorDark);
        Assign(CourseMaterial.StudioWallChalk,     Preset.WallChalk);
        Assign(CourseMaterial.StudioSoftboxFace,   Preset.SoftboxFace);
        Assign(CourseMaterial.StudioTrussDark,     Preset.TrussDark);
        Assign(CourseMaterial.StudioTurntableRing, Preset.Ring);
        this.OnStudioChanged?.(Preset);
        return Preset;
    }
}
