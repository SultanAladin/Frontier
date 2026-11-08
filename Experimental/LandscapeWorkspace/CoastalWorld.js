/*==========================================================================================================================================
                                                               COASTALWORLD.JS
==========================================================================================================================================*/
// 🧩 Shared canonical coastal document for editor startup and reproducible XYZ surface measurements.

import { CreateLandform } from './LandformStack.js';

const Palette = ['#b9a481', '#8e9890', '#b3c5cc', '#b38e76'];
export const CreateLayer = Index => ({
    Name: ['Desert coast', 'Rocky isles', 'Glacial shelf'][Index] || `Mask ${String(Index + 1).padStart(2, '0')}`,
    Color: Palette[Index % Palette.length], Visible: true, Depth: 120, Caves: false,
    Resolution: 1024, Vertical: 128, DistanceMode: 'metric', Landforms: [], Strokes: [], Regions: []
});

export function CreateCoastalWorld()
{
    const Layers = [CreateLayer(0), CreateLayer(1), CreateLayer(2)];
    Layers[0].Strokes = [
        { Points: [[-420, 80], [-380, -40], [-280, -100], [-170, -90], [-105, -20], [-130, 75], [-220, 135], [-320, 160]], Radius: 110, Erase: false },
        { Points: [[-350, 30], [-230, 25], [-170, 70]], Radius: 100, Erase: false },
        { Points: [[-180, 10], [-90, 60], [-30, 15], [30, -60]], Radius: 65, Erase: false },
        { Points: [[-180, 200], [-100, 240]], Radius: 55, Erase: false },
        { Points: [[-385, -170], [-370, -110]], Radius: 60, Erase: true }
    ];
    Layers[0].Regions = [{ X0: -560, Z0: -250, X1: 135, Z1: 335, Edge: 65 }];
    Layers[0].Landforms = [
        { ...CreateLandform('ridged-multifractal'), Name: 'Mountain ridges', Amount: 185, Scale: 1.45, InfluenceX: -.25, InfluenceZ: -.15, InfluenceRadius: 1.3, InfluenceFalloff: .95, Blend: 0, Seed: 4029 },
        { ...CreateLandform('sand-dunes'), Name: 'Coastal dunes', Amount: 48, Scale: 3.7, Azimuth: -20, InfluenceX: .35, InfluenceZ: .42, InfluenceRadius: .68, InfluenceFalloff: 1, Blend: 0, Seed: 8119 }
    ];
    Layers[1].Strokes = [
        { Points: [[230, 0], [285, 25], [300, 100], [240, 165]], Radius: 72, Erase: false },
        { Points: [[350, -115], [395, -85]], Radius: 38, Erase: false },
        { Points: [[150, 295], [210, 320]], Radius: 38, Erase: false }
    ];
    Layers[1].Regions = [{ X0: 65, Z0: -190, X1: 465, Z1: 395, Edge: 48 }];
    Layers[1].Landforms = [{ ...CreateLandform('fractal-ridges'), Amount: 145, Scale: 1.3, InfluenceRadius: 1.6, Blend: 0, Seed: 1771 }];
    Layers[2].Strokes = [{ Points: [[-150, -395], [-65, -440], [30, -405], [115, -430], [180, -380]], Radius: 68, Erase: false }];
    Layers[2].Regions = [{ X0: -250, Z0: -540, X1: 280, Z1: -270, Edge: 55 }];
    Layers[2].Landforms = [{ ...CreateLandform('multifractal'), Amount: 125, Scale: 2.1, InfluenceRadius: 1.4, Blend: 0, Seed: 6032 }];
    return Layers;
}

