//============================================================================================================================================
//                                                       RESERVOIRDUPLICATION.WGSL
//============================================================================================================================================
// 🧩 ReSTIR PT Enhanced §5 Sample Duplication Maps (Lin, Kettunen & Wyman 2026, PACMCGIT 9(1), Art. 13, doi:10.1145/3804494):
//    - Loads a 24×24 shared-memory tile of post-spatial PathSeeds per 8×8 workgroup (9 integer texel loads per lane, 2.25 KB LDS).
//    - Counts exact PathSeed collisions across the 17×17 neighborhood (288 neighbors): D = matches / 288.0.
//    - Encodes D into bits 24..31 of OutputReservoirs[Index].OutgoingAndMeta so next frame's temporal pass applies
//      c_Cap = lerp(c_Cap^Default, 1.0, D^0.1), suppressing spatial-temporal correlation blobs without extra texture fetches.
//    - Writes (D, ReconnectMode, FootprintViz, PairedDeltaViz) into DuplicationDiagnostic for live Inspector heatmaps.

//! include "FrameDescriptor.wgsl"
//! include "ReservoirRecords.wgsl"

@group(0) @binding(1) var PostSpatialMeta                        : texture_2d<u32>;
@group(0) @binding(2) var<storage, read_write> OutputReservoirs  : array<ReservoirRecord>;
@group(0) @binding(3) var DuplicationDiagnostic                  : texture_storage_2d<rgba16float, write>;

const HaloRadius : i32 = 8;
const TileWidth  : u32 = 24u;
const TileTexels : u32 = 576u;   // 24 × 24

var<workgroup> SharedSeeds : array<u32, 576>;

@compute @workgroup_size(8, 8)
fn ComputeDuplicationMap(
    @builtin(global_invocation_id) Invocation : vec3u,
    @builtin(local_invocation_id) LocalId     : vec3u,
    @builtin(local_invocation_index) LocalIdx : u32,
    @builtin(workgroup_id) WorkgroupId        : vec3u
)
{
    let Extent     = vec2i(Frame.TraceExtent);
    let TileOrigin = vec2i(WorkgroupId.xy * 8u) - vec2i(HaloRadius);

    // Cooperatively populate the 24×24 PathSeed tile (576 / 64 == 9 loads per thread)
    for (var Step = LocalIdx; Step < TileTexels; Step += 64u)
    {
        let TileY  = i32(Step / TileWidth);
        let TileX  = i32(Step - u32(TileY) * TileWidth);
        let ReadPx = TileOrigin + vec2i(TileX, TileY);
        var Seed   = 0u;
        if (all(ReadPx >= vec2i(0)) && all(ReadPx < Extent))
        {
            Seed = textureLoad(PostSpatialMeta, ReadPx, 0).x;
        }
        SharedSeeds[Step] = Seed;
    }

    workgroupBarrier();

    let TracePixel = vec2i(Invocation.xy);
    if (any(TracePixel >= Extent))
    {
        return;
    }

    let MetaTexel = textureLoad(PostSpatialMeta, TracePixel, 0);
    let OwnSeed   = MetaTexel.x;
    var DupScore  = 0.0;

    if (OwnSeed != 0u && EnhancedOptionEnabled(Frame, EnhancedDuplicationMap))
    {
        let CenterX = i32(LocalId.x) + HaloRadius;
        let CenterY = i32(LocalId.y) + HaloRadius;
        var Matches = 0u;

        for (var Dy = -HaloRadius; Dy <= HaloRadius; Dy += 1)
        {
            let RowOffset = u32(CenterY + Dy) * TileWidth;
            for (var Dx = -HaloRadius; Dx <= HaloRadius; Dx += 1)
            {
                if (Dx == 0 && Dy == 0)
                {
                    continue;
                }
                let NeighborSeed = SharedSeeds[RowOffset + u32(CenterX + Dx)];
                Matches += select(0u, 1u, NeighborSeed == OwnSeed);
            }
        }
        DupScore = clamp(f32(Matches) / 288.0, 0.0, 1.0);
    }

    // Write quantized 8-bit DuplicationScore into bits 24..31 of OutputReservoirs[Index].OutgoingAndMeta
    let Index    = u32(TracePixel.y * Extent.x + TracePixel.x);
    let DupByte  = u32(clamp(round(DupScore * 255.0), 0.0, 255.0)) & 0xFFu;
    let PrevMeta = OutputReservoirs[Index].OutgoingAndMeta & 0x00FFFFFFu;
    OutputReservoirs[Index].OutgoingAndMeta = PrevMeta | (DupByte << 24u);

    // Unpack diagnostic channels for Inspector visualization modes 11..13
    let MetaBits   = MetaTexel.y;
    let Flags      = (MetaBits >> 16u) & 15u;
    let ReconnectK = ((MetaBits >> 20u) & 3u) + 2u;
    let Extra      = unpack2x16float(MetaTexel.z);

    // Encode reconnection class: 0.25 = k=2 surface, 0.55 = k=2 sky/dir, 0.80 = k=3 glossy replay, 1.0 = §6.2.3 forced NEE
    var ReconnectClass = select(0.25, 0.80, ReconnectK >= 3u);
    if ((Flags & PathFlagDirectional) != 0u)
    {
        ReconnectClass = select(0.55, 0.85, ReconnectK >= 3u);
    }
    if ((Flags & PathFlagForcedNee) != 0u)
    {
        ReconnectClass = 1.0;
    }
    if (MetaTexel.w == 0u)
    {
        ReconnectClass = 0.0;
    }

    textureStore(DuplicationDiagnostic, TracePixel, vec4f(DupScore, ReconnectClass, Extra.x, Extra.y));
}
