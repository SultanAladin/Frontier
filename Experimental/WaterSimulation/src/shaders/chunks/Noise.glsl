// 🧩 Value noise, fbm, and gradient helpers shared by terrain, foam, spray, and the sky
float HashScalar(vec2 Coordinate)
{
    return fract(sin(dot(Coordinate, vec2(127.1, 311.7))) * 43758.5453123);
}

vec2 HashVector(vec2 Coordinate)
{
    return fract(sin(vec2(dot(Coordinate, vec2(127.1, 311.7)),
                          dot(Coordinate, vec2(269.5, 183.3)))) * 43758.5453123);
}

float ValueNoise(vec2 Coordinate)
{
    vec2 Cell = floor(Coordinate);
    vec2 Offset = fract(Coordinate);
    vec2 Eased = Offset * Offset * (3.0 - 2.0 * Offset);

    float CornerA = HashScalar(Cell);
    float CornerB = HashScalar(Cell + vec2(1.0, 0.0));
    float CornerC = HashScalar(Cell + vec2(0.0, 1.0));
    float CornerD = HashScalar(Cell + vec2(1.0, 1.0));

    return mix(mix(CornerA, CornerB, Eased.x), mix(CornerC, CornerD, Eased.x), Eased.y);
}

float FractalNoise(vec2 Coordinate, int OctaveCount)
{
    float Sum = 0.0;
    float Amplitude = 0.5;
    float Normalization = 0.0;
    vec2 Cursor = Coordinate;

    for (int Octave = 0; Octave < 6; Octave++)
    {
        if (Octave >= OctaveCount)
        {
            break;
        }

        Sum += Amplitude * ValueNoise(Cursor);
        Normalization += Amplitude;
        Cursor = Cursor * 2.03 + vec2(19.3, 7.7);
        Amplitude *= 0.5;
    }

    return Sum / max(Normalization, 0.0001);
}

vec2 FractalGradient(vec2 Coordinate, int OctaveCount, float Epsilon)
{
    float Center = FractalNoise(Coordinate, OctaveCount);
    float RightSample = FractalNoise(Coordinate + vec2(Epsilon, 0.0), OctaveCount);
    float UpSample = FractalNoise(Coordinate + vec2(0.0, Epsilon), OctaveCount);

    return vec2(RightSample - Center, UpSample - Center) / max(Epsilon, 0.0001);
}

// 💡 Cellular foam lattice: returns (cell distance, cell identity) for breaking foam texture
vec2 FoamLattice(vec2 Coordinate)
{
    vec2 Cell = floor(Coordinate);
    vec2 Offset = fract(Coordinate);
    float Nearest = 8.0;
    float Identity = 0.0;

    for (int Row = -1; Row <= 1; Row++)
    {
        for (int Column = -1; Column <= 1; Column++)
        {
            vec2 Neighbour = vec2(float(Column), float(Row));
            vec2 Point = HashVector(Cell + Neighbour);
            float Distance = length(Neighbour + Point - Offset);

            if (Distance < Nearest)
            {
                Nearest = Distance;
                Identity = HashScalar(Cell + Neighbour + 3.7);
            }
        }
    }

    return vec2(Nearest, Identity);
}

vec2 SwirlCoordinate(vec2 Coordinate, float Time, float Rate)
{
    float Swirl = FractalNoise(Coordinate * 0.35 + Time * 0.02, 3) * 2.0 - 1.0;
    float Angle = Swirl * Rate;
    float Cosine = cos(Angle);
    float Sine = sin(Angle);
    return mat2(Cosine, -Sine, Sine, Cosine) * Coordinate;
}
