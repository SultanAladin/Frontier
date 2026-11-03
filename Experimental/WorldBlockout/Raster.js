'use strict';
//----------------------------------------------------------------------------------------------------------------------------
//                                            Frontier · World Blockout · Raster
//----------------------------------------------------------------------------------------------------------------------------
// 📝 What this file is: a tiny software rasteriser and PNG writer, small enough to run in a browser tab and in Node with no
//    dependency in either. The blockout needs images — a planet map, a resolution heatmap, a surface slice — and neither a
//    GPU nor a canvas is guaranteed to exist where the evidence scripts run.
//
//    PNG here is deliberately naive: 8-bit RGB, filter 0 on every scanline, and a deflate stream made of stored blocks. It is
//    bigger on disk than a real encoder and it is exactly reproducible, which is what an evidence image wants.
//----------------------------------------------------------------------------------------------------------------------------

const Raster = (function ()
{
    const CRC_TABLE = (() =>
    {
        const Table = new Uint32Array(256);
        for (let Index = 0; Index < 256; ++Index)
        {
            let Value = Index;
            for (let Bit = 0; Bit < 8; ++Bit) Value = Value & 1 ? 0xEDB88320 ^ (Value >>> 1) : Value >>> 1;
            Table[Index] = Value >>> 0;
        }
        return Table;
    })();

    function Crc32(Bytes, Start, Length)
    {
        let Value = 0xFFFFFFFF;
        for (let Index = 0; Index < Length; ++Index)
        {
            Value = CRC_TABLE[(Value ^ Bytes[Start + Index]) & 0xFF] ^ (Value >>> 8);
        }
        return (Value ^ 0xFFFFFFFF) >>> 0;
    }

    function Adler32(Bytes)
    {
        let A = 1, B = 0;
        for (let Index = 0; Index < Bytes.length; ++Index)
        {
            A = (A + Bytes[Index]) % 65521;
            B = (B + A) % 65521;
        }
        return ((B << 16) | A) >>> 0;
    }

    // 📝 A real deflate: fixed Huffman codes, greedy LZ77 with a hash of three bytes and a 32 KiB window. A stored-block
    //    encoder is simpler, but a planet map at 960 × 480 lands at 1.8 MB of base64 and that is a slow page and a fat
    //    evidence folder. This is eighty lines and brings the same map under a hundred kilobytes.
    const LENGTH_BASE = [3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
        163, 195, 227, 258];
    const LENGTH_EXTRA = [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0];
    const DISTANCE_BASE = [1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
        2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577];
    const DISTANCE_EXTRA = [0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12,
        13, 13];

    function Deflate(Raw)
    {
        const Out = [];
        let BitBuffer = 0, BitCount = 0;
        function PushByte(Value) { Out.push(Value & 0xFF); }
        function WriteLsb(Value, Count)
        {
            for (let Index = 0; Index < Count; ++Index)
            {
                BitBuffer |= ((Value >>> Index) & 1) << BitCount;
                if (++BitCount === 8) { PushByte(BitBuffer); BitBuffer = 0; BitCount = 0; }
            }
        }
        function WriteCode(Value, Count)
        {
            for (let Index = Count - 1; Index >= 0; --Index)
            {
                BitBuffer |= ((Value >>> Index) & 1) << BitCount;
                if (++BitCount === 8) { PushByte(BitBuffer); BitBuffer = 0; BitCount = 0; }
            }
        }
        function WriteLiteral(Byte)
        {
            if (Byte <= 143) WriteCode(0x30 + Byte, 8);
            else WriteCode(0x190 + (Byte - 144), 9);
        }
        function WriteLength(Length)
        {
            let Index = LENGTH_BASE.length - 1;
            while (Index > 0 && LENGTH_BASE[Index] > Length) --Index;
            const Code = 257 + Index;
            if (Code <= 279) WriteCode(Code - 256, 7);
            else WriteCode(0xC0 + (Code - 280), 8);
            WriteLsb(Length - LENGTH_BASE[Index], LENGTH_EXTRA[Index]);
        }
        function WriteDistance(Distance)
        {
            let Index = DISTANCE_BASE.length - 1;
            while (Index > 0 && DISTANCE_BASE[Index] > Distance) --Index;
            WriteCode(Index, 5);
            WriteLsb(Distance - DISTANCE_BASE[Index], DISTANCE_EXTRA[Index]);
        }

        PushByte(0x78);
        PushByte(0x01);
        WriteLsb(1, 1);             // BFINAL
        WriteLsb(1, 2);             // BTYPE: fixed Huffman

        const Table = new Map();
        const Window = 32768;
        let Cursor = 0;
        while (Cursor < Raw.length)
        {
            let BestLength = 0, BestDistance = 0;
            if (Cursor + 2 < Raw.length)
            {
                const Key = (Raw[Cursor] << 16) | (Raw[Cursor + 1] << 8) | Raw[Cursor + 2];
                const Candidate = Table.get(Key);
                Table.set(Key, Cursor);
                if (Candidate !== undefined && Cursor - Candidate <= Window)
                {
                    let Length = 0;
                    const Limit = Math.min(258, Raw.length - Cursor);
                    while (Length < Limit && Raw[Candidate + Length] === Raw[Cursor + Length]) ++Length;
                    if (Length >= 3) { BestLength = Length; BestDistance = Cursor - Candidate; }
                }
            }
            if (BestLength >= 3)
            {
                WriteLength(BestLength);
                WriteDistance(BestDistance);
                for (let Index = 1; Index < BestLength - 2 && Cursor + Index + 2 < Raw.length; ++Index)
                {
                    const Offset = Cursor + Index;
                    Table.set((Raw[Offset] << 16) | (Raw[Offset + 1] << 8) | Raw[Offset + 2], Offset);
                }
                Cursor += BestLength;
            }
            else
            {
                WriteLiteral(Raw[Cursor]);
                ++Cursor;
            }
        }
        // 📝 End of block, then flush the partial byte and append the Adler-32 of the uncompressed bytes.
        WriteCode(0, 7);
        if (BitCount > 0) { PushByte(BitBuffer); BitBuffer = 0; BitCount = 0; }
        const Checksum = Adler32(Raw);
        PushByte(Checksum >>> 24);
        PushByte(Checksum >>> 16);
        PushByte(Checksum >>> 8);
        PushByte(Checksum);
        return Uint8Array.from(Out);
    }

    function Chunk(Type, Data)
    {
        const Out = new Uint8Array(12 + Data.length);
        const Length = Data.length;
        Out[0] = (Length >>> 24) & 0xFF;
        Out[1] = (Length >>> 16) & 0xFF;
        Out[2] = (Length >>> 8) & 0xFF;
        Out[3] = Length & 0xFF;
        for (let Index = 0; Index < 4; ++Index) Out[4 + Index] = Type.charCodeAt(Index);
        Out.set(Data, 8);
        const Checksum = Crc32(Out, 4, 4 + Data.length);
        Out[8 + Data.length] = (Checksum >>> 24) & 0xFF;
        Out[9 + Data.length] = (Checksum >>> 16) & 0xFF;
        Out[10 + Data.length] = (Checksum >>> 8) & 0xFF;
        Out[11 + Data.length] = Checksum & 0xFF;
        return Out;
    }

    function EncodePng(Raster)
    {
        const { Width, Height, Pixels } = Raster;
        const Stride = Width * 3 + 1;
        const Raw = new Uint8Array(Stride * Height);
        for (let Row = 0; Row < Height; ++Row)
        {
            Raw[Row * Stride] = 0;
            Raw.set(Pixels.subarray(Row * Width * 3, (Row + 1) * Width * 3), Row * Stride + 1);
        }
        const Header = new Uint8Array(13);
        Header[0] = (Width >>> 24) & 0xFF; Header[1] = (Width >>> 16) & 0xFF;
        Header[2] = (Width >>> 8) & 0xFF; Header[3] = Width & 0xFF;
        Header[4] = (Height >>> 24) & 0xFF; Header[5] = (Height >>> 16) & 0xFF;
        Header[6] = (Height >>> 8) & 0xFF; Header[7] = Height & 0xFF;
        Header[8] = 8; Header[9] = 2; Header[10] = 0; Header[11] = 0; Header[12] = 0;
        const Parts = [
            new Uint8Array([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
            Chunk('IHDR', Header),
            Chunk('IDAT', Deflate(Raw)),
            Chunk('IEND', new Uint8Array(0))
        ];
        const Total = Parts.reduce((Sum, Part) => Sum + Part.length, 0);
        const Out = new Uint8Array(Total);
        let Cursor = 0;
        for (const Part of Parts) { Out.set(Part, Cursor); Cursor += Part.length; }
        return Out;
    }

    function ToDataUrl(Bytes)
    {
        let Binary = '';
        const ChunkSize = 0x8000;
        for (let Index = 0; Index < Bytes.length; Index += ChunkSize)
        {
            Binary += String.fromCharCode.apply(null, Bytes.subarray(Index, Index + ChunkSize));
        }
        const Base = typeof btoa === 'function' ? btoa(Binary) : Buffer.from(Bytes).toString('base64');
        return `data:image/png;base64,${Base}`;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                    DRAWING
    //------------------------------------------------------------------------------------------------------------------------

    function Colour(Value)
    {
        if (Array.isArray(Value)) return Value;
        if (typeof Value === 'object') return [Value.R ?? Value.r, Value.G ?? Value.g, Value.B ?? Value.b];
        const Text = String(Value).replace('#', '');
        return [parseInt(Text.slice(0, 2), 16), parseInt(Text.slice(2, 4), 16), parseInt(Text.slice(4, 6), 16)];
    }

    function CreateRaster(Width, Height, Background)
    {
        const Raster = { Width, Height, Pixels: new Uint8Array(Width * Height * 3) };
        if (Background) Fill(Raster, Background);
        return Raster;
    }

    function Fill(Raster, Value)
    {
        const [R, G, B] = Colour(Value);
        const Pixels = Raster.Pixels;
        for (let Index = 0; Index < Pixels.length; Index += 3)
        {
            Pixels[Index] = R; Pixels[Index + 1] = G; Pixels[Index + 2] = B;
        }
        return Raster;
    }

    function SetPixel(Raster, X, Y, Value, Alpha)
    {
        const Px = Math.round(X), Py = Math.round(Y);
        if (Px < 0 || Py < 0 || Px >= Raster.Width || Py >= Raster.Height) return;
        const Offset = (Py * Raster.Width + Px) * 3;
        const [R, G, B] = Colour(Value);
        const Mix = Alpha === undefined ? 1 : Math.max(0, Math.min(1, Alpha));
        const Pixels = Raster.Pixels;
        Pixels[Offset] = Pixels[Offset] * (1 - Mix) + R * Mix;
        Pixels[Offset + 1] = Pixels[Offset + 1] * (1 - Mix) + G * Mix;
        Pixels[Offset + 2] = Pixels[Offset + 2] * (1 - Mix) + B * Mix;
    }

    function Mix(First, Second, T)
    {
        const A = Colour(First), B = Colour(Second);
        const Clamp = Math.max(0, Math.min(1, T));
        return [
            Math.round(A[0] + (B[0] - A[0]) * Clamp),
            Math.round(A[1] + (B[1] - A[1]) * Clamp),
            Math.round(A[2] + (B[2] - A[2]) * Clamp)
        ];
    }

    function FillRect(Raster, X, Y, Width, Height, Value, Alpha)
    {
        for (let Row = Math.round(Y); Row < Math.round(Y + Height); ++Row)
        {
            for (let Column = Math.round(X); Column < Math.round(X + Width); ++Column)
            {
                SetPixel(Raster, Column, Row, Value, Alpha);
            }
        }
        return Raster;
    }

    function DrawLine(Raster, X0, Y0, X1, Y1, Value, Width, Alpha)
    {
        const Steps = Math.max(1, Math.ceil(Math.max(Math.abs(X1 - X0), Math.abs(Y1 - Y0))));
        const Radius = Math.max(0, (Width ?? 1) - 1) / 2;
        for (let Step = 0; Step <= Steps; ++Step)
        {
            const T = Step / Steps;
            const X = X0 + (X1 - X0) * T, Y = Y0 + (Y1 - Y0) * T;
            if (Radius <= 0) SetPixel(Raster, X, Y, Value, Alpha);
            else
            {
                for (let Row = -Math.ceil(Radius); Row <= Math.ceil(Radius); ++Row)
                {
                    for (let Column = -Math.ceil(Radius); Column <= Math.ceil(Radius); ++Column)
                    {
                        if (Math.hypot(Column, Row) <= Radius) SetPixel(Raster, X + Column, Y + Row, Value, Alpha);
                    }
                }
            }
        }
        return Raster;
    }

    function FillCircle(Raster, X, Y, Radius, Value, Alpha)
    {
        for (let Row = -Math.ceil(Radius); Row <= Math.ceil(Radius); ++Row)
        {
            for (let Column = -Math.ceil(Radius); Column <= Math.ceil(Radius); ++Column)
            {
                if (Math.hypot(Column, Row) <= Radius) SetPixel(Raster, X + Column, Y + Row, Value, Alpha);
            }
        }
        return Raster;
    }

    function StrokeCircle(Raster, X, Y, Radius, Value, Width)
    {
        const Steps = Math.max(24, Math.ceil(Radius * 6));
        for (let Step = 0; Step <= Steps; ++Step)
        {
            const Angle = (Step / Steps) * Math.PI * 2;
            SetPixel(Raster, X + Math.cos(Angle) * Radius, Y + Math.sin(Angle) * Radius, Value, 1);
            if ((Width ?? 1) > 1) SetPixel(Raster, X + Math.cos(Angle) * (Radius + 1),
                Y + Math.sin(Angle) * (Radius + 1), Value, 1);
        }
        return Raster;
    }

    function FillPolygon(Raster, Points, Value, Alpha)
    {
        let Top = Infinity, Bottom = -Infinity;
        for (const [, Y] of Points) { Top = Math.min(Top, Y); Bottom = Math.max(Bottom, Y); }
        for (let Row = Math.floor(Top); Row <= Math.ceil(Bottom); ++Row)
        {
            const Crossings = [];
            for (let Index = 0; Index < Points.length; ++Index)
            {
                const [X1, Y1] = Points[Index], [X2, Y2] = Points[(Index + 1) % Points.length];
                if ((Y1 > Row) === (Y2 > Row)) continue;
                Crossings.push(X1 + (Row - Y1) * (X2 - X1) / (Y2 - Y1));
            }
            Crossings.sort((A, B) => A - B);
            for (let Index = 0; Index + 1 < Crossings.length; Index += 2)
            {
                for (let Column = Math.ceil(Crossings[Index]); Column <= Math.floor(Crossings[Index + 1]); ++Column)
                {
                    SetPixel(Raster, Column, Row, Value, Alpha);
                }
            }
        }
        return Raster;
    }

    return {
        CreateRaster, Fill, SetPixel, FillRect, DrawLine, FillCircle, StrokeCircle, FillPolygon,
        EncodePng, ToDataUrl, Colour, Mix, Crc32, Adler32
    };
})();

if (typeof module !== 'undefined' && module.exports) module.exports = Raster;
