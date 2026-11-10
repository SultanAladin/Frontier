// Shared vehicle materials: the Garage, mirrors and world all use these records.
import { CourseMaterial, EncodeMaterialRecords, MaterialRecordBytes } from './MaterialSpecification.js';
// 📝 The paint shelf, from the reference configurator (interactive-vehicle-hmi-configurator):
//    name + finish. The HMI's only vehicle control for now — everything else about the car is fixed.
// 📝 The eight finishes exactly as the reference configurator's data/config.ts lists them,
//    metallic flag included (it drives the ripple's roughness/metalness targets).
export const PaintOptions = Object.freeze([
    { Name: 'Nebula Silver', Hex: '#c2c8d0', Metallic: true },  { Name: 'Obsidian Black', Hex: '#17191d', Metallic: true },
    { Name: 'Command Red', Hex: '#8a1a1d', Metallic: true },    { Name: 'Tactical Blue', Hex: '#1c3f73', Metallic: true },
    { Name: 'Moab Sand', Hex: '#b2946a', Metallic: false },     { Name: 'Recon Green', Hex: '#33442f', Metallic: false },
    { Name: 'Arctic White', Hex: '#e7ebef', Metallic: true },   { Name: 'Copper Bronze', Hex: '#8c5a2e', Metallic: true }
]);

export const SurfaceTextures = Object.freeze([
    { Id: 0, Name: 'Smooth Clearcoat', Short: 'Smooth' },
    { Id: 1, Name: 'Carbon Twill',     Short: 'Carbon' },
    { Id: 2, Name: 'Brushed Grain',    Short: 'Brushed' },
    { Id: 3, Name: 'Hex Ceramic',      Short: 'Hex Ceramic' }
]);

// 📝 The reference shader's coverage for one surface point (Truck.tsx): 1 well inside the expanding
//    sphere, 0 outside it, smoothstepped across the trailing 0.55 m of the radius. Pure, so the unit
//    tests and the C++ PaintRipple can be checked against the same numbers.
export function ResolveRippleCoverage(Distance, Radius) {
    const T = Math.max(0, Math.min(1, (Distance - (Radius - 0.55)) / 0.55));
    return 1 - T * T * (3 - 2 * T);
}
export const PaintColours = PaintOptions.map(Option => Option.Hex);
export function LinearColour(Hex) {
    if (!/^#[0-9a-f]{6}$/i.test(Hex)) throw new TypeError('Expected #RRGGBB');
    return [1, 3, 5].map(Start => {
        const Value = parseInt(Hex.slice(Start, Start + 2), 16) / 255;
        return Value <= 0.04045 ? Value / 12.92 : ((Value + 0.055) / 1.055) ** 2.4;
    });
}
export class VehicleAppearance {
    constructor(Materials, Upload) {
        this.Materials = Materials;
        this.Upload = Upload;
        this.Colour = null;
        this.Matte = false;
        // 📝 Paint application is a ripple, not a swap (the reference configurator's beginRun/stepRun/
        //    finishRun): an expanding sphere from the touch point carries the next finish across the
        //    body over 1.35 s out to 6.4 m; only when it completes does Apply commit the material.
        this.Ripple = { Active: false, Centre: [0, 0, 0], NextColour: [1, 1, 1], Radius: 0,
                        Elapsed: 0, Duration: 1.35, MaximumRadius: 6.4,
                        Colour: null, Matte: false, Roughness: 0.0775, Metalness: 0.0, CoatWeight: 1.0 };
        this.DarkWheels = false;
        this.BlueRims = true;
        this.SurfaceTexture = 0;   // 0 Smooth, 1 Carbon Twill, 2 Brushed Grain, 3 Hex Ceramic
        this.View = 0; // solid, ghost, wireframe, animated scan; inspection only, never affects physics
    }
    AssignSurfaceTexture(TextureId) {
        const Clamped = Math.max(0, Math.min(3, Math.round(Number(TextureId) || 0)));
        this.SurfaceTexture = Clamped;
        return Clamped;
    }
    // 📝 Starts the sphere at a body-local touch point (the drop's raycast hit, or the nose default the
    //    caller resolves). A ripple already in flight commits instantly first, as finishRun does.
    BeginPaintRipple(Colour, Centre, { Matte = false, Metallic = true } = {}) {
        if (this.Ripple.Active) this.Apply({ Colour: this.Ripple.Colour, Matte: this.Ripple.Matte });
        const Option = PaintOptions.find(Paint => Paint.Hex === Colour);
        const IsMetallic = Option ? Option.Metallic : Metallic;
        Object.assign(this.Ripple, { Active: true, Centre: Centre ? [...Centre] : [0, 0, 0],
            NextColour: LinearColour(Colour), Radius: 0, Elapsed: 0, Colour, Matte,
            Roughness: Matte ? 0.7 : 0.0775, Metalness: IsMetallic ? 0.9 : 0.0, CoatWeight: Matte ? 0.0 : 1.0 });
    }
    // 📝 Advances the sphere (easeOutCubic); returns true on the step that completes and commits it.
    AdvancePaintRipple(Seconds) {
        const Ripple = this.Ripple;
        if (!Ripple.Active) return false;
        Ripple.Elapsed += Seconds;
        const K = Math.min(Ripple.Elapsed / Ripple.Duration, 1);
        Ripple.Radius = (1 - (1 - K) ** 3) * Ripple.MaximumRadius;
        if (K >= 1) { Ripple.Active = false; this.Apply({ Colour: Ripple.Colour, Matte: Ripple.Matte }); return true; }
        return false;
    }
    // 📝 Coverage of one body-local point right now (0 when no ripple is running).
    ResolvePaintCoverage(Point) {
        if (!this.Ripple.Active) return 0;
        const Distance = Math.hypot(Point[0] - this.Ripple.Centre[0], Point[1] - this.Ripple.Centre[1], Point[2] - this.Ripple.Centre[2]);
        return ResolveRippleCoverage(Distance, this.Ripple.Radius);
    }
    Apply({ Colour = this.Colour, Matte = this.Matte, DarkWheels = this.DarkWheels, BlueRims = this.BlueRims } = {}) {
        const Albedo = Colour === null ? null : LinearColour(Colour);
        const Body = this.Materials[CourseMaterial.BodyCoat];
        if (Albedo) Body.Albedo = Albedo;
        Body.Roughness = Matte ? 0.7 : 0.0775;
        Body.CoatWeight = Matte ? 0 : 1;
        const Hub = this.Materials[CourseMaterial.Hub];
        Hub.Albedo = DarkWheels ? [0.025, 0.028, 0.032] : BlueRims ? [0.06, 0.22, 0.62] : [0.62, 0.63, 0.65];
        Hub.Emission = BlueRims && !DarkWheels ? [0, 0.03327756, 0.12] : [0,0,0];
        this.BlueRims = BlueRims;
        Hub.Metalness = 1;
        Hub.Roughness = DarkWheels ? 0.38 : 0.2;
        this.Colour = Colour; this.Matte = Matte; this.DarkWheels = DarkWheels;
        for (const Family of [CourseMaterial.BodyCoat, CourseMaterial.Hub])
            this.Upload(Family * MaterialRecordBytes, EncodeMaterialRecords([this.Materials[Family]]));
    }
}
