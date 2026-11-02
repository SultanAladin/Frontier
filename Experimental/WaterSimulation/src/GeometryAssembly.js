/*====================================================================================================================================
                                                    GEOMETRY ASSEMBLY.JS
====================================================================================================================================*/
// 🧩 Indexed geometry creation and program-aware attribute binding for the surface, terrain, overlay, and hull

export class GeometryBuffer
{
    constructor(Device, Descriptor)
    {
        this.Device = Device;
        this.Gl = Device.Gl;
        this.Label = Descriptor.Label ?? 'Geometry';
        this.Layout = Descriptor.Layout;
        this.Stride = this.Layout.reduce((Total, Entry) => Total + Entry.Size, 0) * 4;
        this.IndexCount = Descriptor.Indices.length;
        this.DrawCategory = Descriptor.Category === 'lines' ? this.Gl.LINES : this.Gl.TRIANGLES;
        this.IndexType = Descriptor.Indices instanceof Uint32Array ? this.Gl.UNSIGNED_INT : this.Gl.UNSIGNED_SHORT;
        this.Bindings = new Map();

        const Gl = this.Gl;
        this.VertexBuffer = Gl.createBuffer();
        Gl.bindBuffer(Gl.ARRAY_BUFFER, this.VertexBuffer);
        Gl.bufferData(Gl.ARRAY_BUFFER, Descriptor.Vertices, Gl.STATIC_DRAW);

        this.IndexBuffer = Gl.createBuffer();
        Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, this.IndexBuffer);
        Gl.bufferData(Gl.ELEMENT_ARRAY_BUFFER, Descriptor.Indices, Gl.STATIC_DRAW);

        Gl.bindBuffer(Gl.ARRAY_BUFFER, null);
    }

    // 📝 One vertex array per program keeps attribute indices correct without global layout assumptions
    ResolveArrayObject(Program)
    {
        const Key = Program.Label;

        if (this.Bindings.has(Key))
        {
            return this.Bindings.get(Key);
        }

        const Gl = this.Gl;
        const ArrayObject = Gl.createVertexArray();
        Gl.bindVertexArray(ArrayObject);
        Gl.bindBuffer(Gl.ARRAY_BUFFER, this.VertexBuffer);

        let Offset = 0;

        for (const Entry of this.Layout)
        {
            const Location = Gl.getAttribLocation(Program.Program, Entry.Name);

            if (Location >= 0)
            {
                Gl.enableVertexAttribArray(Location);
                Gl.vertexAttribPointer(Location, Entry.Size, Gl.FLOAT, false, this.Stride, Offset);
            }

            Offset += Entry.Size * 4;
        }

        Gl.bindBuffer(Gl.ELEMENT_ARRAY_BUFFER, this.IndexBuffer);
        Gl.bindVertexArray(null);
        this.Bindings.set(Key, ArrayObject);

        return ArrayObject;
    }

    Draw(Program)
    {
        const Gl = this.Gl;
        Gl.bindVertexArray(this.ResolveArrayObject(Program));
        Gl.drawElements(this.DrawCategory, this.IndexCount, this.IndexType, 0);
        Gl.bindVertexArray(null);
    }
}

export function BuildRadialSurfaceGrid(Rings, Sectors, RadiusExponent = 2.35)
{
    const VertexCount = Rings * Sectors;
    const Vertices = new Float32Array(VertexCount * 2);
    const Indices = new Uint32Array((Rings - 1) * Sectors * 6);

    for (let Ring = 0; Ring < Rings; Ring++)
    {
        const Ratio = (Ring + 1) / Rings;
        const Radius = Math.pow(Ratio, RadiusExponent);

        for (let Sector = 0; Sector < Sectors; Sector++)
        {
            const Angle = (Sector / Sectors) * Math.PI * 2.0;
            const Index = (Ring * Sectors + Sector) * 2;
            Vertices[Index + 0] = Math.cos(Angle) * Radius;
            Vertices[Index + 1] = Math.sin(Angle) * Radius;
        }
    }

    let Cursor = 0;

    for (let Ring = 0; Ring < Rings - 1; Ring++)
    {
        for (let Sector = 0; Sector < Sectors; Sector++)
        {
            const NextSector = (Sector + 1) % Sectors;
            const Inner0 = Ring * Sectors + Sector;
            const Inner1 = Ring * Sectors + NextSector;
            const Outer0 = (Ring + 1) * Sectors + Sector;
            const Outer1 = (Ring + 1) * Sectors + NextSector;

            Indices[Cursor++] = Inner0;
            Indices[Cursor++] = Outer0;
            Indices[Cursor++] = Outer1;
            Indices[Cursor++] = Inner0;
            Indices[Cursor++] = Outer1;
            Indices[Cursor++] = Inner1;
        }
    }

    return {
        Label: 'RadialSurfaceGrid',
        Layout: [{ Name: 'a_GridPosition', Size: 2 }],
        Vertices: Vertices,
        Indices: Indices,
        Category: 'triangles'
    };
}

