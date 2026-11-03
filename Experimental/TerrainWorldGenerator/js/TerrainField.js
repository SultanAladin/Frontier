/*====================================================================================================
                                      TERRAINFIELD.JS
====================================================================================================*/
// 🧩 SDF height-field evaluation, continent stamps, ocean gulfs, and sculpt brushes
//
// The world is an 8 km flat continent surrounded by ocean. Elevation is sampled from a
// signed-distance-field composition (fractal plates + continent stamps + gulf cut-outs)
// into a height map, which the renderer displaces a mesh from. Sculpt brushes edit the
// height map directly; every edit is re-uploaded as the terrain displacement texture.
// All quantities below are SI unless unit-tagged otherwise.

// [m] - Half side length of the block-out world (total span 8 km).
const WorldHalfSideM = 4000.0;
// [m] - Distance between adjacent height-map texels.
const HeightMapTexelM = 8.0;
// [idx] - Height-map resolution (square).
const HeightMapSize = 1024;
// [-] - Default PRNG seed for the procedural continent layout.
const DefaultSeed = 0xC0FFEE;
// [m] - Absolute elevation clamp.
const ElevationMinM = -2048.0;
const ElevationMaxM = 2048.0;

// [unitless] - Seeded 32-bit xorshift stream, returns [0, 1).
function CreateSeededRandom(Seed)
{
    let State = Seed | 0;
    return () =>
    {
        State ^= State << 13;
        State ^= State >>> 17;
        State ^= State << 5;
        return (State >>> 0) / 4294967296.0;
    };
}

// [unitless] - 2D simplex noise (Gustavson), roughly [-1, 1].
function CreateSimplexNoise(Seed)
{
    const Permutation = new Uint8Array(512);
    const Random = CreateSeededRandom(Seed);
    for (let I = 0; I < 256; I++) Permutation[I] = I;
    for (let I = 255; I > 0; I--)
    {
        const J = Math.floor(Random() * (I + 1));
        const Temporary = Permutation[I];
        Permutation[I] = Permutation[J];
        Permutation[J] = Temporary;
    }
    for (let I = 0; I < 256; I++) Permutation[256 + I] = Permutation[I];

    // [-] - Gradient directions for the 2D simplex.
    const Gradients =
    [
        [1, 1], [-1, 1], [1, -1], [-1, -1],
        [1, 0], [-1, 0], [0, 1], [0, -1]
    ];

    const Noise = (Xin, Yin) =>
    {
        const F2 = 0.3660254037844386; // (sqrt(3) - 1) / 2
        const G2 = 0.2113248654051871; // (3 - sqrt(3)) / 6
        const Stretch = (Xin + Yin) * F2;
        const I = Math.floor(Xin + Stretch);
        const J = Math.floor(Yin + Stretch);
        const Shrink = (I + J) * G2;
        const X0 = Xin - (I - Shrink);
        const Y0 = Yin - (J - Shrink);
        let I1 = 0;
        let J1 = 0;
        if (X0 > Y0) { I1 = 1; } else { J1 = 1; }
        const X1 = X0 - I1 + G2;
        const Y1 = Y0 - J1 + G2;
        const X2 = X0 - 1.0 + 2.0 * G2;
        const Y2 = Y0 - 1.0 + 2.0 * G2;

        const Ii = I & 255;
        const Jj = J & 255;
        const Gi0 = Gradients[Permutation[Ii + Permutation[Jj]] & 7];
        const Gi1 = Gradients[Permutation[Ii + I1 + Permutation[Jj + J1]] & 7];
        const Gi2 = Gradients[Permutation[Ii + 1 + Permutation[Jj + 1]] & 7];

        let N0 = 0.0;
        let T0 = 0.5 - X0 * X0 - Y0 * Y0;
        if (T0 > 0.0) { T0 *= T0; N0 = T0 * T0 * (Gi0[0] * X0 + Gi0[1] * Y0); }
        let N1 = 0.0;
        let T1 = 0.5 - X1 * X1 - Y1 * Y1;
        if (T1 > 0.0) { T1 *= T1; N1 = T1 * T1 * (Gi1[0] * X1 + Gi1[1] * Y1); }
        let N2 = 0.0;
        let T2 = 0.5 - X2 * X2 - Y2 * Y2;
        if (T2 > 0.0) { T2 *= T2; N2 = T2 * T2 * (Gi2[0] * X2 + Gi2[1] * Y2); }
        return 40.0 * (N0 + N1 + N2);
    };

    return { Noise };
}

