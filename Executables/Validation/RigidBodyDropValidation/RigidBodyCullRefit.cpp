/*============================================================================================================================================
                                                           RIGIDBODYCULLREFIT.CPP
============================================================================================================================================*/
// 🧩 See RigidBodyCullRefit.h. The record fit MIRRORS the engine's own (InstanceCullSubmission.cpp): the same column-major transform, the same
//    max-column-length radius scale. That duplication is deliberate — the engine's helpers are file-local statics, and this bed is the only caller
//    that needs them per frame, so the alternative is widening the engine's public surface for one validation target.
//
//    ⚠️ If the engine ever changes how it fits a record, this must change with it or the cull will test bounds the shader does not expect. The two
//       sites agree on one contract: PartitionCullRecord's world sphere must ENCLOSE the transformed mesh, and the cone cosine passes through
//       rotation unchanged.

#include "RigidBodyCullRefit.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace Frontier
{

namespace
{
    // Transform a mesh-local sphere by a column-major Model[16]: centre through the full transform, radius by the LARGEST column length. The max
    // (not the average) is what keeps the fit enclosing under non-uniform scale — the wrecker is a scaled cube, so this path is live here.
    void ProjectSphereThroughModel(const float Model[16], const float LocalSphere[4],
                                   float& CentreX, float& CentreY, float& CentreZ, float& Radius)
    {
        const float Cx = LocalSphere[0], Cy = LocalSphere[1], Cz = LocalSphere[2];
        // Element (col c, row r) sits at Model[c*4 + r]. World centre = Model * (centre, 1).
        CentreX = Model[0] * Cx + Model[4] * Cy + Model[8]  * Cz + Model[12];
        CentreY = Model[1] * Cx + Model[5] * Cy + Model[9]  * Cz + Model[13];
        CentreZ = Model[2] * Cx + Model[6] * Cy + Model[10] * Cz + Model[14];

        const auto ColumnLength = [&](int Column)
        {
            const float X = Model[Column * 4 + 0], Y = Model[Column * 4 + 1], Z = Model[Column * 4 + 2];
            return std::sqrt(X * X + Y * Y + Z * Z);
        };
        const float ScaleX = ColumnLength(0), ScaleY = ColumnLength(1), ScaleZ = ColumnLength(2);
        float MaxScale = ScaleX > ScaleY ? ScaleX : ScaleY;
        if (ScaleZ > MaxScale) MaxScale = ScaleZ;
        Radius = LocalSphere[3] * MaxScale;
    }

    // Rotate a local cone axis by Model's upper-left, then renormalize. The half-angle cosine is scale- and rotation-invariant, so it passes
    // through untouched — including the -1 that marks a record non-coneable.
    void ProjectConeThroughModel(const float Model[16], const float LocalCone[4],
                                 float& AxisX, float& AxisY, float& AxisZ, float& Cosine)
    {
        const float Ax = LocalCone[0], Ay = LocalCone[1], Az = LocalCone[2];
        float Rx = Model[0] * Ax + Model[4] * Ay + Model[8]  * Az;
        float Ry = Model[1] * Ax + Model[5] * Ay + Model[9]  * Az;
        float Rz = Model[2] * Ax + Model[6] * Ay + Model[10] * Az;
        const float Length = std::sqrt(Rx * Rx + Ry * Ry + Rz * Rz);
        if (Length > 1e-6f)
        {
            Rx /= Length; Ry /= Length; Rz /= Length;
        }
        AxisX  = Rx;
        AxisY  = Ry;
        AxisZ  = Rz;
        Cosine = LocalCone[3];
    }
}

void FitRigidBodyLocalBounds(const float* Positions, uint32_t VertexCount, uint32_t PositionStride, RigidBodyLocalBounds& Bounds)
{
    Bounds = RigidBodyLocalBounds{};
    if (Positions == nullptr || VertexCount == 0 || PositionStride < 3)
        return;

    // Centroid, then the furthest vertex from it. Not the minimal enclosing sphere, but it is what the engine uses and it is guaranteed enclosing,
    // which is the only property the cull depends on.
    double SumX = 0.0, SumY = 0.0, SumZ = 0.0;
    for (uint32_t Index = 0; Index < VertexCount; ++Index)
    {
        const float* Vertex = Positions + (size_t)Index * PositionStride;
        SumX += Vertex[0]; SumY += Vertex[1]; SumZ += Vertex[2];
    }
    const float CentreX = (float)(SumX / (double)VertexCount);
    const float CentreY = (float)(SumY / (double)VertexCount);
    const float CentreZ = (float)(SumZ / (double)VertexCount);

    float RadiusSquared = 0.0f;
    for (uint32_t Index = 0; Index < VertexCount; ++Index)
    {
        const float* Vertex = Positions + (size_t)Index * PositionStride;
        const float Dx = Vertex[0] - CentreX, Dy = Vertex[1] - CentreY, Dz = Vertex[2] - CentreZ;
        const float Distance = Dx * Dx + Dy * Dy + Dz * Dz;
        if (Distance > RadiusSquared)
            RadiusSquared = Distance;
    }

    Bounds.Sphere[0] = CentreX;
    Bounds.Sphere[1] = CentreY;
    Bounds.Sphere[2] = CentreZ;
    Bounds.Sphere[3] = std::sqrt(RadiusSquared);
    // Non-coneable: a closed box's normals span every direction, so no cone half-angle can reject a face without dropping a visible one.
    Bounds.Cone[0] = 0.0f; Bounds.Cone[1] = 0.0f; Bounds.Cone[2] = 1.0f; Bounds.Cone[3] = -1.0f;
}

// 🔴 The layout tripwire. This file mirrors the engine's record fit by hand, and the header's "keep the two in step" warning is only a comment — a
//    comment does not fail a build. A record gaining, losing, or reordering a field is the most likely way that mirror silently diverges, and the
//    symptom would be the cull testing bounds the shader never wrote. Sizing is the one part of the contract that CAN be asserted, so it is.
static_assert(sizeof(PartitionCullRecord) == 32,
              "PartitionCullRecord is no longer 8 floats: re-check ProjectSphereThroughModel / ProjectConeThroughModel against the engine's own fit "
              "in InstanceCullSubmission.cpp before adjusting this assert.");

void RefitRigidBodyCullRecords(InstanceCullSubmission&                  Cull,
                               RigidBodyCullMapping&                    Mapping,
                               const std::vector<SuzanneSceneInstance>& Instances,
                               const RigidBodyLocalBounds&              Bounds)
{
    if (!Cull.ReadyCondition || Cull.Host == nullptr || Cull.RecordMemory == VK_NULL_HANDLE)
        return;

    uint32_t Count = (uint32_t)Instances.size();
    if (Count > Cull.RecordCapacity)
        Count = Cull.RecordCapacity;
    if (Count == 0)
        return;

    // 📝 Map ONCE, on the first refit, and hold it. The allocation outlives every frame, so there is nothing to re-establish per frame.
    //    ⚠️ Map the CAPACITY, not the live count: the allocation was made at capacity, and mapping a sub-range would drag in nonCoherentAtomSize
    //       offset alignment. Mapping from zero sidesteps that entirely.
    if (Mapping.Records == nullptr)
    {
        const VkDeviceSize RecordCapacityBytes = (VkDeviceSize)Cull.RecordCapacity * sizeof(PartitionCullRecord);
        if (vkMapMemory(Cull.Host->Device, Cull.RecordMemory, 0, RecordCapacityBytes, 0, &Mapping.Records) != VK_SUCCESS)
        {
            Mapping.Records = nullptr;
            return;   // leave RecordCount alone: last frame's records are stale but valid, which beats culling the whole scene to nothing
        }
    }

    // 📝 Compose straight into mapped memory — no staging vector, so a move costs zero allocations. Each field is assigned exactly once and never
    //    read back, which is what keeps this safe over write-combined memory (see RigidBodyCullMapping's warning).
    PartitionCullRecord* Records = (PartitionCullRecord*)Mapping.Records;
    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        PartitionCullRecord& Record = Records[Index];
        ProjectSphereThroughModel(Instances[Index].Model, Bounds.Sphere,
                                  Record.SphereX, Record.SphereY, Record.SphereZ, Record.SphereRadius);
        ProjectConeThroughModel(Instances[Index].Model, Bounds.Cone,
                                Record.ConeAxisX, Record.ConeAxisY, Record.ConeAxisZ, Record.ConeCosine);
    }

    // No vkFlushMappedMemoryRanges: the allocation is HOST_COHERENT (see the header's note).
    Cull.RecordCount = Count;
}

void ReleaseRigidBodyCullMapping(InstanceCullSubmission& Cull, RigidBodyCullMapping& Mapping)
{
    if (Mapping.Records == nullptr || Cull.Host == nullptr || Cull.RecordMemory == VK_NULL_HANDLE)
    {
        Mapping.Records = nullptr;
        return;
    }
    vkUnmapMemory(Cull.Host->Device, Cull.RecordMemory);
    Mapping.Records = nullptr;
}

} // namespace Frontier
