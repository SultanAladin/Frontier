/*====================================================================================================
                              RENDERHEIGHTMAPPREVIEW.MJS
====================================================================================================*/
// 🧩 Evidence generator: renders the prototype's own SDF height field (continent
// stamps, gulfs, fractal plates) to a PNG. Reuses Experimental/TerrainWorldGenerator/js/TerrainField.js
// directly so the image is produced by the same code that runs in the browser.

import { TerrainField } from '../../../Experimental/TerrainWorldGenerator/js/TerrainField.js';
import { writeFileSync } from 'fs';
import { deflateRawSync } from 'zlib';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
const ResultsDir = join(dirname(fileURLToPath(import.meta.url)), '..', 'Results');

const Field = new TerrainField();
Field.Regenerate(0xC0FFEE);

// [m] - Sculpt a demonstration basin + ridge so the preview shows editable state.
Field.ApplySculptBrush(800, 800, 600, 45, 'Lower');
Field.ApplySculptBrush(-900, -700, 500, 60, 'Raise');

const Width = 512;
const Height = 512;
const Pixels = Field.BuildColorPreview(Width, Height);

// CRC32 table (PNG).
const CrcTable = new Uint32Array(256);
for (let N = 0; N < 256; N++)
{
    let C = N;
    for (let K = 0; K < 8; K++) C = (C & 1) ? (0xEDB88320 ^ (C >>> 1)) : (C >>> 1);
    CrcTable[N] = C;
}
const Crc32 = (Buf, Start, Len) =>
{
    let C = 0xFFFFFFFF;
    for (let I = Start; I < Start + Len; I++) C = CrcTable[(C ^ Buf[I]) & 0xFF] ^ (C >>> 8);
    return (C ^ 0xFFFFFFFF) >>> 0;
};

const Chunk = (Type, Data) =>
{
    const Len = Data.byteLength;
    const Out = new Uint8Array(4 + 4 + Len + 4);
    let O = 0;
    Out[O++] = (Len >>> 24) & 0xFF; Out[O++] = (Len >>> 16) & 0xFF;
    Out[O++] = (Len >>> 8) & 0xFF; Out[O++] = Len & 0xFF;
    for (let I = 0; I < 4; I++) Out[O++] = Type.charCodeAt(I);
    Out.set(Data, O); O += Len;
    const C = Crc32(Out, 4, 4 + Len);
    Out[O++] = (C >>> 24) & 0xFF; Out[O++] = (C >>> 16) & 0xFF;
    Out[O++] = (C >>> 8) & 0xFF; Out[O++] = C & 0xFF;
    return Out;
};

const IHDR = new Uint8Array(13);
let O = 0;
const W32 = (V) => { IHDR[O++] = (V >>> 24) & 0xFF; IHDR[O++] = (V >>> 16) & 0xFF; IHDR[O++] = (V >>> 8) & 0xFF; IHDR[O++] = V & 0xFF; };
W32(Width); W32(Height);
IHDR[O++] = 8;  // bit depth
IHDR[O++] = 6;  // colour type RGBA
IHDR[O++] = 0;  // compression
IHDR[O++] = 0;  // filter
IHDR[O++] = 0;  // interlace

const Scanlines = new Uint8Array(Height * (1 + Width * 4));
for (let Y = 0; Y < Height; Y++)
{
    const RowOff = Y * (1 + Width * 4);
    Scanlines[RowOff] = 0; // no filter
    Scanlines.set(Pixels.subarray(Y * Width * 4, (Y + 1) * Width * 4), RowOff + 1);
}
const IDAT = new Uint8Array([...deflateRawSync(Scanlines)]);

const Parts = [];
Parts.push(Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]));
Parts.push(Buffer.from(Chunk('IHDR', IHDR)));
Parts.push(Buffer.from(Chunk('IDAT', IDAT)));
Parts.push(Buffer.from(Chunk('IEND', new Uint8Array(0))));
const PNG = Buffer.concat(Parts);

const OutPath = join(ResultsDir, 'TerrainHeightmapPreview.png');
writeFileSync(OutPath, PNG);
console.log('Wrote', OutPath, PNG.byteLength, 'bytes');