// [-] - Fractal Brownian Motion over simplex noise. Returns roughly [0, 1].
function Fbm(Noise, X, Z, Octaves)
{
    let Amplitude = 1.0;
    let Frequency = 0.05;
    let Sum = 0.0;
    let MaxAmplitude = 0.0;
    for (let O = 0; O < Octaves; O++)
    {
        Sum += Amplitude * Noise.Noise(X * Frequency, Z * Frequency);
        MaxAmplitude += Amplitude;
        Amplitude *= 0.5;
        Frequency *= 2.0;
    }
    return Sum / MaxAmplitude;
}

function Mix(A, B, T) { return A + (B - A) * T; }

function Smoothstep(Edge0, Edge1, X)
{
    const T = Math.min(1.0, Math.max(0.0, (X - Edge0) / (Edge1 - Edge0)));
    return T * T * (3.0 - 2.0 * T);
}

// [m] - 2D ellipse signed distance, approximate but stable for block-out stamps.
function SdEllipse(P, Ab)
{
    const X = Math.abs(P[0]);
    const Z = Math.abs(P[1]);
    const Qx = X / Ab[0];
    const Qz = Z / Ab[1];
    const L = Math.hypot(Qx, Qz) - 1.0;
    const M = Math.min(Qx, Qz) / Math.max(Qx, Qz);
    return L * Math.max(Qx, Qz) * (0.5 + 0.5 * M);
}

// [-] - 1.0 at the ellipse centre, 0.0 one unit outside.
function EllipseWeight(Nx, Nz, Cx, Cz, Rx, Rz, Rotation)
{
    const Cos = Math.cos(Rotation);
    const Sin = Math.sin(Rotation);
    const Dx = (Nx - Cx) * Cos + (Nz - Cz) * Sin;
    const Dz = -(Nx - Cx) * Sin + (Nz - Cz) * Cos;
    const Sd = SdEllipse([Dx, Dz], [Rx, Rz]);
    return 1.0 - Math.min(Math.max(Sd, 0.0), 1.0);
}

// [-] - Africa-like continental block-out: a coarse union with a horn, west bulge, and cape.
function AfricaStamp(Noise, Nx, Nz)
{
    let Weight = 0.0;
    Weight = Math.max(Weight, EllipseWeight(Nx, Nz, -0.10, 0.00, 0.72, 0.52, 0.15));
    Weight = Math.max(Weight, EllipseWeight(Nx, Nz, -0.55, 0.05, 0.42, 0.30, -0.25));
    Weight = Math.max(Weight, EllipseWeight(Nx, Nz, 0.55, -0.15, 0.24, 0.20, 0.55));
    Weight = Math.max(Weight, EllipseWeight(Nx, Nz, 0.12, 0.62, 0.30, 0.22, -0.40));
    return Weight;
}

// [-] - Gulf cut-outs carved into the continental margin; the ocean plane fills them.
function GulfStamp(Nx, Nz)
{
    let Gulf = 0.0;
    Gulf = Math.max(Gulf, EllipseWeight(Nx, Nz, 0.62, -0.28, 0.32, 0.24, 0.0));
    Gulf = Math.max(Gulf, EllipseWeight(Nx, Nz, -0.85, 0.22, 0.28, 0.20, 0.15));
    return Gulf;
}

