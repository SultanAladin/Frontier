// Shared vehicle materials: the Garage, mirrors and world all use these records.
import { CourseMaterial, EncodeMaterialRecords, MaterialRecordBytes } from './MaterialSpecification.js';

// The colour names, order, hex values and metallic classifications come from the reference configurator.
// Its initial finish is gloss; a non-metallic colour is not automatically matte.
export const PaintOptions = Object.freeze([
    { Name: 'Nebula Silver', Hex: '#c2c8d0', Metallic: true,  Matte: false },
    { Name: 'Obsidian Black', Hex: '#17191d', Metallic: true,  Matte: false },
    { Name: 'Command Red', Hex: '#8a1a1d',    Metallic: true,  Matte: false },
    { Name: 'Tactical Blue', Hex: '#1c3f73',  Metallic: true,  Matte: false },
    { Name: 'Moab Sand', Hex: '#b2946a',      Metallic: false, Matte: false },
    { Name: 'Recon Green', Hex: '#33442f',    Metallic: false, Matte: false },
    { Name: 'Arctic White', Hex: '#e7ebef',   Metallic: true,  Matte: false },
    { Name: 'Copper Bronze', Hex: '#8c5a2e',  Metallic: true,  Matte: false }
]);
export const PaintColours = PaintOptions.map(Option => Option.Hex);
export function LinearColour(Hex) {
    if (!/^#[0-9a-f]{6}$/i.test(Hex)) throw new TypeError('Expected #RRGGBB');
    return [1, 3, 5].map(Start => {
        const Component = parseInt(Hex.slice(Start, Start + 2), 16) / 255;
        return Component <= 0.04045 ? Component / 12.92 : ((Component + 0.055) / 1.055) ** 2.4;
    });
}
export class VehicleAppearance {
    constructor(Materials, Upload, OnTransition = null) {
        this.Materials = Materials;
        this.Upload = Upload;
        this.OnTransition = OnTransition;
        this.Colour = null;
        this.Matte = false;
        this.Metallic = Materials[CourseMaterial.BodyCoat].Metalness >= 0.5;
        this.DarkWheels = false;
        this.BlueRims = true;
        this.View = 0; // solid, ghost, wireframe, animated scan; inspection only, never affects physics
    }
    Apply(Options = {}) {
        const PaintRequested = ['Colour', 'Matte', 'Metallic'].some(Field => Object.hasOwn(Options, Field));
        const WheelsRequested = ['DarkWheels', 'BlueRims'].some(Field => Object.hasOwn(Options, Field));
        const Colour = Options.Colour ?? this.Colour;
        const Matte = Options.Matte ?? this.Matte;
        const Swatch = Colour === null ? null : PaintOptions.find(Paint => Paint.Hex.toLowerCase() === Colour.toLowerCase());
        const Metallic = Options.Metallic ?? Swatch?.Metallic ?? this.Metallic;
        const Albedo = Colour === null ? null : LinearColour(Colour);
        if (PaintRequested) {
            const Body = this.Materials[CourseMaterial.BodyCoat];
            const PreviousBody = { ...Body, Albedo: [...Body.Albedo], Emission: [...(Body.Emission || [0, 0, 0])] };
            if (Albedo) Body.Albedo = Albedo;
            // Truck.tsx: roughness and metalness ripple; clearcoat switches only when that ripple finishes.
            Body.Roughness = Matte ? 0.72 : Metallic ? 0.26 : 0.45;
            Body.Metalness = Metallic ? 0.9 : 0.35;
            Body.CoatWeight = Matte ? 0.1 : 1.0;
            Body.CoatRoughness = Matte ? 0.65 : 0.12;
            this.Colour = Colour;
            this.Matte = Matte;
            this.Metallic = Metallic;
            this.Upload(CourseMaterial.BodyCoat * MaterialRecordBytes, EncodeMaterialRecords([Body]));
            if (this.OnTransition && !Options.SkipRipple)
                this.OnTransition(PreviousBody, Body, Options.RippleOrigin ?? null);
        }
        if (WheelsRequested) {
            const DarkWheels = Options.DarkWheels ?? this.DarkWheels;
            const BlueRims = Options.BlueRims ?? this.BlueRims;
            const Hub = this.Materials[CourseMaterial.Hub];
            Hub.Albedo = DarkWheels ? [0.025, 0.028, 0.032] : BlueRims ? [0.06, 0.22, 0.62] : [0.62, 0.63, 0.65];
            Hub.Emission = BlueRims && !DarkWheels ? [0, 0.03327756, 0.12] : [0, 0, 0];
            Hub.Metalness = 1;
            Hub.Roughness = DarkWheels ? 0.38 : 0.2;
            this.DarkWheels = DarkWheels;
            this.BlueRims = BlueRims;
            this.Upload(CourseMaterial.Hub * MaterialRecordBytes, EncodeMaterialRecords([Hub]));
        }
    }
}
