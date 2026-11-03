/*============================================================================================================================================
                                                        PROJECTCODEC.JS
============================================================================================================================================*/
// 🧩 Deterministic project serialization plus dependency-free grayscale PNG encoding for heightfield export

const CrcTable = new Uint32Array(256);
for (let Entry = 0; Entry < 256; ++Entry)
{
    let Remainder = Entry;
    for (let Bit = 0; Bit < 8; ++Bit) Remainder = (Remainder & 1) ? 0xEDB88320 ^ (Remainder >>> 1) : Remainder >>> 1;
    CrcTable[Entry] = Remainder >>> 0;
}

function ComputeCrc(Bytes)
{
    let Remainder = 0xFFFFFFFF;
    for (let Offset = 0; Offset < Bytes.length; ++Offset) Remainder = CrcTable[(Remainder ^ Bytes[Offset]) & 0xFF] ^ (Remainder >>> 8);
    return (Remainder ^ 0xFFFFFFFF) >>> 0;
}

function ComputeAdler(Bytes)
{
    let First = 1;
    let Second = 0;
    for (let Offset = 0; Offset < Bytes.length; ++Offset)
    {
        First = (First + Bytes[Offset]) % 65521;
        Second = (Second + First) % 65521;
    }
    return ((Second << 16) | First) >>> 0;
}

function AppendChunk(Target, Offset, Identifier, Payload)
{
    const Length = Payload.length;
    Target[Offset++] = (Length >>> 24) & 0xFF;
    Target[Offset++] = (Length >>> 16) & 0xFF;
    Target[Offset++] = (Length >>> 8) & 0xFF;
    Target[Offset++] = Length & 0xFF;
    const IdentifierBytes = new TextEncoder().encode(Identifier);
    Target.set(IdentifierBytes, Offset);
    Offset += 4;
    Target.set(Payload, Offset);
    Offset += Length;
    const CrcBytes = new Uint8Array(4 + Length);
    CrcBytes.set(IdentifierBytes, 0);
    CrcBytes.set(Payload, 4);
    const Crc = ComputeCrc(CrcBytes);
    Target[Offset++] = (Crc >>> 24) & 0xFF;
    Target[Offset++] = (Crc >>> 16) & 0xFF;
    Target[Offset++] = (Crc >>> 8) & 0xFF;
    Target[Offset++] = Crc & 0xFF;
    return Offset;
}

// 📝 Stored (uncompressed) DEFLATE blocks keep the encoder dependency-free on both browser and Node.
export function EncodePng(Width, Height, Samples, BytesPerChannel = 1, ColourType = 0, BitDepth = null)
{
    const RowBytes = Width * BytesPerChannel;
    const Filtered = new Uint8Array((RowBytes + 1) * Height);
    for (let Row = 0; Row < Height; ++Row)
    {
        Filtered[Row * (RowBytes + 1)] = 0;
        Filtered.set(Samples.subarray(Row * RowBytes, (Row + 1) * RowBytes), Row * (RowBytes + 1) + 1);
    }
    const MaximumBlock = 65535;
    const BlockCount = Math.ceil(Filtered.length / MaximumBlock) || 1;
    const Compressed = new Uint8Array(2 + Filtered.length + BlockCount * 5 + 4);
    let Offset = 0;
    Compressed[Offset++] = 0x78;
    Compressed[Offset++] = 0x01;
    for (let BlockIndex = 0; BlockIndex < BlockCount; ++BlockIndex)
    {
        const Start = BlockIndex * MaximumBlock;
        const Length = Math.min(MaximumBlock, Filtered.length - Start);
        Compressed[Offset++] = BlockIndex === BlockCount - 1 ? 1 : 0;
        Compressed[Offset++] = Length & 0xFF;
        Compressed[Offset++] = (Length >>> 8) & 0xFF;
        Compressed[Offset++] = (~Length) & 0xFF;
        Compressed[Offset++] = ((~Length) >>> 8) & 0xFF;
        Compressed.set(Filtered.subarray(Start, Start + Length), Offset);
        Offset += Length;
    }
    const Adler = ComputeAdler(Filtered);
    Compressed[Offset++] = (Adler >>> 24) & 0xFF;
    Compressed[Offset++] = (Adler >>> 16) & 0xFF;
    Compressed[Offset++] = (Adler >>> 8) & 0xFF;
    Compressed[Offset++] = Adler & 0xFF;
    const ZlibPayload = Compressed.subarray(0, Offset);

    const Header = new Uint8Array(13);
    Header[0] = Width >>> 24; Header[1] = Width >>> 16; Header[2] = Width >>> 8; Header[3] = Width;
    Header[4] = Height >>> 24; Header[5] = Height >>> 16; Header[6] = Height >>> 8; Header[7] = Height;
    Header[8] = BitDepth ?? (BytesPerChannel >= 2 && ColourType === 0 ? 16 : 8);
    Header[9] = ColourType;
    Header[10] = 0; Header[11] = 0; Header[12] = 0;
    const Signature = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10]);
    const Total = 8 + (12 + 13) + (12 + ZlibPayload.length) + 12;
    const Result = new Uint8Array(Total);
    let Cursor = 0;
    Result.set(Signature, Cursor); Cursor += 8;
    Cursor = AppendChunk(Result, Cursor, 'IHDR', Header);
    Cursor = AppendChunk(Result, Cursor, 'IDAT', ZlibPayload);
    Cursor = AppendChunk(Result, Cursor, 'IEND', new Uint8Array(0));
    return Result;
}

