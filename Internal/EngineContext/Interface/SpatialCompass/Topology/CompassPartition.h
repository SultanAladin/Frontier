/*==============================================================================================================================================
                                                            COMPASSPARTITION.H
==============================================================================================================================================*/
// 🧩 The six face patches that make up the cube, constructed once. Ports the mockup's six `.f-*` transforms: each face is the 96px quad pushed
//    ±CubeHalfExtent along one axis (front +Z, back -Z, right +X, left -X, top +Y, bottom -Y). Edge + corner patches were removed from the mockup
//    (they were the stray floating rectangles), so the partition is faces-only. Header-only; the .cpp that orchestrates the overlay fills a
//    CompassPartition once at initialize and re-projects it each cycle.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_COMPASSPARTITION_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_SPATIALCOMPASS_COMPASSPARTITION_H

#include "SurfacePatchDefinition.h"
#include "../Configuration/CompassConfiguration.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The cube decomposed into its six clickable faces. FaceCount is fixed at six; the array carries the definitions.
struct CompassPartition
{
    static const int FaceCount = 6;         // [-] - The six named faces (edges/corners removed per the mockup)
    SurfacePatchDefinition Face[FaceCount]; // [-] - The face patches, in AlignmentPreset order Front..Bottom
};


//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Build the six face patches in cube space from the configured half-extent. Reproduces the mockup's `.f-*` placements: a
//    face is a HalfExtent-square quad whose plane sits HalfExtent out along its axis, wound CCW as seen from outside.
inline void InitializeCompassPartition(CompassPartition& Partition, const CompassConfiguration& Configuration)
{
    const float Half = Configuration.CubeHalfExtent;

    // Each face: preset, outward normal, then four CCW corners on the plane at +Half along that normal.
    // Corners are listed so the winding is counter-clockwise when the face is viewed from outside the cube.

    // FRONT  (+Z) — the mockup's default-facing face
    Partition.Face[0].Preset        = AlignmentPreset::Front;
    Partition.Face[0].OutwardNormal = CubeSpacePoint{  0.0f,  0.0f,  1.0f };
    Partition.Face[0].Corner[0]     = CubeSpacePoint{ -Half, -Half,  Half };
    Partition.Face[0].Corner[1]     = CubeSpacePoint{  Half, -Half,  Half };
    Partition.Face[0].Corner[2]     = CubeSpacePoint{  Half,  Half,  Half };
    Partition.Face[0].Corner[3]     = CubeSpacePoint{ -Half,  Half,  Half };

    // BACK   (-Z)
    Partition.Face[1].Preset        = AlignmentPreset::Back;
    Partition.Face[1].OutwardNormal = CubeSpacePoint{  0.0f,  0.0f, -1.0f };
    Partition.Face[1].Corner[0]     = CubeSpacePoint{  Half, -Half, -Half };
    Partition.Face[1].Corner[1]     = CubeSpacePoint{ -Half, -Half, -Half };
    Partition.Face[1].Corner[2]     = CubeSpacePoint{ -Half,  Half, -Half };
    Partition.Face[1].Corner[3]     = CubeSpacePoint{  Half,  Half, -Half };

    // RIGHT  (+X)
    Partition.Face[2].Preset        = AlignmentPreset::Right;
    Partition.Face[2].OutwardNormal = CubeSpacePoint{  1.0f,  0.0f,  0.0f };
    Partition.Face[2].Corner[0]     = CubeSpacePoint{  Half, -Half,  Half };
    Partition.Face[2].Corner[1]     = CubeSpacePoint{  Half, -Half, -Half };
    Partition.Face[2].Corner[2]     = CubeSpacePoint{  Half,  Half, -Half };
    Partition.Face[2].Corner[3]     = CubeSpacePoint{  Half,  Half,  Half };

    // LEFT   (-X)
    Partition.Face[3].Preset        = AlignmentPreset::Left;
    Partition.Face[3].OutwardNormal = CubeSpacePoint{ -1.0f,  0.0f,  0.0f };
    Partition.Face[3].Corner[0]     = CubeSpacePoint{ -Half, -Half, -Half };
    Partition.Face[3].Corner[1]     = CubeSpacePoint{ -Half, -Half,  Half };
    Partition.Face[3].Corner[2]     = CubeSpacePoint{ -Half,  Half,  Half };
    Partition.Face[3].Corner[3]     = CubeSpacePoint{ -Half,  Half, -Half };

    // TOP    (+Y)
    Partition.Face[4].Preset        = AlignmentPreset::Top;
    Partition.Face[4].OutwardNormal = CubeSpacePoint{  0.0f,  1.0f,  0.0f };
    Partition.Face[4].Corner[0]     = CubeSpacePoint{ -Half,  Half,  Half };
    Partition.Face[4].Corner[1]     = CubeSpacePoint{  Half,  Half,  Half };
    Partition.Face[4].Corner[2]     = CubeSpacePoint{  Half,  Half, -Half };
    Partition.Face[4].Corner[3]     = CubeSpacePoint{ -Half,  Half, -Half };

    // BOTTOM (-Y)
    Partition.Face[5].Preset        = AlignmentPreset::Bottom;
    Partition.Face[5].OutwardNormal = CubeSpacePoint{  0.0f, -1.0f,  0.0f };
    Partition.Face[5].Corner[0]     = CubeSpacePoint{ -Half, -Half, -Half };
    Partition.Face[5].Corner[1]     = CubeSpacePoint{  Half, -Half, -Half };
    Partition.Face[5].Corner[2]     = CubeSpacePoint{  Half, -Half,  Half };
    Partition.Face[5].Corner[3]     = CubeSpacePoint{ -Half, -Half,  Half };
}

}   // namespace Frontier

#endif
