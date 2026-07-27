/*============================================================================================================================================
                                                          CALIBRATIONSPECIMEN.CPP
============================================================================================================================================*/
// 🧩 Fills a PolygonCluster from the embedded calibration specimen (Blender Suzanne, original polygons preserved). Positions
//    and PRE-NEGATED normals stream from the generated header into the shared VertexField; the jagged face loops stream into
//    FaceVertexIndices + FaceVertexCounts verbatim (triangles and quads coexist). Per-corner UVs stream from
//    CalibrationSpecimenCornerTexture into FaceCornerTexture (one Vector2d per corner, parallel to FaceVertexIndices). The
//    descriptor is then refreshed so counts / bounds / attribute-presence are ready for the renderer + picker + UV editor.
//    Gated by FRONTIER_CALIBRATION_SPECIMEN.

#include "CalibrationSpecimen.h"

#ifdef FRONTIER_CALIBRATION_SPECIMEN

#include "PolygonCluster.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void AssembleCalibrationSpecimen(PolygonCluster& Specimen)
{
    ResetPolygonCluster(Specimen);

    VertexField& Attributes = Specimen.Attributes;
    Attributes.Position.reserve(CalibrationSpecimenVertexCount);
    Attributes.Normal.reserve(CalibrationSpecimenVertexCount);

    // Positions + PRE-NEGATED normals: one triple per vertex, parallel arrays in the generated header. The header data is authored
    //    Y-up (its exporter's convention); the engine is Z-up (right-handed), so each triple is rotated +90° about world X —
    //    (x, y, z) → (x, −z, y) — which carries the specimen's +Y up-axis onto +Z. This is a proper rotation (determinant +1), so
    //    winding / handedness are preserved and the face-corner order + UVs stream verbatim below.
    for (uint32_t VertexIndex = 0; VertexIndex < CalibrationSpecimenVertexCount; ++VertexIndex)
    {
        const uint32_t Base = VertexIndex * 3;
        Attributes.Position.push_back(Vector3d{  CalibrationSpecimenPositions[Base],
                                                -CalibrationSpecimenPositions[Base + 2],
                                                 CalibrationSpecimenPositions[Base + 1] });
        Attributes.Normal.push_back(Vector3d{  CalibrationSpecimenNormals[Base],
                                              -CalibrationSpecimenNormals[Base + 2],
                                               CalibrationSpecimenNormals[Base + 1] });
    }

    // Face stream: concatenated corner indices + per-face corner counts, streamed verbatim (topology preserved).
    Specimen.FaceVertexIndices.assign(CalibrationSpecimenFaceIndices,
                                      CalibrationSpecimenFaceIndices + CalibrationSpecimenCornerCount);
    Specimen.FaceVertexCounts.assign(CalibrationSpecimenFaceCounts,
                                     CalibrationSpecimenFaceCounts + CalibrationSpecimenFaceCount);

    // Per-corner UVs: one Vector2d per face-corner reference, parallel to FaceVertexIndices (.XCoord = U, .YCoord = V). A seam
    //    vertex shared by two faces carries two distinct corner UVs — the truth the UV editor + painting read.
    Specimen.FaceCornerTexture.reserve(CalibrationSpecimenCornerCount);
    for (uint32_t Corner = 0; Corner < CalibrationSpecimenCornerCount; ++Corner)
    {
        const uint32_t Base = Corner * 2;
        Specimen.FaceCornerTexture.push_back(Vector2d{ CalibrationSpecimenCornerTexture[Base],
                                                       CalibrationSpecimenCornerTexture[Base + 1] });
    }

    // Refresh the derived descriptor for the whole cluster.
    EvaluatePolygonDescriptor(Specimen);
}

} // namespace Frontier

#endif // FRONTIER_CALIBRATION_SPECIMEN