export function EncodeGrayscalePng(Width, Height, Samples, BytesPerChannel = 1)
{
    return EncodePng(Width, Height, Samples, BytesPerChannel, 0, BytesPerChannel === 2 ? 16 : 8);
}

export function EncodeHeightfieldPng(Heightfield, MinimumElevation = null, MaximumElevation = null)
{
    const Resolution = Heightfield.Resolution;
    const Minimum = MinimumElevation ?? -4000;
    const Maximum = MaximumElevation ?? 4000;
    const Samples = new Uint8Array(Resolution * Resolution * 2);
    for (let Index = 0; Index < Heightfield.Elevations.length; ++Index)
    {
        const Normalized = Math.max(0, Math.min(1, (Heightfield.Elevations[Index] - Minimum) / (Maximum - Minimum)));
        const Quantized = Math.round(Normalized * 65535);
        Samples[Index * 2] = (Quantized >>> 8) & 0xFF;
        Samples[Index * 2 + 1] = Quantized & 0xFF;
    }
    return EncodeGrayscalePng(Resolution, Resolution, Samples, 2);
}

export function EncodeColourPng(Width, Height, RgbSamples)
{
    return EncodePng(Width, Height, RgbSamples, 3, 2, 8);
}

export function EncodeScalarFieldPng(Field, Resolution, Minimum, Maximum)
{
    const Samples = new Uint8Array(Resolution * Resolution * 2);
    for (let Index = 0; Index < Field.length; ++Index)
    {
        const Normalized = Math.max(0, Math.min(1, (Field[Index] - Minimum) / (Maximum - Minimum || 1)));
        const Quantized = Math.round(Normalized * 65535);
        Samples[Index * 2] = (Quantized >>> 8) & 0xFF;
        Samples[Index * 2 + 1] = Quantized & 0xFF;
    }
    return EncodeGrayscalePng(Resolution, Resolution, Samples, 2);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROJECT DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

export function EncodeProject(Document)
{
    return JSON.stringify({
        Format: 'Frontier.WorldForge.Project',
        Revision: 1,
        Crust: Document.CrustConfiguration,
        Primitives: Document.Field.Primitives,
        Strokes: Document.Heightfield.StrokeCommands,
        Water: Document.WaterConfiguration,
        Spectral: Document.SpectralConfiguration,
        Camera: { Position: Document.Camera.Position, Yaw: Document.Camera.Yaw, Pitch: Document.Camera.Pitch, FlySpeed: Document.Camera.FlySpeed },
        View: Document.View
    }, null, 2);
}

export function DecodeProject(Text)
{
    const Parsed = JSON.parse(Text);
    if (Parsed.Format !== 'Frontier.WorldForge.Project') throw new Error('Unsupported project format');
    if (!Array.isArray(Parsed.Primitives) || !Array.isArray(Parsed.Strokes)) throw new Error('Malformed project document');
    return Parsed;
}