class TerrainField
{
    constructor()
    {
        this.WorldHalfSideM = WorldHalfSideM;
        this.TexelM = HeightMapTexelM;
        this.Size = HeightMapSize;
        this.SeaLevelM = 12.0;
        this.Heights = new Float32Array(this.Size * this.Size);
        this.Noise = CreateSimplexNoise(DefaultSeed);
        this.Dirty = true;
    }

    // [idx] - Texel coordinates for a world position.
    WorldToTexel(WorldX, WorldZ)
    {
        const Tx = (WorldX + this.WorldHalfSideM) / (2.0 * this.WorldHalfSideM) * (this.Size - 1);
        const Ty = (this.WorldHalfSideM - WorldZ) / (2.0 * this.WorldHalfSideM) * (this.Size - 1);
        return [Tx, Ty];
    }

    // [m] - Bilinearly sampled elevation at a world position.
    SampleBilinear(WorldX, WorldZ)
    {
        const Texel = this.WorldToTexel(WorldX, WorldZ);
        const Tx = Texel[0];
        const Ty = Texel[1];
        const X0 = Math.floor(Tx);
        const Y0 = Math.floor(Ty);
        const Fx = Tx - X0;
        const Fy = Ty - Y0;
        const Get = (X, Y) =>
        {
            if (X < 0 || X >= this.Size || Y < 0 || Y >= this.Size) return 0.0;
            return this.Heights[Y * this.Size + X];
        };
        const H00 = Get(X0, Y0);
        const H10 = Get(X0 + 1, Y0);
        const H01 = Get(X0, Y0 + 1);
        const H11 = Get(X0 + 1, Y0 + 1);
        return Mix(Mix(H00, H10, Fx), Mix(H01, H11, Fx), Fy);
    }

    // [m] - Procedural elevation from the SDF composition (no sculpt state applied).
    EvaluateProceduralHeight(WorldX, WorldZ)
    {
        const Nx = WorldX / this.WorldHalfSideM;
        const Nz = WorldZ / this.WorldHalfSideM;
        const Plate = Fbm(this.Noise, Nx * 1.1, Nz * 1.1, 4);
        const Mask = Smoothstep(0.30, 0.52, Plate);
        const Stamp = AfricaStamp(this.Noise, Nx, Nz);
        const Gulf = GulfStamp(Nx, Nz);
        const Land = Math.max(Mask, Stamp) * (1.0 - Gulf * 0.95);

        let Height;
        if (Land > 0.5)
        {
            const Mountain = Fbm(this.Noise, Nx * 11.0, Nz * 11.0, 5);
            const Foothills = Fbm(this.Noise, Nx * 44.0, Nz * 44.0, 3);
            Height = 35.0 + Mountain * Mountain * 700.0 + Foothills * 45.0;
        }
        else
        {
            const Depth = Fbm(this.Noise, Nx * 5.0, Nz * 5.0, 4);
            Height = -25.0 - Depth * 1600.0;
        }
        return Math.max(ElevationMinM, Math.min(ElevationMaxM, Height));
    }

    // Regenerate the height map from the SDF composition, discarding sculpt edits.
    Regenerate(Seed = DefaultSeed)
    {
        this.NoiseSeed = Seed;
        this.Noise = CreateSimplexNoise(Seed);
        const Stride = this.Size - 1;
        for (let Ty = 0; Ty < this.Size; Ty++)
        {
            const Nz = (Ty / Stride - 0.5) * 2.0;
            const WorldZ = Nz * this.WorldHalfSideM;
            for (let Tx = 0; Tx < this.Size; Tx++)
            {
                const Nx = (Tx / Stride - 0.5) * 2.0;
                const WorldX = Nx * this.WorldHalfSideM;
                this.Heights[Ty * this.Size + Tx] = this.EvaluateProceduralHeight(WorldX, WorldZ);
            }
        }
        this.Dirty = true;
    }

