/*==========================================================================================================================================
                                                          BAKEVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Reproducible 1024 × 128 × 1024 sparse-volume sampling and compression measurements.
import { performance } from 'node:perf_hooks';
import { Readable, Writable } from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { createGzip } from 'node:zlib';
import { writeFile } from 'node:fs/promises';
import Assert from 'node:assert/strict';
import { VolumeSpecification, EncodeSparseVolume } from '../../Experimental/LandscapeWorkspace/SparseVolume.js';
import { VolumeDistance } from '../../Experimental/LandscapeWorkspace/DistanceField.js';
const Layer={Name:'1024 coast validation',Color:'#b4a17d',Visible:true,Depth:120,Caves:true,Resolution:1024,Vertical:128,Strokes:[{Points:[[-150,0],[0,-100],[150,0],[100,150]],Radius:100,Erase:false}],Regions:[{X0:-300,Z0:-250,X1:300,Z1:300}]};
const Specification=VolumeSpecification(Layer,Layer.Regions[0]);
const Started=performance.now();
let Progress, CompressedBytes=0, CheckedSamples=0, MaximumError=0;
function* Chunks()
{
    for(const Entry of EncodeSparseVolume(Specification))
    {
        Progress=Entry.Progress;
        if(Entry.Bytes[0]!==123)
        {
            const View=new DataView(Entry.Bytes.buffer,Entry.Bytes.byteOffset,Entry.Bytes.byteLength);
            const Category=View.getUint32(0,true),X=View.getUint32(4,true),Y=View.getUint32(8,true),Z=View.getUint32(12,true),Width=View.getUint32(16,true);
            for(const Offset of [0,Math.floor(Width**3/2),Width**3-1])
            {
                const Cell=[X+Offset%Width,Y+Math.floor(Offset/Width)%Width,Z+Math.floor(Offset/Width**2)];
                const Point=Cell.map((Coordinate,Axis)=>Specification.Origin[Axis]+(Coordinate+.5)*Specification.Pitch[Axis]);
                const Expected=Math.max(-Specification.Band,Math.min(Specification.Band,VolumeDistance(Layer,...Point)));
                const Observed=View.getFloat32(20+(Category===1?0:Offset*4),true);
                MaximumError=Math.max(MaximumError,Math.abs(Expected-Observed));CheckedSamples++;
                Assert.ok(Math.abs(Expected-Observed)<1e-5);
            }
        }
        yield Entry.Bytes;
    }
}
await pipeline(Readable.from(Chunks()),createGzip(),new Writable({write(Chunk,Encoding,Next){CompressedBytes+=Chunk.length;Next();}}));
Assert.equal(Progress.Covered,1024*128*1024);
const Results={Dimensions:Specification.Dimensions,TruncationBandMetres:Specification.Band,...Progress,CompressedBytes,CheckedSamples,MaximumError,ElapsedSeconds:(performance.now()-Started)/1000,Platform:process.platform,Architecture:process.arch,Node:process.version,Fixture:Layer};
await writeFile(new URL('./BakeResults.json',import.meta.url),JSON.stringify(Results,null,2));
console.log(JSON.stringify(Results,null,2));
