/*==========================================================================================================================================
                                                      EMISSIVELIGHTSPACE.SLANG
                                     ------------------------------------------------------------------
                  Emissive surfaces as lights: the per-cell cluster lists, the exact-area
                  point sampler, and the partition of unity that keeps the combination of
                              the BSDF technique and the light technique unbiased.
==========================================================================================================================================*/

// 🧩 Emissive surfaces as lights: the Slang form of the emissive-sampling block of the demo's
//    RayTraversal.wgsl (BEGIN/END EMISSIVE SAMPLING), 1:1.
//
// 📝 An emitter point has two techniques: the BSDF ray that happens to hit it, and the light sample
//    below. The BSDF technique keeps the renderer's own heuristic weight w_b(ω)
//    (EvaluateEmissiveBsdfWeight) and the light technique takes 1 − w_b(ω). Wherever the light
//    technique is not available (feature off, not ray tracing, outside the grid, a cell with more
//    clusters than slots) the BSDF hit counts in full — any partition of unity is a valid combination,
//    and the shader tests membership with the BVH triangle index the host stored.

import LightClusterSpace;

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

public static const uint EmissiveEmptySlot       = 0x7FFFu;
public static const uint EmissiveSlotCount       = 8u;
public static const uint4 EmissiveUnusable       = uint4(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
public static const uint EmissiveSamplesPerVertex = 2u;

// 📝 The occupant table and the ray-traced materials the sampler reads. The host binds the same tables
//    the traversal does: one record per occupant (LocalToWorld and roles), one per material family.
public struct OccupantRecord
{
    public float4x4 LocalToWorld;
    public uint4    Roles;
};

public struct RayMaterialRecord
{
    public float4 Emission;
};

[[vk::binding(0, 2)]] public StructuredBuffer<OccupantRecord> Occupants;
[[vk::binding(3, 2)]] public ConstantBuffer<uint4> EmissiveCounts;   // x = number of occupant records
[[vk::binding(1, 2)]] public StructuredBuffer<RayMaterialRecord> RayMaterials;

// 📝 The emission scale the host's slider supplies, and the feature word that gates the structure.
public struct EmissiveFeatureRecord
{
    public float EmissionScale;
    public uint  Features2;
};

[[vk::binding(2, 2)]] public ConstantBuffer<EmissiveFeatureRecord> EmissiveFeatures;

public static const uint FeatureRayTracing = 8192u;

public static const uint RoleEmissiveCluster = 64u;

// 📝 The number of occupant records bound, from the host (Slang's StructuredBuffer::GetDimensions needs an
//    overload the SPIR-V target does not carry, so the count arrives as a constant instead).
public uint OccupantCount() { return EmissiveCounts.x; }

public bool FeatureEnabled(uint Bit) { return (EmissiveFeatures.Features2 & Bit) != 0u; }

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE EMITTER SAMPLE
//------------------------------------------------------------------------------------------------------------------------

public struct EmissiveSample
{
    public bool   Valid;
    public float3 Position;   // [m]  world point on the emitter
    public float3 Normal;     // [-]  its geometric normal (emitters shine from both sides)
    public float3 Radiance;   // [-]  emitted radiance, colour × luminance × the emission scale
    public float  Pdf;        // [1/m²] density of choosing this point (cluster × triangle × uniform on the triangle)
};

public float ResolveLuminance(float3 Colour)
{
    return dot(Colour, float3(0.2126, 0.7152, 0.0722));
}

// 📝 The cell's cluster ids at a world position, or EmissiveUnusable (the feature is off, not ray
//    tracing, no clusters, or the point lies outside the grid): then there is no light technique at
//    that vertex and every emitter seen from it counts in full.
public uint4 ResolveEmissiveCell(float3 Position)
{
    if (!ExtendedEnabled(ExtendedEmissiveLights) || !FeatureEnabled(FeatureRayTracing) || SceneLights.Extent.w < 0.5)
        return EmissiveUnusable;

    float3 Local = (Position - SceneLights.Minimum.xyz) / SceneLights.Extent.xyz;
    if (any(Local < float3(0.0)) || any(Local >= float3(1.0)))
        return EmissiveUnusable;

    uint3 Cell = min(uint3(Local * float3(LightClusterResolution)), LightClusterResolution - uint3(1u));
    return SceneLights.EmissiveCells[Cell.x + LightClusterResolution.x * (Cell.y + LightClusterResolution.y * Cell.z)];
}

public bool EmissiveUsable(uint4 Words)
{
    return Words.x != 0xFFFFFFFFu;
}

public uint ResolveEmissiveSlot(uint4 Words, uint Slot)
{
    uint Pair = Words[Slot >> 1u];
    return ((Slot & 1u) != 0u) ? ((Pair >> 16u) & 0x7FFFu) : (Pair & 0x7FFFu);
}

// 📝 Is this traced hit an emitter the table represents (its occupant carries RoleEmissiveCluster)?
public bool IsTableEmitter(uint Identity)
{
    return Identity < OccupantCount() && (Occupants[Identity].Roles.x & RoleEmissiveCluster) != 0u;
}

// 📐 Can this cell's light technique draw the traced hit (occupant, BVH triangle)? Exactly when the
//    triangle is in a cluster the cell lists. The BSDF technique takes the partition's w_b at such a
//    hit and weight 1 at every other emitter.
public bool IsListedEmitter(uint4 Words, uint Identity, uint Triangle)
{
    if (!EmissiveUsable(Words) || !IsTableEmitter(Identity))
        return false;

    for (uint Slot = 0u; Slot < EmissiveSlotCount; ++Slot)
    {
        uint Id = ResolveEmissiveSlot(Words, Slot);
        if (Id == EmissiveEmptySlot)
            break;
        EmissiveClusterRecord Cluster = SceneLights.EmissiveClusters[Id];
        if (Cluster.Meta.x != Identity)
            continue;
        for (uint Index = 0u; Index < Cluster.Meta.z; ++Index)
        {
            if (asuint(SceneLights.EmissiveTriangles[Cluster.Meta.y + Index].A.w) == Triangle)
                return true;
        }
    }
    return false;
}

// 📝 The conservative occupant-level version, for screen strikes (which know the occupant but not the
//    triangle): true when ANY cluster of the occupant is listed. Treating a strike as listed when it
//    need not be only sends that ray down the BVH path instead of the screen, which is always correct.
public bool IsListedOccupant(uint4 Words, uint Identity)
{
    if (!EmissiveUsable(Words) || !IsTableEmitter(Identity))
        return false;

    for (uint Slot = 0u; Slot < EmissiveSlotCount; ++Slot)
    {
        uint Id = ResolveEmissiveSlot(Words, Slot);
        if (Id == EmissiveEmptySlot)
            break;
        if (SceneLights.EmissiveClusters[Id].Meta.x == Identity)
            return true;
    }
    return false;
}

public float ResolveEmissiveTriangleWeight(EmissiveTriangleRecord Triangle)
{
    return Triangle.E1.w * ResolveLuminance(RayMaterials[asuint(Triangle.E2.w)].Emission.rgb);
}

// 📐 The heuristic weight the path tracer has always given the BSDF technique at an emissive hit: a
//    function of the BSDF's pdf for the direction and of the emitter's geometry (squared distance,
//    cosine at the emitter) and of nothing else. Defined by the pair (vertex, emitter point), so the
//    light technique can evaluate the very same number.
public float EvaluateEmissiveBsdfWeight(float BsdfPdf, float Distance, float CosineAtEmitter)
{
    float DistSq = max(Distance * Distance, 0.04);
    float GeomTerm = max(CosineAtEmitter, 0.05) / DistSq;
    return clamp((BsdfPdf * BsdfPdf) / max(BsdfPdf * BsdfPdf + GeomTerm * GeomTerm * 0.01, 1.0e-5), 0.15, 1.0);
}

// 📐 Draws one point on the emitters of a cell: a cluster in proportion to power / (d² + r²), a
//    triangle in proportion to area × luminance, a uniform point on it. R carries four uniforms
//    (cluster, triangle, the two barycentric coordinates).
public EmissiveSample SampleEmissiveLight(float3 Position, uint4 Words, float4 R)
{
    EmissiveSample Result;
    Result.Valid = false;
    Result.Position = float3(0.0);
    Result.Normal = float3(0.0, 0.0, 1.0);
    Result.Radiance = float3(0.0);
    Result.Pdf = 0.0;

    float Weights[8];
    float Total = 0.0;
    uint Count = 0u;
    for (uint Slot = 0u; Slot < EmissiveSlotCount; ++Slot)
    {
        uint Id = ResolveEmissiveSlot(Words, Slot);
        if (Id == EmissiveEmptySlot)
            break;
        EmissiveClusterRecord Cluster = SceneLights.EmissiveClusters[Id];
        float3 Toward = Cluster.CentreReach.xyz - Position;
        float Weight = max(asfloat(Cluster.Meta.w), 0.0)
                     / (dot(Toward, Toward) + Cluster.CentreReach.w * Cluster.CentreReach.w + 1.0e-4);
        Weights[Slot] = Weight;
        Total += Weight;
        Count += 1u;
    }
    if (Count == 0u || !(Total > 0.0))
        return Result;

    uint Chosen = Count - 1u;
    float Running = 0.0;
    float Pick = R.x * Total;
    for (uint Slot = 0u; Slot < Count; ++Slot)
    {
        Running += Weights[Slot];
        if (Pick < Running)
        {
            Chosen = Slot;
            break;
        }
    }

    float ClusterProbability = Weights[Chosen] / Total;
    EmissiveClusterRecord Cluster = SceneLights.EmissiveClusters[ResolveEmissiveSlot(Words, Chosen)];
    uint First = Cluster.Meta.y;
    uint Number = Cluster.Meta.z;

    float TriangleTotal = 0.0;
    for (uint Index = 0u; Index < Number; ++Index)
        TriangleTotal += ResolveEmissiveTriangleWeight(SceneLights.EmissiveTriangles[First + Index]);
    if (!(TriangleTotal > 0.0))
        return Result;

    EmissiveTriangleRecord Triangle = SceneLights.EmissiveTriangles[First + Number - 1u];
    float TriangleWeight = ResolveEmissiveTriangleWeight(Triangle);
    float Cumulative = 0.0;
    float TrianglePick = R.y * TriangleTotal;
    for (uint Index = 0u; Index < Number; ++Index)
    {
        EmissiveTriangleRecord Candidate = SceneLights.EmissiveTriangles[First + Index];
        float Weight = ResolveEmissiveTriangleWeight(Candidate);
        Cumulative += Weight;
        if (TrianglePick < Cumulative)
        {
            Triangle = Candidate;
            TriangleWeight = Weight;
            break;
        }
    }

    float Root = sqrt(R.z);
    float B1 = Root * (1.0 - R.w);
    float B2 = Root * R.w;

    float4x4 ToWorld = Occupants[Cluster.Meta.x].LocalToWorld;
    float3 Point = mul(ToWorld, float4(Triangle.A.xyz + Triangle.E1.xyz * B1 + Triangle.E2.xyz * B2, 1.0)).xyz;
    float3 Edge1 = mul(ToWorld, float4(Triangle.E1.xyz, 0.0)).xyz;
    float3 Edge2 = mul(ToWorld, float4(Triangle.E2.xyz, 0.0)).xyz;
    float3 Cross = cross(Edge1, Edge2);
    float Area = 0.5 * length(Cross);
    if (!(Area > 1.0e-9))
        return Result;

    Result.Valid = true;
    Result.Position = Point;
    Result.Normal = Cross / (2.0 * Area);
    Result.Radiance = RayMaterials[asuint(Triangle.E2.w)].Emission.rgb * EmissiveFeatures.EmissionScale;
    Result.Pdf = ClusterProbability * (TriangleWeight / TriangleTotal) / Area;
    return Result;
}

// 📐 The light technique's share of one emitter sample, ready to merge into the same resampling as the
//    clustered lights so a single shadow ray serves the lot. CosSurface is the cosine at the receiver;
//    the returned radiance is already divided by the sample's pdf and the sample count.
public float3 EvaluateEmissiveContribution(EmissiveSample Emitter,
                                           float3 Position,
                                           float3 Normal,
                                           float BsdfPdf,
                                           float Samples)
{
    float3 Toward = Emitter.Position - Position;
    float Dist = length(Toward);
    float3 LDir = Toward / max(Dist, 1.0e-4);
    float CosSurface = dot(Normal, LDir);
    if (CosSurface <= 1.0e-4)
        return float3(0.0);
    float CosEmitter = abs(dot(Emitter.Normal, LDir));
    float BsdfWeight = EvaluateEmissiveBsdfWeight(BsdfPdf, Dist, CosEmitter);
    return Emitter.Radiance * (CosEmitter * CosSurface * (1.0 - BsdfWeight)
                               / (max(Dist * Dist, 1.0e-4) * max(Emitter.Pdf, 1.0e-8) * max(Samples, 1.0)));
}
