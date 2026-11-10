//============================================================================================================================================
//                                                            PAIRINGSPACE.JS
//============================================================================================================================================
// 🧩 Reciprocal spatial-reuse neighbor pairing textures (Lin, Kettunen & Wyman 2026, "ReSTIR PT Enhanced", §3.1–3.2, doi:10.1145/3804494).
//    Constructs three independent tileable self-inverting Gaussian delta textures (254², 230², 210²) via staggered 2×2 block shuffles so
//    every spatial reuse pair (A, B) evaluates 2 path shifts instead of 4 while remaining free of N-rooks block-border structure.

export const PairingDimensions = Object.freeze([254, 230, 210]);
export const PairingOffsetBias = 256;   // [px] - encodes signed delta Δ ∈ [-127, 127] into unsigned rg16uint texel Δ + 256

//------------------------------------------------------------------------------------------------------------------------
//                                            GAUSSIAN SHUFFLE PARAMETERIZATION
//------------------------------------------------------------------------------------------------------------------------

// 📐 Matches the mean radial sample distance of an isotropic 2D Gaussian (σ·√(π/2)) to a uniform disk of radius R (2R/3):
//    σ = √(8 / (9π)) · R  (Paper §7: R = 30 px -> σ ≈ 16.0 px).
export function ResolveGaussianSigmaFromRadius(RadiusPixels)
{
    const ClampedRadius = Math.max(1.6, Number(RadiusPixels) || 30.0);
    return Math.max(0.8, Math.sqrt(8.0 / (9.0 * Math.PI)) * ClampedRadius);
}

// 📐 Equation (3) of Lin, Kettunen & Wyman (2026): tiled 2×2 shuffle count producing normal coordinate deltas of std-dev σ.
export function ResolveShuffleIterationCount(Sigma)
{
    const S = Math.max(0.8, Number(Sigma) || 16.0);
    return Math.max(1, Math.floor(
        0.5 * S * S + 1.46 / S + 1.76 / (S * S) + 0.656 / (S * S * S) + 0.5
    ));
}

