/*==========================================================================================================================================
                                                        ENGINEERINGMATCAPS.JS
==========================================================================================================================================*/
// Procedural CAD / Industrial Design / Precision Machining MatCap Synthesizer:
//   - Synthesizes 8 Engineering spheres from exact view-reflection vectors R = reflect((0,0,-1), N)
//   - Industrial studio light-tunnel dome, dual overhead LED softboxes, crisp shop-floor horizon split, CNC lathe micro-grooves,
//     metallic Schlick grazing Fresnel, Rhino/Plasticity technical drafting clay, and anti-aliased Class-A Zebra continuity bands.

export const MATCAP_STUDIO_CATALOG = [
    {
        slot: 0, id: 'cnc-alu', label: 'CNC Aluminum', short: 'CNC Alu',
        kind: 'brushed-metal',
        skyTop: [214, 222, 232], skyHorizon: [148, 158, 170], groundDark: [38, 43, 50], groundBounce: [92, 100, 112],
        stripColor: [252, 254, 255], rimColor: [216, 228, 242],
        stripSharpness: 18.0, anisoX: 0.42, anisoY: 0.16, brushAmp: 0.055, fresnelGain: 0.38, horizonSharp: 11.0
    },
    {
        slot: 1, id: 'ti-grade5', label: 'Grade-5 Titanium', short: 'Titanium',
        kind: 'brushed-metal',
        skyTop: [192, 188, 182], skyHorizon: [122, 119, 116], groundDark: [28, 28, 31], groundBounce: [76, 75, 78],
        stripColor: [248, 244, 236], rimColor: [188, 198, 212],
        stripSharpness: 24.0, anisoX: 0.36, anisoY: 0.13, brushAmp: 0.045, fresnelGain: 0.44, horizonSharp: 14.0
    },
    {
        slot: 2, id: 'cad-clay', label: 'CAD Clay', short: 'CAD Clay',
        kind: 'cad-clay',
        skyTop: [204, 200, 194], skyHorizon: [146, 143, 138], groundDark: [68, 66, 65], groundBounce: [112, 108, 104],
        stripColor: [236, 233, 228], rimColor: [164, 174, 186],
        stripSharpness: 5.5, anisoX: 0.55, anisoY: 0.45, brushAmp: 0.0, fresnelGain: 0.14, horizonSharp: 3.5
    },
    {
        slot: 3, id: 'studio-chrome', label: 'Class-A Chrome', short: 'Chrome',
        kind: 'chrome',
        skyTop: [228, 236, 248], skyHorizon: [176, 192, 212], groundDark: [14, 17, 22], groundBounce: [68, 76, 88],
        stripColor: [255, 255, 255], rimColor: [226, 238, 255],
        stripSharpness: 52.0, anisoX: 0.28, anisoY: 0.075, brushAmp: 0.0, fresnelGain: 0.62, horizonSharp: 34.0
    },
    {
        slot: 4, id: 'gunmetal-ano', label: 'Gunmetal Ano', short: 'Gunmetal',
        kind: 'anodized',
        skyTop: [86, 96, 110], skyHorizon: [48, 54, 64], groundDark: [15, 18, 23], groundBounce: [36, 42, 50],
        stripColor: [208, 224, 242], rimColor: [124, 152, 184],
        stripSharpness: 28.0, anisoX: 0.34, anisoY: 0.12, brushAmp: 0.025, fresnelGain: 0.46, horizonSharp: 16.0
    },
    {
        slot: 5, id: 'layout-blue', label: 'Steel Blue', short: 'Steel Blue',
        kind: 'anodized',
        skyTop: [82, 126, 176], skyHorizon: [42, 74, 114], groundDark: [14, 24, 38], groundBounce: [34, 56, 86],
        stripColor: [226, 240, 255], rimColor: [132, 188, 244],
        stripSharpness: 30.0, anisoX: 0.32, anisoY: 0.11, brushAmp: 0.02, fresnelGain: 0.48, horizonSharp: 15.0
    },
    {
        slot: 6, id: 'cnc-brass', label: 'Turned Brass', short: 'Brass',
        kind: 'brushed-metal',
        skyTop: [212, 184, 126], skyHorizon: [142, 116, 68], groundDark: [36, 27, 15], groundBounce: [92, 72, 42],
        stripColor: [255, 244, 214], rimColor: [236, 206, 146],
        stripSharpness: 26.0, anisoX: 0.38, anisoY: 0.12, brushAmp: 0.05, fresnelGain: 0.42, horizonSharp: 16.0
    },
    {
        slot: 7, id: 'cad-zebra', label: 'Zebra CAD', short: 'Zebra',
        kind: 'zebra',
        skyTop: [236, 240, 246], skyHorizon: [188, 196, 206], groundDark: [22, 26, 32], groundBounce: [52, 58, 68],
        stripColor: [255, 255, 255], rimColor: [204, 220, 240],
        stripSharpness: 32.0, anisoX: 0.30, anisoY: 0.10, brushAmp: 0.0, fresnelGain: 0.30, horizonSharp: 18.0
    }
];

