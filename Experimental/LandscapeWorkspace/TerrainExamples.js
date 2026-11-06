/*==========================================================================================================================================
                                                            TERRAINEXAMPLES.JS
==========================================================================================================================================*/
// 🧩 Already-applied landform generator stacks on independent editable mask volumes.

import { CreateLandform } from './LandformStack.js';
export const TerrainExamples=[
    ['fractal','Fractal terrain','#9aa58c'],['multifractal','Multifractal terrain','#a8a086'],
    ['ridged-multifractal','Ridged multifractal','#929b9d'],['badlands','Badlands','#b29072'],
    ['ridge-noise','Ridge noise','#8a9b85'],['fractal-ridges','Fractal ridges','#929798'],
    ['sand-dunes','Sand dunes','#c6ad76'],['craters','Crater field','#a09689']
];

export function CreateTerrainWorld()
{
    return TerrainExamples.map(([Type,Name,Color],Index)=>
    {
        const X=(Index%4-1.5)*850,Z=(Math.floor(Index/4)-.5)*850;
        const Operation={...CreateLandform(Type),Amount:Type==='sand-dunes'?85:180,Scale:Type==='badlands'?3:2,Blend:0};
        return {Name,Color,Visible:true,Depth:100,Caves:false,Resolution:256,Vertical:64,Landforms:Type==='craters'?[{...CreateLandform('fractal'),Amount:90,Blend:0},Operation]:[Operation],
            Strokes:[{Points:[[X-60,Z],[X+60,Z]],Radius:260,Erase:false}],Regions:[{X0:X-360,Z0:Z-330,X1:X+360,Z1:Z+330,Edge:12}]};
    });
}