function AdvanceDeterministicPrng(State)
{
    let Z = (State + 0x6D2B79F5) | 0;
    Z = Math.imul(Z ^ (Z >>> 15), Z | 1);
    Z ^= Z + Math.imul(Z ^ (Z >>> 7), Z | 61);
    return ((Z ^ (Z >>> 14)) >>> 0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SELF-INVERTING PAIRING MAP BUILDER
//------------------------------------------------------------------------------------------------------------------------

// 📐 Initializes a W×W even grid with consecutive link pairs (0 .. W²/2 − 1), applies n_σ staggered 2×2 random permutations
//    (alternating (0,0) and (1,1) toroidal offsets), and emits signed toroidal deltas Δ(A) = −Δ(B) so B = A + Δ(A) ⇔ A = B + Δ(B).
export function ConstructPairingMapCpu(Width, Sigma = 16.0, Seed = 1337)
{
    if (Width < 4 || (Width & 1) !== 0)
        throw new Error(`Pairing texture width must be an even integer >= 4; received ${Width}`);

    const PixelCount = Width * Width;
    const PairCount  = PixelCount >>> 1;
    const Links      = new Uint32Array(PixelCount);
    for (let Index = 0; Index < PixelCount; ++Index)
        Links[Index] = Index >>> 1;

    const Iterations = ResolveShuffleIterationCount(Sigma);
    const HalfWidth  = Width >>> 1;
    let Rng          = (Seed ^ Math.imul(Width, 0x9E3779B1)) >>> 0;

    for (let Pass = 0; Pass < Iterations; ++Pass)
    {
        const Stagger = Pass & 1;
        for (let BlockY = 0; BlockY < HalfWidth; ++BlockY)
        {
            const Y0   = (BlockY * 2 + Stagger) % Width;
            const Y1   = (Y0 + 1) % Width;
            const Row0 = Y0 * Width;
            const Row1 = Y1 * Width;
            for (let BlockX = 0; BlockX < HalfWidth; ++BlockX)
            {
                const X0 = (BlockX * 2 + Stagger) % Width;
                const X1 = (X0 + 1) % Width;
                const I0 = Row0 + X0;
                const I1 = Row0 + X1;
                const I2 = Row1 + X0;
                const I3 = Row1 + X1;

                let V0 = Links[I0], V1 = Links[I1], V2 = Links[I2], V3 = Links[I3];
                Rng = AdvanceDeterministicPrng(Rng);
                let Pick = Rng % 24;

                // Unrolled Fisher-Yates over 4 slots using one 0..23 uniform draw (4! = 24 exact permutations)
                const S4 = Pick & 3;
                Pick = (Pick / 4) | 0;
                if (S4 === 0) { const T = V3; V3 = V0; V0 = T; }
                else if (S4 === 1) { const T = V3; V3 = V1; V1 = T; }
                else if (S4 === 2) { const T = V3; V3 = V2; V2 = T; }

                const S3 = Pick % 3;
                const S2 = (Pick / 3) | 0;
                if (S3 === 0) { const T = V2; V2 = V0; V0 = T; }
                else if (S3 === 1) { const T = V2; V2 = V1; V1 = T; }
                if (S2 === 0) { const T = V1; V1 = V0; V0 = T; }

                Links[I0] = V0;
                Links[I1] = V1;
                Links[I2] = V2;
                Links[I3] = V3;
            }
        }
    }

    const FirstX = new Int16Array(PairCount).fill(-1);
    const FirstY = new Int16Array(PairCount).fill(-1);
    const Deltas = new Int16Array(PixelCount * 2);
    const Packed = new Uint16Array(PixelCount * 2);

    for (let Y = 0; Y < Width; ++Y)
    {
        for (let X = 0; X < Width; ++X)
        {
            const Pixel = Y * Width + X;
            const Link  = Links[Pixel];
            const Ax    = FirstX[Link];
            if (Ax < 0)
            {
                FirstX[Link] = X;
                FirstY[Link] = Y;
                continue;
            }
            const Ay = FirstY[Link];
            let Dx   = X - Ax;
            let Dy   = Y - Ay;
            if (Dx > HalfWidth)       Dx -= Width;
            else if (Dx < -HalfWidth) Dx += Width;
            if (Dy > HalfWidth)       Dy -= Width;
            else if (Dy < -HalfWidth) Dy += Width;

            const PixelA = Ay * Width + Ax;
            Deltas[PixelA * 2]     = Dx;
            Deltas[PixelA * 2 + 1] = Dy;
            Deltas[Pixel * 2]      = -Dx;
            Deltas[Pixel * 2 + 1]  = -Dy;

            Packed[PixelA * 2]     = Dx + PairingOffsetBias;
            Packed[PixelA * 2 + 1] = Dy + PairingOffsetBias;
            Packed[Pixel * 2]      = -Dx + PairingOffsetBias;
            Packed[Pixel * 2 + 1]  = -Dy + PairingOffsetBias;
        }
    }

    return { Width, Sigma, Iterations, Deltas, Packed };
}

//------------------------------------------------------------------------------------------------------------------------
//                                    PER-FRAME INVOLUTIVE DIHEDRAL TRANSFORM (§3.2)
//------------------------------------------------------------------------------------------------------------------------

// 📐 Builds per-frame dihedral symmetry (flipX, flipY, transpose) and toroidal offset O for one pairing map.
export function ResolvePairingTransform(FrameIndex, LayerIndex, Width)
{
    const Hash = AdvanceDeterministicPrng((Math.imul(FrameIndex + 1, 0x85EBCA6B) ^ Math.imul(LayerIndex + 1, 0xC2B2AE35)) >>> 0);
    const Symmetry  = Hash & 7;   // bit0: flipX, bit1: flipY, bit2: transpose
    const OffsetX   = ((Hash >>> 3) % Width + Width) % Width;
    const OffsetY   = ((Hash >>> 16) % Width + Width) % Width;
    return { OffsetX, OffsetY, Symmetry, Width };
}

// 📐 Exact CPU counterpart of the WGSL paired-neighbor lookup: u = (M(P) + O) mod W, δ = Tex(u), Q = P + M⁻¹(δ).
//    Guaranteed involutive on ℤ²: ResolvePairedNeighborCpu(Map, Transform, Q) === P for every P ∈ ℤ².
export function ResolvePairedNeighborCpu(Map, Transform, PixelX, PixelY)
{
    const W     = Map.Width;
    const FlipX = (Transform.Symmetry & 1) ? -1 : 1;
    const FlipY = (Transform.Symmetry & 2) ? -1 : 1;
    const Trans = (Transform.Symmetry & 4) !== 0;

    const Mx = Trans ? FlipX * PixelY : FlipX * PixelX;
    const My = Trans ? FlipY * PixelX : FlipY * PixelY;
    const Ux = ((Mx + Transform.OffsetX) % W + W) % W;
    const Uy = ((My + Transform.OffsetY) % W + W) % W;

    const Base = (Uy * W + Ux) * 2;
    const Dx   = Map.Deltas[Base];
    const Dy   = Map.Deltas[Base + 1];

    const OutDx = Trans ? FlipY * Dy : FlipX * Dx;
    const OutDy = Trans ? FlipX * Dx : FlipY * Dy;
    return [PixelX + OutDx, PixelY + OutDy];
}

//------------------------------------------------------------------------------------------------------------------------
//                                               GPU PAIRING SPACE HOST
//------------------------------------------------------------------------------------------------------------------------

export class PairingSpace
{
    constructor(Exchange)
    {
        this.Exchange      = Exchange;
        this.CurrentRadius = -1.0;
        this.Maps          = [];
        this.ArrayTexture  = Exchange
            ? Exchange.ConstructTexture({
                label:  "Pairing maps (254x254x3 rg16uint)",
                size:   [PairingDimensions[0], PairingDimensions[0], PairingDimensions.length],
                format: "rg16uint",
                usage:  GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST
            })
            : null;
        this.ArrayView     = this.ArrayTexture ? this.ArrayTexture.createView({ dimension: "2d-array" }) : null;
        this.UniformBuffer = Exchange
            ? Exchange.ConstructRecords(64, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST, "Pairing transforms")
            : null;
    }

    UpdateRadius(SpatialRadius = 30.0)
    {
        const Rounded = Math.max(2.0, Math.round(Number(SpatialRadius) || 30.0));
        if (Math.abs(Rounded - this.CurrentRadius) < 0.5 && this.Maps.length === 3)
            return false;

        this.CurrentRadius = Rounded;
        const Sigma = ResolveGaussianSigmaFromRadius(Rounded);
        this.Maps   = PairingDimensions.map((Dim, Layer) => ConstructPairingMapCpu(Dim, Sigma, 0x51A7E0 + Layer * 7919));

        if (!this.Exchange || !this.ArrayTexture)
            return true;

        const Device = this.Exchange.Device;
        this.Maps.forEach((Map, Layer) =>
        {
            const RowBytes    = Math.ceil((Map.Width * 4) / 256) * 256;
            const RowElements = RowBytes >>> 1;
            const Staged      = new Uint16Array(RowElements * Map.Width);
            for (let Y = 0; Y < Map.Width; ++Y)
                Staged.set(Map.Packed.subarray(Y * Map.Width * 2, (Y + 1) * Map.Width * 2), Y * RowElements);
            Device.queue.writeTexture(
                { texture: this.ArrayTexture, origin: [0, 0, Layer] },
                Staged,
                { bytesPerRow: RowBytes, rowsPerImage: Map.Width },
                [Map.Width, Map.Width, 1]
            );
        });
        return true;
    }

    EncodeFrameTransforms(FrameIndex)
    {
        if (!this.Exchange || !this.UniformBuffer)
            return;
        const Words = new Int32Array(16);
        for (let Layer = 0; Layer < 3; ++Layer)
        {
            const Width = PairingDimensions[Layer];
            const Xf    = ResolvePairingTransform(FrameIndex, Layer, Width);
            Words[Layer * 4 + 0] = Xf.OffsetX;
            Words[Layer * 4 + 1] = Xf.OffsetY;
            Words[Layer * 4 + 2] = Xf.Symmetry;
            Words[Layer * 4 + 3] = Width;
        }
        this.Exchange.Device.queue.writeBuffer(this.UniformBuffer, 0, Words);
    }
}