function SmoothStep01(Edge0, Edge1, X)
{
    const T = Math.max(0, Math.min(1, (X - Edge0) / (Edge1 - Edge0)));
    return T * T * (3 - 2 * T);
}

export function SynthesizeMatcapPixels(Spec, CellSize, TargetBuffer = null)
{
    const Data = TargetBuffer || new Uint8ClampedArray(CellSize * CellSize * 4);
    const Kx = -0.36, Ky = 0.64, Kz = 0.677;
    const Fx = 0.58, Fy = -0.28, Fz = 0.762;

    for (let y = 0; y < CellSize; y++)
    {
        const Ny = 1 - (2 * (y + 0.5)) / CellSize;
        for (let x = 0; x < CellSize; x++)
        {
            const Nx = (2 * (x + 0.5)) / CellSize - 1;
            const R2 = Nx * Nx + Ny * Ny;
            const ClampR2 = Math.min(1.0, R2);
            const Nz = Math.sqrt(Math.max(0.0, 1.0 - ClampR2));
            const Rn = R2 > 1.0 ? 1.0 / Math.sqrt(R2) : 1.0;
            const Sx = Nx * Rn, Sy = Ny * Rn;
            const idx = (y * CellSize + x) * 4;

            // Exact view reflection vector R = reflect((0,0,-1), N) = (2*Nx*Nz, 2*Ny*Nz, 2*Nz^2 - 1)
            const Rx = 2.0 * Sx * Nz;
            const Ry = 2.0 * Sy * Nz;

            if (Spec.kind === 'zebra')
            {
                const Coord = (Rx * 0.76 + Ry * 0.65) * 6.5 + Math.atan2(Sy, Sx) * 0.18;
                const Wave = Math.sin(Coord * Math.PI);
                const Band = SmoothStep01(-0.14, 0.14, Wave);
                const Form = 0.72 + 0.28 * Math.max(0, Sx * Kx + Sy * Ky + Nz * Kz);
                const Rim = Math.pow(1.0 - Nz, 3.4) * 0.28;
                const DarkR = 24, DarkG = 28, DarkB = 34;
                const BrightR = 242, BrightG = 246, BrightB = 252;
                const EdgeContour = SmoothStep01(0.0, 0.14, Nz) * 0.18 + 0.82;
                Data[idx]     = Math.min(255, ((DarkR + (BrightR - DarkR) * Band) * Form + Spec.rimColor[0] * Rim) * EdgeContour);
                Data[idx + 1] = Math.min(255, ((DarkG + (BrightG - DarkG) * Band) * Form + Spec.rimColor[1] * Rim) * EdgeContour);
                Data[idx + 2] = Math.min(255, ((DarkB + (BrightB - DarkB) * Band) * Form + Spec.rimColor[2] * Rim) * EdgeContour);
                Data[idx + 3] = 255;
                continue;
            }

            if (Spec.kind === 'cad-clay')
            {
                const KeyWrap = Math.max(0, Math.min(1, (Sx * Kx + Sy * Ky + Nz * Kz + 0.48) / 1.48));
                const SkyHemi = Sy * 0.5 + 0.5;
                const Bounce = Math.max(0, Sx * Fx + Sy * Fy + Nz * Fz) * 0.22;
                const SoftSpec = Math.pow(Math.max(0, Sx * -0.22 + Sy * 0.44 + Nz * 0.87), 14.0) * 0.16;
                const Silhouette = 0.74 + 0.26 * SmoothStep01(0.0, 0.28, Nz);
                const Blend = KeyWrap * 0.72 + SkyHemi * 0.28;
                Data[idx]     = Math.min(255, (Spec.groundDark[0] + (Spec.skyTop[0] - Spec.groundDark[0]) * Blend + Spec.groundBounce[0] * Bounce + 255 * SoftSpec) * Silhouette);
                Data[idx + 1] = Math.min(255, (Spec.groundDark[1] + (Spec.skyTop[1] - Spec.groundDark[1]) * Blend + Spec.groundBounce[1] * Bounce + 252 * SoftSpec) * Silhouette);
                Data[idx + 2] = Math.min(255, (Spec.groundDark[2] + (Spec.skyTop[2] - Spec.groundDark[2]) * Blend + Spec.groundBounce[2] * Bounce + 248 * SoftSpec) * Silhouette);
                Data[idx + 3] = 255;
                continue;
            }

            const HorizonT = SmoothStep01(-1.0 / Spec.horizonSharp - 0.05, 1.0 / Spec.horizonSharp - 0.05, Ry);
            const SkyGrad = SmoothStep01(-0.05, 0.95, Ry);
            const FloorBounce = SmoothStep01(-0.18, -0.88, Ry);

            const EnvR = HorizonT * (Spec.skyHorizon[0] + (Spec.skyTop[0] - Spec.skyHorizon[0]) * SkyGrad)
                       + (1.0 - HorizonT) * (Spec.groundDark[0] + (Spec.groundBounce[0] - Spec.groundDark[0]) * FloorBounce);
            const EnvG = HorizonT * (Spec.skyHorizon[1] + (Spec.skyTop[1] - Spec.skyHorizon[1]) * SkyGrad)
                       + (1.0 - HorizonT) * (Spec.groundDark[1] + (Spec.groundBounce[1] - Spec.groundDark[1]) * FloorBounce);
            const EnvB = HorizonT * (Spec.skyHorizon[2] + (Spec.skyTop[2] - Spec.skyHorizon[2]) * SkyGrad)
                       + (1.0 - HorizonT) * (Spec.groundDark[2] + (Spec.groundBounce[2] - Spec.groundDark[2]) * FloorBounce);

            const U1 = (Rx * 0.93 + (Ry - 0.50) * 0.36) / Spec.anisoX;
            const V1 = (-Rx * 0.36 + (Ry - 0.50) * 0.93) / Spec.anisoY;
            const Strip1 = Math.exp(-0.5 * (Math.pow(Math.abs(U1), 2.8) + Math.pow(Math.abs(V1), 2.4)));

            const U2 = ((Rx - 0.46) * 0.86 - (Ry - 0.18) * 0.51) / (Spec.anisoX * 0.72);
            const V2 = ((Rx - 0.46) * 0.51 + (Ry - 0.18) * 0.86) / (Spec.anisoY * 0.95);
            const Strip2 = Math.exp(-0.5 * (Math.pow(Math.abs(U2), 2.6) + Math.pow(Math.abs(V2), 2.4))) * 0.58;

            let BrushMod = 1.0;
            if (Spec.brushAmp > 0.0)
            {
                const Radius = Math.sqrt(ClampR2);
                const Angle = Math.atan2(Sy, Sx);
                const FineRings = Math.sin(Radius * 68.0 + Math.sin(Angle * 4.0) * 1.4) * 0.5
                                + Math.sin(Angle * 36.0 + Radius * 12.0) * 0.5;
                BrushMod = 1.0 + FineRings * Spec.brushAmp * (0.35 + 0.65 * Radius);
            }

            const Fresnel = Math.pow(1.0 - Nz, 4.0) * Spec.fresnelGain;
            const CadRimDark = 0.80 + 0.20 * SmoothStep01(0.0, 0.13, Nz);
            const StripSum = (Strip1 * 0.88 + Strip2) * (Spec.kind === 'chrome' ? 1.18 : 0.78);

            const OutR = (EnvR * BrushMod + Spec.stripColor[0] * StripSum + Spec.rimColor[0] * Fresnel) * CadRimDark;
            const OutG = (EnvG * BrushMod + Spec.stripColor[1] * StripSum + Spec.rimColor[1] * Fresnel) * CadRimDark;
            const OutB = (EnvB * BrushMod + Spec.stripColor[2] * StripSum + Spec.rimColor[2] * Fresnel) * CadRimDark;

            Data[idx]     = Math.max(0, Math.min(255, OutR));
            Data[idx + 1] = Math.max(0, Math.min(255, OutG));
            Data[idx + 2] = Math.max(0, Math.min(255, OutB));
            Data[idx + 3] = 255;
        }
    }
    return Data;
}

export function RenderMatcapSphereToImageData(Ctx, Spec, CellSize, OffsetX = 0, OffsetY = 0)
{
    const Img = Ctx.createImageData(CellSize, CellSize);
    SynthesizeMatcapPixels(Spec, CellSize, Img.data);
    Ctx.putImageData(Img, OffsetX, OffsetY);
}
