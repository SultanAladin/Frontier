/*==========================================================================================================================================
                                                           LANDFORMPRESETS.JS
==========================================================================================================================================*/
// 🧩 Independent mask-layer recipe presets and reproducible signed-volume example documents.

import { CreateLandform } from './LandformStack.js';
const Operation = (Type, Parameters) => ({...CreateLandform(Type),...Parameters});

export const LandformPresets = [
    {Token:'desert-mesas',Name:'Desert mesas',Category:'PLATEAU / LEDGES',Color:'#b29b76',
        Description:'Broad sandstone mesas with alternating ledges and overhanging hoodoo caps.',
        Operations:[Operation('mesa',{Amount:100,Scale:1.5,Blend:12}),Operation('strata',{Amount:55,Scale:1,Blend:4}),Operation('stacks',{Amount:75,Scale:1.5,Seed:6741})]},
    {Token:'alpine-ridges',Name:'Alpine ridges',Category:'RIDGES / UPLIFT',Color:'#929c91',
        Description:'A seeded ridge range with subsidiary spurs and an uplifted fault shoulder.',
        Operations:[Operation('fault',{Amount:38,Scale:1,Azimuth:-20}),Operation('ridges',{Amount:140,Scale:2.5,Sharpness:.8,Blend:12})]},
    {Token:'dune-sea',Name:'Dune sea',Category:'AEOLIAN / DUNES',Color:'#c6ad76',
        Description:'Two differently seeded dune populations, blended into a sandy mask volume.',
        Operations:[Operation('dunes',{Amount:48,Scale:3,Azimuth:35,Sharpness:.35,Blend:15}),Operation('dunes',{Amount:24,Scale:2.5,Azimuth:70,Seed:9181,Sharpness:.3,Blend:10})]},
    {Token:'basalt-terraces',Name:'Basalt terraces',Category:'STRATA / ESCARPMENT',Color:'#7d8e87',
        Description:'A raised escarpment and stepped beds with a true volumetric undercut.',
        Operations:[Operation('fault',{Amount:105,Azimuth:40}),Operation('strata',{Amount:80,Scale:2,Blend:3}),Operation('tunnel',{Amount:40,Azimuth:28,Blend:5})]},
    {Token:'impact-basin',Name:'Impact basin',Category:'CRATER / EXCAVATION',Color:'#a09689',
        Description:'A raised plateau excavated by deep crater bowls; subtraction operates on the 3D solid.',
        Operations:[Operation('mesa',{Amount:40,Scale:1,Blend:12}),Operation('craters',{Amount:120,Scale:1.5,Blend:8})]},
    {Token:'coastal-arches',Name:'Coastal arches',Category:'ARCH / CAVE',Color:'#b7c9d0',
        Description:'A standing torus arch above a clipped island, with an additional cave cut through the foundation.',
        Operations:[Operation('arch',{Amount:140,Scale:1,Blend:8}),Operation('tunnel',{Amount:42,Azimuth:0,Blend:6})]}
];

export function RetrievePreset(Token)
{
    const Preset=LandformPresets.find(Entry=>Entry.Token===Token);
    if(!Preset)throw Error('Unknown landform preset');
    return Preset;
}

export function CreatePresetOperations(Token)
{
    return structuredClone(RetrievePreset(Token).Operations);
}

export function CreatePresetExample(Token, CentreX=0, CentreZ=0)
{
    const Preset=RetrievePreset(Token);
    return {Name:Preset.Name,Color:Preset.Color,Visible:true,Depth:100,Caves:false,Resolution:256,Vertical:64,
        Landforms:CreatePresetOperations(Token),
        Strokes:[{Points:[[CentreX-25,CentreZ],[CentreX+25,CentreZ]],Radius:155,Erase:false}],
        Regions:[{X0:CentreX-210,Z0:CentreZ-195,X1:CentreX+210,Z1:CentreZ+195,Edge:25}]};
}

export function CreatePresetWorld()
{
    return LandformPresets.map((Preset,Index)=>CreatePresetExample(Preset.Token,(Index%3-1)*500,(Math.floor(Index/3)-.5)*500));
}