    // [-] - Sculpt brush edit applied directly to the height map (metres per stamp).
    // Mode is one of: 'Raise', 'Lower', 'Flatten', 'Smooth'.
    ApplySculptBrush(WorldX, WorldZ, RadiusM, StrengthM, Mode)
    {
        const Texel = this.WorldToTexel(WorldX, WorldZ);
        const CenterTx = Texel[0];
        const CenterTy = Texel[1];
        const RadiusTex = RadiusM / this.TexelM;
        const X0 = Math.max(0, Math.floor(CenterTx - RadiusTex));
        const X1 = Math.min(this.Size - 1, Math.ceil(CenterTx + RadiusTex));
        const Y0 = Math.max(0, Math.floor(CenterTy - RadiusTex));
        const Y1 = Math.min(this.Size - 1, Math.ceil(CenterTy + RadiusTex));

        for (let Ty = Y0; Ty <= Y1; Ty++)
        {
            for (let Tx = X0; Tx <= X1; Tx++)
            {
                const Dx = Tx - CenterTx;
                const Dy = Ty - CenterTy;
                const D = Math.hypot(Dx, Dy);
                if (D > RadiusTex) continue;
                const T = 1.0 - D / RadiusTex;
                const Falloff = T * T * (3.0 - 2.0 * T);
                const Idx = Ty * this.Size + Tx;
                const Old = this.Heights[Idx];
                let Next = Old;
                if (Mode === 'Raise')
                {
                    Next = Old + StrengthM * Falloff;
                }
                else if (Mode === 'Lower')
                {
                    Next = Old - StrengthM * Falloff;
                }
                else if (Mode === 'Flatten')
                {
                    Next = Mix(Old, this.SeaLevelM - 3.0, StrengthM * Falloff);
                }
                else if (Mode === 'Smooth')
                {
                    let Sum = 0.0;
                    let Count = 0;
                    for (let Oy = -1; Oy <= 1; Oy++)
                    {
                        for (let Ox = -1; Ox <= 1; Ox++)
                        {
                            const Nx2 = Tx + Ox;
                            const Ny2 = Ty + Oy;
                            if (Nx2 >= 0 && Nx2 < this.Size && Ny2 >= 0 && Ny2 < this.Size)
                            {
                                Sum += this.Heights[Ny2 * this.Size + Nx2];
                                Count++;
                            }
                        }
                    }
                    Next = Mix(Old, Sum / Count, StrengthM * Falloff);
                }
                this.Heights[Idx] = Math.max(ElevationMinM, Math.min(ElevationMaxM, Next));
            }
        }
        this.Dirty = true;
    }

    // [Uint8ClampedArray] - RGBA preview of the height map for the editor mini-map.
    BuildColorPreview(Width, Height)
    {
        const Pixels = new Uint8ClampedArray(Width * Height * 4);
        const StepX = this.Size / Width;
        const StepY = this.Size / Height;
        for (let Py = 0; Py < Height; Py++)
        {
            for (let Px = 0; Px < Width; Px++)
            {
                const Tx = Math.floor(Px * StepX);
                const Ty = Math.floor(Py * StepY);
                const H = this.Heights[Ty * this.Size + Tx];
                let R = 0, G = 0, B = 0;
                if (H < this.SeaLevelM)
                {
                    const T = Math.min(1.0, Math.max(0.0, (this.SeaLevelM - H) / 600.0));
                    R = 8 + Math.round(40 * T);
                    G = 40 + Math.round(90 * T);
                    B = 100 + Math.round(100 * T);
                }
                else if (H < 45.0)
                {
                    R = 190; G = 175; B = 130;
                }
                else if (H < 220.0)
                {
                    R = 110; G = 155; B = 90;
                }
                else if (H < 520.0)
                {
                    R = 140; G = 132; B = 120;
                }
                else
                {
                    R = 220; G = 225; B = 235;
                }
                const Offset = (Py * Width + Px) * 4;
                Pixels[Offset] = R;
                Pixels[Offset + 1] = G;
                Pixels[Offset + 2] = B;
                Pixels[Offset + 3] = 255;
            }
        }
        return Pixels;
    }
}

export { TerrainField, WorldHalfSideM, HeightMapTexelM, HeightMapSize, DefaultSeed };