export function BuildTerrainGrid(Size)
{
    const Vertices = new Float32Array(Size * Size * 2);
    const Indices = new Uint32Array((Size - 1) * (Size - 1) * 6);

    for (let Row = 0; Row < Size; Row++)
    {
        for (let Column = 0; Column < Size; Column++)
        {
            const Index = (Row * Size + Column) * 2;
            Vertices[Index + 0] = Column / (Size - 1) - 0.5;
            Vertices[Index + 1] = Row / (Size - 1) - 0.5;
        }
    }

    let Cursor = 0;

    for (let Row = 0; Row < Size - 1; Row++)
    {
        for (let Column = 0; Column < Size - 1; Column++)
        {
            const LowerLeft = Row * Size + Column;
            const LowerRight = LowerLeft + 1;
            const UpperLeft = LowerLeft + Size;
            const UpperRight = UpperLeft + 1;

            Indices[Cursor++] = LowerLeft;
            Indices[Cursor++] = UpperLeft;
            Indices[Cursor++] = UpperRight;
            Indices[Cursor++] = LowerLeft;
            Indices[Cursor++] = UpperRight;
            Indices[Cursor++] = LowerRight;
        }
    }

    return {
        Label: 'TerrainGrid',
        Layout: [{ Name: 'a_GridPosition', Size: 2 }],
        Vertices: Vertices,
        Indices: Indices,
        Category: 'triangles'
    };
}

export function BuildOverlayLineGrid(Domain, Spacing, Segments)
{
    const Half = Domain * 0.5;
    const LineCount = Math.floor(Domain / Spacing) + 1;
    const Positions = [];
    const Indices = [];
    let VertexCursor = 0;

    for (let Line = 0; Line < LineCount; Line++)
    {
        const Coordinate = -Half + Line * Spacing;

        for (let Segment = 0; Segment < Segments; Segment++)
        {
            const Start = -Half + (Segment / Segments) * Domain;
            const End = -Half + ((Segment + 1) / Segments) * Domain;

            Positions.push(Coordinate, Start, Coordinate, End);
            Indices.push(VertexCursor, VertexCursor + 1, VertexCursor + 2, VertexCursor + 3);
            VertexCursor += 4;
        }

        for (let Segment = 0; Segment < Segments; Segment++)
        {
            const Start = -Half + (Segment / Segments) * Domain;
            const End = -Half + ((Segment + 1) / Segments) * Domain;

            Positions.push(Start, Coordinate, End, Coordinate);
            Indices.push(VertexCursor, VertexCursor + 1, VertexCursor + 2, VertexCursor + 3);
            VertexCursor += 4;
        }
    }

    return {
        Label: 'OverlayGrid',
        Layout: [{ Name: 'a_GridPosition', Size: 2 }],
        Vertices: new Float32Array(Positions),
        Indices: new Uint32Array(Indices),
        Category: 'lines'
    };
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    VESSEL HULL
------------------------------------------------------------------------------------------------------------------------------------*/

function HullColor(HeightAboveKeel, DeckDarkness)
{
    const White = [0.87, 0.88, 0.89];
    const Navy = [0.055, 0.12, 0.22];
    const Antifoul = [0.28, 0.06, 0.05];
    const Teak = [0.44 * DeckDarkness, 0.31 * DeckDarkness, 0.19 * DeckDarkness];

    if (DeckDarkness < 0.999)
    {
        return Teak;
    }

    if (HeightAboveKeel < -0.34)
    {
        return Antifoul;
    }

    if (HeightAboveKeel < -0.12)
    {
        return Navy;
    }

    return White;
}

// 🧩 Lofted speedboat: section-lofted hull, transom, deck, and a wheelhouse, with smooth accumulated normals
export function BuildVesselHull()
{
    const Stations = 26;
    const Sections = 12;
    const Length = 6.6;
    const HalfBeam = 1.28;
    const Draft = 0.78;
    const Sheer = 0.46;
    const Positions = [];
    const Colors = [];
    const Indices = [];

    const BeamAt = (Parameter) => HalfBeam * Math.pow(Math.sin(Math.PI * Math.min(1, 0.12 + 0.88 * Parameter)), 0.62);
    const KeelAt = (Parameter) => -Draft * (1.0 - 0.28 * Parameter);
    const SheerAt = (Parameter) => Sheer * (0.35 + 0.62 * Parameter);
    const StationZ = (Parameter) => Length * 0.5 - Length * Parameter;

    const PushVertex = (X, Y, Z, Color) =>
    {
        Positions.push(X, Y, Z);
        Colors.push(Color[0], Color[1], Color[2]);
        return Positions.length / 3 - 1;
    };

    for (let Side = 0; Side < 2; Side++)
    {
        const Sign = Side === 0 ? -1.0 : 1.0;

        for (let Station = 0; Station < Stations; Station++)
        {
            const Parameter = Station / (Stations - 1);
            const Beam = BeamAt(Parameter);
            const Keel = KeelAt(Parameter);
            const SheerLine = SheerAt(Parameter);
            const Z = StationZ(Parameter);

            for (let Section = 0; Section < Sections; Section++)
            {
                const Ratio = Section / (Sections - 1);
                const Y = Keel + (SheerLine - Keel) * Math.pow(Ratio, 1.32);
                const X = Sign * Beam * Math.pow(Ratio, 0.72);
                PushVertex(X, Y, Z, HullColor(Y, 1.0));
            }
        }
    }

    for (let Side = 0; Side < 2; Side++)
    {
        const Base = Side * Stations * Sections;

        for (let Station = 0; Station < Stations - 1; Station++)
        {
            for (let Section = 0; Section < Sections - 1; Section++)
            {
                const A = Base + Station * Sections + Section;
                const B = A + Sections;
                Indices.push(A, B, B + 1, A, B + 1, A + 1);
            }
        }
    }

    // Transom: closes the stern section across both sides
    const SternBase = 0;
    const StarboardBase = Stations * Sections;

    for (let Section = 0; Section < Sections - 1; Section++)
    {
        const Port0 = SternBase + Section;
        const Port1 = Port0 + 1;
        const Starboard0 = StarboardBase + Section;
        const Starboard1 = Starboard0 + 1;
        Indices.push(Port0, Port1, Starboard1, Port0, Starboard1, Starboard0);
    }

    // Deck: teak surface between the port and starboard sheer lines
    const DeckIndices = [];

    for (let Station = 0; Station < Stations - 1; Station++)
    {
        const Parameter = Station / (Stations - 1);
        const Beam = BeamAt(Parameter) * 0.985;
        const SheerLine = SheerAt(Parameter) - 0.045;
        const Z = StationZ(Parameter);
        const ZNext = StationZ((Station + 1) / (Stations - 1));
        const BeamNext = BeamAt((Station + 1) / (Stations - 1)) * 0.985;
        const SheerNext = SheerAt((Station + 1) / (Stations - 1)) - 0.045;
        const Teak = [0.44, 0.31, 0.19];

        const Port = PushVertex(-Beam, SheerLine, Z, Teak);
        const PortNext = PushVertex(-BeamNext, SheerNext, ZNext, Teak);
        const StarboardNext = PushVertex(BeamNext, SheerNext, ZNext, Teak);
        const Starboard = PushVertex(Beam, SheerLine, Z, Teak);

        DeckIndices.push(Port, PortNext, StarboardNext, Port, StarboardNext, Starboard);
    }

    Indices.push(...DeckIndices);

    // Wheelhouse: a low cabin block with a dark windscreen band
    const CabinNear = 0.40;
    const CabinFar = 0.66;
    const CabinHeight = 0.62;
    const CabinColor = [0.80, 0.82, 0.83];
    const GlassColor = [0.07, 0.11, 0.14];

    const CabinCorners = [];

    for (const Parameter of [CabinNear, CabinFar])
    {
        const Beam = BeamAt(Parameter) * 0.62;
        const Base = SheerAt(Parameter) - 0.05;
        const Z = StationZ(Parameter);
        const Ring =
        [
            PushVertex(-Beam, Base, Z, CabinColor),
            PushVertex(Beam, Base, Z, CabinColor),
            PushVertex(Beam, Base + CabinHeight, Z, GlassColor),
            PushVertex(-Beam, Base + CabinHeight, Z, GlassColor)
        ];
        CabinCorners.push(Ring);
    }

    const [NearRing, FarRing] = CabinCorners;

    for (let Index = 0; Index < 4; Index++)
    {
        const Next = (Index + 1) % 4;
        Indices.push(NearRing[Index], FarRing[Index], FarRing[Next], NearRing[Index], FarRing[Next], NearRing[Next]);
    }

    Indices.push(NearRing[0], NearRing[1], NearRing[2], NearRing[0], NearRing[2], NearRing[3]);

    // Smooth vertex normals from accumulated face normals
    const Normals = new Float32Array(Positions.length);

    for (let Triangle = 0; Triangle < Indices.length; Triangle += 3)
    {
        const IndexA = Indices[Triangle] * 3;
        const IndexB = Indices[Triangle + 1] * 3;
        const IndexC = Indices[Triangle + 2] * 3;

        const EdgeAB = [Positions[IndexB] - Positions[IndexA], Positions[IndexB + 1] - Positions[IndexA + 1], Positions[IndexB + 2] - Positions[IndexA + 2]];
        const EdgeAC = [Positions[IndexC] - Positions[IndexA], Positions[IndexC + 1] - Positions[IndexA + 1], Positions[IndexC + 2] - Positions[IndexA + 2]];
        const Normal =
        [
            EdgeAB[1] * EdgeAC[2] - EdgeAB[2] * EdgeAC[1],
            EdgeAB[2] * EdgeAC[0] - EdgeAB[0] * EdgeAC[2],
            EdgeAB[0] * EdgeAC[1] - EdgeAB[1] * EdgeAC[0]
        ];

        for (const Vertex of [IndexA, IndexB, IndexC])
        {
            Normals[Vertex + 0] += Normal[0];
            Normals[Vertex + 1] += Normal[1];
            Normals[Vertex + 2] += Normal[2];
        }
    }

    for (let Vertex = 0; Vertex < Normals.length; Vertex += 3)
    {
        const Length = Math.hypot(Normals[Vertex], Normals[Vertex + 1], Normals[Vertex + 2]) || 1.0;
        Normals[Vertex + 0] /= Length;
        Normals[Vertex + 1] /= Length;
        Normals[Vertex + 2] /= Length;
    }

    const VertexCount = Positions.length / 3;
    const Interleaved = new Float32Array(VertexCount * 9);

    for (let Vertex = 0; Vertex < VertexCount; Vertex++)
    {
        Interleaved[Vertex * 9 + 0] = Positions[Vertex * 3 + 0];
        Interleaved[Vertex * 9 + 1] = Positions[Vertex * 3 + 1];
        Interleaved[Vertex * 9 + 2] = Positions[Vertex * 3 + 2];
        Interleaved[Vertex * 9 + 3] = Normals[Vertex * 3 + 0];
        Interleaved[Vertex * 9 + 4] = Normals[Vertex * 3 + 1];
        Interleaved[Vertex * 9 + 5] = Normals[Vertex * 3 + 2];
        Interleaved[Vertex * 9 + 6] = Colors[Vertex * 3 + 0];
        Interleaved[Vertex * 9 + 7] = Colors[Vertex * 3 + 1];
        Interleaved[Vertex * 9 + 8] = Colors[Vertex * 3 + 2];
    }

    return {
        Label: 'VesselHull',
        Layout:
        [
            { Name: 'a_Position', Size: 3 },
            { Name: 'a_Normal', Size: 3 },
            { Name: 'a_Color', Size: 3 }
        ],
        Vertices: Interleaved,
        Indices: new Uint32Array(Indices),
        Category: 'triangles'
    };
}
