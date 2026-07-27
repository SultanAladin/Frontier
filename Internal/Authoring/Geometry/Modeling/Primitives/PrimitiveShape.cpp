/*============================================================================================================================================
                                                              PRIMITIVESHAPE.CPP
============================================================================================================================================*/
// 🧩 Implementation of the twenty parametrized base primitives. Every constructor fills a PolygonCluster (positions in authoring
//    cm, CCW-outward winding, quads where a face is naturally four-sided), then refreshes its descriptor once before returning.
//    Shared helpers below (AccumulateRing, PushQuadFace / PushTriFace / PushNgonFace, AccumulateCap, SweepTubeAlongCurve) keep the
//    round + swept shapes on one construction path so the cap-style + loop-count parameter pattern reads identically everywhere.

#include "PrimitiveShape.h"

#include <cmath>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        LOCAL CONSTANTS + SCALARS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    const double Pi        = 3.14159265358979323846;   // [-] - circle constant
    const double TwoPi     = 6.28318530717958647692;   // [-] - full turn in radians

    // Clamp a subdivision / loop count up to a floor a closed body needs (never rejects a degenerate parameter).
    uint32_t EnforceMinimumCount(uint32_t Requested, uint32_t Floor)
    {
        return Requested < Floor ? Floor : Requested;
    }

    Vector3d MakeVector(double XCoord, double YCoord, double ZCoord)
    {
        Vector3d Result;
        Result.XCoord = XCoord;
        Result.YCoord = YCoord;
        Result.ZCoord = ZCoord;
        return Result;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   FACE APPEND HELPERS
    //--------------------------------------------------------------------------------------------------------------------

    // Append a triangle face (three existing vertex indices, CCW-outward).
    void PushTriFace(PolygonCluster& Target, uint32_t IndexA, uint32_t IndexB, uint32_t IndexC)
    {
        Target.FaceVertexIndices.push_back(IndexA);
        Target.FaceVertexIndices.push_back(IndexB);
        Target.FaceVertexIndices.push_back(IndexC);
        Target.FaceVertexCounts.push_back(3);
    }

    // Append a quad face (four existing vertex indices, CCW-outward). Preserved verbatim — the render stream fan-splits it later.
    void PushQuadFace(PolygonCluster& Target,
                      uint32_t       IndexA,
                      uint32_t       IndexB,
                      uint32_t       IndexC,
                      uint32_t       IndexD)
    {
        Target.FaceVertexIndices.push_back(IndexA);
        Target.FaceVertexIndices.push_back(IndexB);
        Target.FaceVertexIndices.push_back(IndexC);
        Target.FaceVertexIndices.push_back(IndexD);
        Target.FaceVertexCounts.push_back(4);
    }

    // Append an N-gon face from a run of existing vertex indices (>= 3), CCW-outward.
    void PushNgonFace(PolygonCluster& Target, const std::vector<uint32_t>& Corners)
    {
        for (uint32_t Corner : Corners)
        {
            Target.FaceVertexIndices.push_back(Corner);
        }
        Target.FaceVertexCounts.push_back(static_cast<uint32_t>(Corners.size()));
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   RING + CAP HELPERS
    //--------------------------------------------------------------------------------------------------------------------

    // Append one full ring of SegmentCount vertices at a fixed height, returning the index of the first vertex. The ring is
    // centred on (CentreX, Height, CentreZ) and walks CCW when viewed from +Y. Callers stitch consecutive rings into a wall.
    uint32_t AccumulateRing(PolygonCluster& Target,
                            double          CentreX,
                            double          Height,
                            double          CentreZ,
                            double          Radius,
                            uint32_t        SegmentCount)
    {
        uint32_t FirstIndex = EvaluateVertexCount(Target.Attributes);
        for (uint32_t Segment = 0; Segment < SegmentCount; ++Segment)
        {
            double Angle = TwoPi * (static_cast<double>(Segment) / static_cast<double>(SegmentCount));
            double PointX = CentreX + Radius * std::cos(Angle);
            double PointZ = CentreZ + Radius * std::sin(Angle);
            AccumulateVertexPosition(Target.Attributes, MakeVector(PointX, Height, PointZ));
        }
        return FirstIndex;
    }

    // Stitch two rings (equal SegmentCount) into a wall band of quads. LowerFirst / UpperFirst are the first-vertex indices of
    // the two rings. OutwardCcw selects the winding so the wall faces away from the axis.
    void AccumulateWallBand(PolygonCluster& Target,
                            uint32_t        LowerFirst,
                            uint32_t        UpperFirst,
                            uint32_t        SegmentCount,
                            bool            OutwardCcw)
    {
        for (uint32_t Segment = 0; Segment < SegmentCount; ++Segment)
        {
            uint32_t Next   = (Segment + 1) % SegmentCount;
            uint32_t LowerA = LowerFirst + Segment;
            uint32_t LowerB = LowerFirst + Next;
            uint32_t UpperA = UpperFirst + Segment;
            uint32_t UpperB = UpperFirst + Next;
            if (OutwardCcw)
            {
                PushQuadFace(Target, LowerA, LowerB, UpperB, UpperA);
            }
            else
            {
                PushQuadFace(Target, LowerA, UpperA, UpperB, LowerB);
            }
        }
    }

    // Close a circular end (a ring already appended, first vertex RingFirst) per the cap style. FaceUp selects winding so the
    // cap normal points away from the body. CapLoopCount concentric bands apply only to CapSquareGrid.
    void AccumulateCap(PolygonCluster& Target,
                       uint32_t        RingFirst,
                       double          CentreX,
                       double          CentreY,
                       double          CentreZ,
                       double          Radius,
                       uint32_t        SegmentCount,
                       uint32_t        CapLoopCount,
                       CapCategory     Cap,
                       bool            FaceUp)
    {
        if (Cap == CapNone)
        {
            return;
        }
        if (Cap == CapTriangleFan)
        {
            uint32_t CentreIndex = AccumulateVertexPosition(Target.Attributes, MakeVector(CentreX, CentreY, CentreZ));
            for (uint32_t Segment = 0; Segment < SegmentCount; ++Segment)
            {
                uint32_t Next = (Segment + 1) % SegmentCount;
                if (FaceUp)
                {
                    PushTriFace(Target, CentreIndex, RingFirst + Segment, RingFirst + Next);
                }
                else
                {
                    PushTriFace(Target, CentreIndex, RingFirst + Next, RingFirst + Segment);
                }
            }
            return;
        }

        // CapSquareGrid: concentric quad bands from the outer ring inward, closed by a central fan on the innermost ring.
        uint32_t Bands       = EnforceMinimumCount(CapLoopCount, 1);
        uint32_t OuterFirst  = RingFirst;
        for (uint32_t Band = 1; Band <= Bands; ++Band)
        {
            double   InnerRadius = Radius * (1.0 - static_cast<double>(Band) / static_cast<double>(Bands));
            uint32_t InnerFirst  = AccumulateRing(Target, CentreX, CentreY, CentreZ, InnerRadius, SegmentCount);
            for (uint32_t Segment = 0; Segment < SegmentCount; ++Segment)
            {
                uint32_t Next   = (Segment + 1) % SegmentCount;
                uint32_t OuterA = OuterFirst + Segment;
                uint32_t OuterB = OuterFirst + Next;
                uint32_t InnerA = InnerFirst + Segment;
                uint32_t InnerB = InnerFirst + Next;
                if (FaceUp)
                {
                    PushQuadFace(Target, OuterA, OuterB, InnerB, InnerA);
                }
                else
                {
                    PushQuadFace(Target, OuterA, InnerA, InnerB, OuterB);
                }
            }
            OuterFirst = InnerFirst;
        }
        // Innermost ring collapsed to its centre with a fan (radius 0 band would be degenerate quads otherwise).
        uint32_t CentreIndex = AccumulateVertexPosition(Target.Attributes, MakeVector(CentreX, CentreY, CentreZ));
        for (uint32_t Segment = 0; Segment < SegmentCount; ++Segment)
        {
            uint32_t Next = (Segment + 1) % SegmentCount;
            if (FaceUp)
            {
                PushTriFace(Target, CentreIndex, OuterFirst + Segment, OuterFirst + Next);
            }
            else
            {
                PushTriFace(Target, CentreIndex, OuterFirst + Next, OuterFirst + Segment);
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   SWEPT-TUBE HELPER
    //--------------------------------------------------------------------------------------------------------------------

    // Rodrigues rotation of a vector about a unit axis by an angle in radians (local — Math library has no rotate-about-axis).
    Vector3d RotateAroundAxis(const Vector3d& Target, const Vector3d& UnitAxis, double Angle)
    {
        double   CosAngle = std::cos(Angle);
        double   SinAngle = std::sin(Angle);
        Vector3d Term1    = ScaleVector(Target, CosAngle);
        Vector3d Term2    = ScaleVector(CrossProduct(UnitAxis, Target), SinAngle);
        Vector3d Term3    = ScaleVector(UnitAxis, DotProduct(UnitAxis, Target) * (1.0 - CosAngle));
        return AddVector(AddVector(Term1, Term2), Term3);
    }

    // Sweep a circular cross-section of TubeRadius along an ordered centreline, orienting each ring by a parallel-transport
    // frame so the tube never twists. Closed selects whether the last sample stitches back to the first (Torus / Torus-knot).
    // Every consecutive ring pair becomes a wall band of quads; no end caps (a closed tube needs none, an open one is a stub).
    void SweepTubeAlongCurve(PolygonCluster&              Target,
                             const std::vector<Vector3d>& Centreline,
                             double                       TubeRadius,
                             uint32_t                     TubeSegmentCount,
                             bool                         Closed)
    {
        uint32_t SampleCount = static_cast<uint32_t>(Centreline.size());
        if (SampleCount < 2)
        {
            return;
        }

        // Seed a reference frame at the first tangent, then parallel-transport it sample to sample (minimum-rotation frame).
        Vector3d PreviousTangent = NormalizeVector(SubtractVector(Centreline[1], Centreline[0]));
        Vector3d SeedUp          = std::fabs(PreviousTangent.YCoord) < 0.99 ? MakeVector(0.0, 1.0, 0.0)
                                                                            : MakeVector(1.0, 0.0, 0.0);
        Vector3d FrameNormal     = NormalizeVector(CrossProduct(PreviousTangent, SeedUp));
        Vector3d FrameBinormal   = NormalizeVector(CrossProduct(PreviousTangent, FrameNormal));

        std::vector<uint32_t> RingFirst;
        RingFirst.reserve(SampleCount);
        for (uint32_t Sample = 0; Sample < SampleCount; ++Sample)
        {
            uint32_t NextSample = (Sample + 1) % SampleCount;
            Vector3d Tangent    = (Sample + 1 < SampleCount || Closed)
                                    ? NormalizeVector(SubtractVector(Centreline[NextSample], Centreline[Sample]))
                                    : PreviousTangent;

            // Rotate the frame from PreviousTangent onto Tangent (Rodrigues about their cross product).
            Vector3d RotationAxis = CrossProduct(PreviousTangent, Tangent);
            double   SinAngle     = EvaluateVectorLength(RotationAxis);
            double   CosAngle     = DotProduct(PreviousTangent, Tangent);
            if (SinAngle > 1.0e-6)
            {
                Vector3d UnitAxis   = ScaleVector(RotationAxis, 1.0 / SinAngle);
                double   Angle      = std::atan2(SinAngle, CosAngle);
                FrameNormal         = RotateAroundAxis(FrameNormal, UnitAxis, Angle);
                FrameBinormal       = RotateAroundAxis(FrameBinormal, UnitAxis, Angle);
            }
            PreviousTangent = Tangent;

            uint32_t FirstIndex = EvaluateVertexCount(Target.Attributes);
            for (uint32_t Segment = 0; Segment < TubeSegmentCount; ++Segment)
            {
                double   Angle  = TwoPi * (static_cast<double>(Segment) / static_cast<double>(TubeSegmentCount));
                Vector3d Offset = AddVector(ScaleVector(FrameNormal, TubeRadius * std::cos(Angle)),
                                            ScaleVector(FrameBinormal, TubeRadius * std::sin(Angle)));
                AccumulateVertexPosition(Target.Attributes, AddVector(Centreline[Sample], Offset));
            }
            RingFirst.push_back(FirstIndex);
        }

        uint32_t BandCount = Closed ? SampleCount : SampleCount - 1;
        for (uint32_t Band = 0; Band < BandCount; ++Band)
        {
            uint32_t NextBand = (Band + 1) % SampleCount;
            AccumulateWallBand(Target, RingFirst[Band], RingFirst[NextBand], TubeSegmentCount, true);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                        BOX + PLANE
//------------------------------------------------------------------------------------------------------------------------

PolygonCluster ConstructBoxPrimitive(const BoxParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t SegmentX = EnforceMinimumCount(Parameters.SegmentX, 1);
    uint32_t SegmentY = EnforceMinimumCount(Parameters.SegmentY, 1);
    uint32_t SegmentZ = EnforceMinimumCount(Parameters.SegmentZ, 1);
    double   ExtentX  = Parameters.HalfExtentX;
    double   ExtentY  = Parameters.HalfExtentY;
    double   ExtentZ  = Parameters.HalfExtentZ;

    // Each of the six faces is an independent subdivided grid (own vertices — a box has no shared corner UVs). AxisU / AxisV
    // walk the face plane; the fixed axis holds at ±extent. Winding is CCW seen from outside.
    struct FacePlan
    {
        double NormalX, NormalY, NormalZ;
        uint32_t CountU, CountV;
    };

    auto BuildFace = [&](double FixedX, double FixedY, double FixedZ,
                         double AxisUX, double AxisUY, double AxisUZ,
                         double AxisVX, double AxisVY, double AxisVZ,
                         uint32_t CountU, uint32_t CountV)
    {
        uint32_t FirstIndex = EvaluateVertexCount(Result.Attributes);
        for (uint32_t Row = 0; Row <= CountV; ++Row)
        {
            double VParam = -1.0 + 2.0 * (static_cast<double>(Row) / static_cast<double>(CountV));
            for (uint32_t Column = 0; Column <= CountU; ++Column)
            {
                double UParam = -1.0 + 2.0 * (static_cast<double>(Column) / static_cast<double>(CountU));
                double PointX = FixedX + AxisUX * UParam + AxisVX * VParam;
                double PointY = FixedY + AxisUY * UParam + AxisVY * VParam;
                double PointZ = FixedZ + AxisUZ * UParam + AxisVZ * VParam;
                AccumulateVertexPosition(Result.Attributes, MakeVector(PointX, PointY, PointZ));
            }
        }
        uint32_t Stride = CountU + 1;
        for (uint32_t Row = 0; Row < CountV; ++Row)
        {
            for (uint32_t Column = 0; Column < CountU; ++Column)
            {
                uint32_t A = FirstIndex + Row * Stride + Column;
                uint32_t B = A + 1;
                uint32_t C = A + Stride + 1;
                uint32_t D = A + Stride;
                PushQuadFace(Result, A, B, C, D);
            }
        }
    };

    // +X and -X faces (span YZ), +Y / -Y (span XZ), +Z / -Z (span XY). AxisU × AxisV points outward on each.
    BuildFace( ExtentX, 0.0, 0.0,   0.0, 0.0,  ExtentZ,   0.0, ExtentY, 0.0,   SegmentZ, SegmentY);
    BuildFace(-ExtentX, 0.0, 0.0,   0.0, 0.0, -ExtentZ,   0.0, ExtentY, 0.0,   SegmentZ, SegmentY);
    BuildFace(0.0,  ExtentY, 0.0,   ExtentX, 0.0, 0.0,    0.0, 0.0, -ExtentZ,  SegmentX, SegmentZ);
    BuildFace(0.0, -ExtentY, 0.0,   ExtentX, 0.0, 0.0,    0.0, 0.0,  ExtentZ,  SegmentX, SegmentZ);
    BuildFace(0.0, 0.0,  ExtentZ,   ExtentX, 0.0, 0.0,    0.0, ExtentY, 0.0,   SegmentX, SegmentY);
    BuildFace(0.0, 0.0, -ExtentZ,  -ExtentX, 0.0, 0.0,    0.0, ExtentY, 0.0,   SegmentX, SegmentY);

    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructPlanePrimitive(const PlaneParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t CountX = EnforceMinimumCount(Parameters.SubdivisionX, 1);
    uint32_t CountZ = EnforceMinimumCount(Parameters.SubdivisionZ, 1);
    double   ExtentX = Parameters.HalfExtentX;
    double   ExtentZ = Parameters.HalfExtentZ;

    for (uint32_t Row = 0; Row <= CountZ; ++Row)
    {
        double PointZ = -ExtentZ + 2.0 * ExtentZ * (static_cast<double>(Row) / static_cast<double>(CountZ));
        for (uint32_t Column = 0; Column <= CountX; ++Column)
        {
            double PointX = -ExtentX + 2.0 * ExtentX * (static_cast<double>(Column) / static_cast<double>(CountX));
            AccumulateVertexPosition(Result.Attributes, MakeVector(PointX, 0.0, PointZ));
        }
    }
    uint32_t Stride = CountX + 1;
    for (uint32_t Row = 0; Row < CountZ; ++Row)
    {
        for (uint32_t Column = 0; Column < CountX; ++Column)
        {
            uint32_t A = Row * Stride + Column;
            uint32_t B = A + 1;
            uint32_t C = A + Stride + 1;
            uint32_t D = A + Stride;
            PushQuadFace(Result, A, D, C, B);   // CCW seen from +Y (upward-facing)
        }
    }
    EvaluatePolygonDescriptor(Result);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          SPHERES
//------------------------------------------------------------------------------------------------------------------------

PolygonCluster ConstructSphereUvPrimitive(const SphereUvParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Segments = EnforceMinimumCount(Parameters.SegmentCount, 3);
    uint32_t Rings    = EnforceMinimumCount(Parameters.RingCount, 2);
    double   Radius   = Parameters.Radius;

    // Pole vertices plus (Rings-1) interior latitude rings, each of Segments longitudes.
    uint32_t NorthPole = AccumulateVertexPosition(Result.Attributes, MakeVector(0.0, Radius, 0.0));
    std::vector<uint32_t> RingFirst;
    RingFirst.reserve(Rings - 1);
    for (uint32_t Ring = 1; Ring < Rings; ++Ring)
    {
        double Latitude = Pi * (static_cast<double>(Ring) / static_cast<double>(Rings));
        double Height   = Radius * std::cos(Latitude);
        double RowRadius = Radius * std::sin(Latitude);
        RingFirst.push_back(AccumulateRing(Result, 0.0, Height, 0.0, RowRadius, Segments));
    }
    uint32_t SouthPole = AccumulateVertexPosition(Result.Attributes, MakeVector(0.0, -Radius, 0.0));

    // North cap fan.
    for (uint32_t Segment = 0; Segment < Segments; ++Segment)
    {
        uint32_t Next = (Segment + 1) % Segments;
        PushTriFace(Result, NorthPole, RingFirst[0] + Next, RingFirst[0] + Segment);
    }
    // Interior wall bands.
    for (uint32_t Ring = 0; Ring + 1 < RingFirst.size(); ++Ring)
    {
        AccumulateWallBand(Result, RingFirst[Ring], RingFirst[Ring + 1], Segments, false);
    }
    // South cap fan.
    uint32_t LastRing = RingFirst.back();
    for (uint32_t Segment = 0; Segment < Segments; ++Segment)
    {
        uint32_t Next = (Segment + 1) % Segments;
        PushTriFace(Result, SouthPole, LastRing + Segment, LastRing + Next);
    }
    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructSphereIcoPrimitive(const SphereIcoParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Level  = Parameters.SubdivisionLevel > 6 ? 6 : Parameters.SubdivisionLevel;
    double   Radius = Parameters.Radius;

    // Base icosahedron (12 vertices), then Level rounds of midpoint splitting; every vertex is normalized to the sphere.
    const double GoldenRatio = (1.0 + std::sqrt(5.0)) * 0.5;
    std::vector<Vector3d> Positions =
    {
        MakeVector(-1.0,  GoldenRatio, 0.0), MakeVector( 1.0,  GoldenRatio, 0.0),
        MakeVector(-1.0, -GoldenRatio, 0.0), MakeVector( 1.0, -GoldenRatio, 0.0),
        MakeVector(0.0, -1.0,  GoldenRatio), MakeVector(0.0,  1.0,  GoldenRatio),
        MakeVector(0.0, -1.0, -GoldenRatio), MakeVector(0.0,  1.0, -GoldenRatio),
        MakeVector( GoldenRatio, 0.0, -1.0), MakeVector( GoldenRatio, 0.0,  1.0),
        MakeVector(-GoldenRatio, 0.0, -1.0), MakeVector(-GoldenRatio, 0.0,  1.0)
    };
    std::vector<uint32_t> Faces =
    {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1
    };

    for (uint32_t Pass = 0; Pass < Level; ++Pass)
    {
        std::vector<uint32_t> NextFaces;
        NextFaces.reserve(Faces.size() * 4);
        for (uint32_t Triangle = 0; Triangle < Faces.size(); Triangle += 3)
        {
            uint32_t IndexA = Faces[Triangle];
            uint32_t IndexB = Faces[Triangle + 1];
            uint32_t IndexC = Faces[Triangle + 2];
            Vector3d MidAB   = ScaleVector(AddVector(Positions[IndexA], Positions[IndexB]), 0.5);
            Vector3d MidBC   = ScaleVector(AddVector(Positions[IndexB], Positions[IndexC]), 0.5);
            Vector3d MidCA   = ScaleVector(AddVector(Positions[IndexC], Positions[IndexA]), 0.5);
            uint32_t IndexAB = static_cast<uint32_t>(Positions.size()); Positions.push_back(MidAB);
            uint32_t IndexBC = static_cast<uint32_t>(Positions.size()); Positions.push_back(MidBC);
            uint32_t IndexCA = static_cast<uint32_t>(Positions.size()); Positions.push_back(MidCA);
            uint32_t Quad[12] = { IndexA, IndexAB, IndexCA,
                                  IndexB, IndexBC, IndexAB,
                                  IndexC, IndexCA, IndexBC,
                                  IndexAB, IndexBC, IndexCA };
            for (uint32_t Corner = 0; Corner < 12; ++Corner)
            {
                NextFaces.push_back(Quad[Corner]);
            }
        }
        Faces.swap(NextFaces);
    }

    for (const Vector3d& Position : Positions)
    {
        Vector3d Unit = NormalizeVector(Position);
        AccumulateVertexPosition(Result.Attributes, ScaleVector(Unit, Radius));
    }
    for (uint32_t Triangle = 0; Triangle < Faces.size(); Triangle += 3)
    {
        PushTriFace(Result, Faces[Triangle], Faces[Triangle + 1], Faces[Triangle + 2]);
    }
    EvaluatePolygonDescriptor(Result);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     CYLINDER + CONE
//------------------------------------------------------------------------------------------------------------------------

PolygonCluster ConstructCylinderPrimitive(const CylinderParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Radial  = EnforceMinimumCount(Parameters.RadialLoopCount, 3);
    uint32_t Heights = EnforceMinimumCount(Parameters.HeightLoopCount, 1);
    double   Radius  = Parameters.Radius;
    double   HalfHeight = Parameters.Height * 0.5;

    std::vector<uint32_t> RingFirst;
    RingFirst.reserve(Heights + 1);
    for (uint32_t Row = 0; Row <= Heights; ++Row)
    {
        double Height = -HalfHeight + Parameters.Height * (static_cast<double>(Row) / static_cast<double>(Heights));
        RingFirst.push_back(AccumulateRing(Result, 0.0, Height, 0.0, Radius, Radial));
    }
    for (uint32_t Row = 0; Row < Heights; ++Row)
    {
        AccumulateWallBand(Result, RingFirst[Row], RingFirst[Row + 1], Radial, true);
    }
    // Top cap (faces +Y) then bottom cap (faces -Y). Each closes a fresh coincident ring so cap topology is independent.
    uint32_t TopRing = AccumulateRing(Result, 0.0, HalfHeight, 0.0, Radius, Radial);
    AccumulateCap(Result, TopRing, 0.0, HalfHeight, 0.0, Radius, Radial, Parameters.CapLoopCount, Parameters.Cap, true);
    uint32_t BottomRing = AccumulateRing(Result, 0.0, -HalfHeight, 0.0, Radius, Radial);
    AccumulateCap(Result, BottomRing, 0.0, -HalfHeight, 0.0, Radius, Radial, Parameters.CapLoopCount, Parameters.Cap, false);

    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructConePrimitive(const ConeParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Radial = EnforceMinimumCount(Parameters.RadialLoopCount, 3);
    double   Radius = Parameters.BaseRadius;
    double   HalfHeight = Parameters.Height * 0.5;

    uint32_t BaseRing = AccumulateRing(Result, 0.0, -HalfHeight, 0.0, Radius, Radial);
    uint32_t Apex     = AccumulateVertexPosition(Result.Attributes, MakeVector(0.0, HalfHeight, 0.0));
    for (uint32_t Segment = 0; Segment < Radial; ++Segment)
    {
        uint32_t Next = (Segment + 1) % Radial;
        PushTriFace(Result, BaseRing + Segment, BaseRing + Next, Apex);   // sidewall, outward
    }
    uint32_t CapRing = AccumulateRing(Result, 0.0, -HalfHeight, 0.0, Radius, Radial);
    AccumulateCap(Result, CapRing, 0.0, -HalfHeight, 0.0, Radius, Radial, Parameters.CapLoopCount, Parameters.Cap, false);

    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructCapsulePrimitive(const CapsuleParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Segments   = EnforceMinimumCount(Parameters.SegmentCount, 3);
    uint32_t HemiRings  = EnforceMinimumCount(Parameters.HemisphereRingCount, 1);
    double   Radius     = Parameters.Radius;
    double   HalfCyl    = Parameters.CylinderHeight * 0.5;

    // Top pole, top hemisphere rings, two shared cylinder-seam rings, bottom hemisphere rings, bottom pole.
    uint32_t NorthPole = AccumulateVertexPosition(Result.Attributes, MakeVector(0.0, HalfCyl + Radius, 0.0));
    std::vector<uint32_t> RingFirst;

    for (uint32_t Ring = 1; Ring <= HemiRings; ++Ring)
    {
        double Latitude  = (Pi * 0.5) * (static_cast<double>(Ring) / static_cast<double>(HemiRings));
        double Height     = HalfCyl + Radius * std::cos(Latitude);
        double RowRadius   = Radius * std::sin(Latitude);
        RingFirst.push_back(AccumulateRing(Result, 0.0, Height, 0.0, RowRadius, Segments));
    }
    // Straight seam ring at the lower cylinder edge (upper seam is the last hemisphere ring above).
    RingFirst.push_back(AccumulateRing(Result, 0.0, -HalfCyl, 0.0, Radius, Segments));
    for (uint32_t Ring = 1; Ring <= HemiRings; ++Ring)
    {
        double Latitude  = (Pi * 0.5) * (static_cast<double>(Ring) / static_cast<double>(HemiRings));
        double Height     = -HalfCyl - Radius * std::sin(Latitude);
        double RowRadius   = Radius * std::cos(Latitude);
        RingFirst.push_back(AccumulateRing(Result, 0.0, Height, 0.0, RowRadius, Segments));
    }
    uint32_t SouthPole = AccumulateVertexPosition(Result.Attributes, MakeVector(0.0, -HalfCyl - Radius, 0.0));

    for (uint32_t Segment = 0; Segment < Segments; ++Segment)
    {
        uint32_t Next = (Segment + 1) % Segments;
        PushTriFace(Result, NorthPole, RingFirst[0] + Next, RingFirst[0] + Segment);
    }
    for (uint32_t Ring = 0; Ring + 1 < RingFirst.size(); ++Ring)
    {
        AccumulateWallBand(Result, RingFirst[Ring], RingFirst[Ring + 1], Segments, false);
    }
    uint32_t LastRing = RingFirst.back();
    for (uint32_t Segment = 0; Segment < Segments; ++Segment)
    {
        uint32_t Next = (Segment + 1) % Segments;
        PushTriFace(Result, SouthPole, LastRing + Segment, LastRing + Next);
    }
    EvaluatePolygonDescriptor(Result);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      TORUS + TUBE + DISC
//------------------------------------------------------------------------------------------------------------------------

PolygonCluster ConstructTorusPrimitive(const TorusParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t MajorCount = EnforceMinimumCount(Parameters.MajorSegmentCount, 3);
    uint32_t MinorCount = EnforceMinimumCount(Parameters.MinorSegmentCount, 3);

    // Sample the major ring centreline in the XZ plane; SweepTubeAlongCurve extrudes the minor circle and closes the loop.
    std::vector<Vector3d> Centreline;
    Centreline.reserve(MajorCount);
    for (uint32_t Segment = 0; Segment < MajorCount; ++Segment)
    {
        double Angle = TwoPi * (static_cast<double>(Segment) / static_cast<double>(MajorCount));
        Centreline.push_back(MakeVector(Parameters.MajorRadius * std::cos(Angle),
                                        0.0,
                                        Parameters.MajorRadius * std::sin(Angle)));
    }
    SweepTubeAlongCurve(Result, Centreline, Parameters.MinorRadius, MinorCount, true);
    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructTubePrimitive(const TubeParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Radial  = EnforceMinimumCount(Parameters.RadialLoopCount, 3);
    uint32_t Heights = EnforceMinimumCount(Parameters.HeightLoopCount, 1);
    double   Inner   = Parameters.InnerRadius;
    double   Outer   = Parameters.OuterRadius > Inner ? Parameters.OuterRadius : Inner + 1.0;
    double   HalfHeight = Parameters.Height * 0.5;

    std::vector<uint32_t> OuterRing;
    std::vector<uint32_t> InnerRing;
    OuterRing.reserve(Heights + 1);
    InnerRing.reserve(Heights + 1);
    for (uint32_t Row = 0; Row <= Heights; ++Row)
    {
        double Height = -HalfHeight + Parameters.Height * (static_cast<double>(Row) / static_cast<double>(Heights));
        OuterRing.push_back(AccumulateRing(Result, 0.0, Height, 0.0, Outer, Radial));
        InnerRing.push_back(AccumulateRing(Result, 0.0, Height, 0.0, Inner, Radial));
    }
    for (uint32_t Row = 0; Row < Heights; ++Row)
    {
        AccumulateWallBand(Result, OuterRing[Row], OuterRing[Row + 1], Radial, true);    // outer wall faces out
        AccumulateWallBand(Result, InnerRing[Row], InnerRing[Row + 1], Radial, false);   // inner wall faces in
    }
    // Ring end caps (top faces +Y, bottom faces -Y): a quad band between the coincident inner + outer rim rings.
    auto CloseRimBand = [&](uint32_t OuterFirst, uint32_t InnerFirst, bool FaceUp)
    {
        for (uint32_t Segment = 0; Segment < Radial; ++Segment)
        {
            uint32_t Next   = (Segment + 1) % Radial;
            uint32_t OuterA = OuterFirst + Segment;
            uint32_t OuterB = OuterFirst + Next;
            uint32_t InnerA = InnerFirst + Segment;
            uint32_t InnerB = InnerFirst + Next;
            if (FaceUp)
            {
                PushQuadFace(Result, OuterA, OuterB, InnerB, InnerA);
            }
            else
            {
                PushQuadFace(Result, OuterA, InnerA, InnerB, OuterB);
            }
        }
    };
    CloseRimBand(OuterRing.back(), InnerRing.back(), true);
    CloseRimBand(OuterRing.front(), InnerRing.front(), false);

    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructDiscPrimitive(const DiscParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t    Radial = EnforceMinimumCount(Parameters.RadialLoopCount, 3);
    CapCategory Cap    = Parameters.Cap == CapNone ? CapTriangleFan : Parameters.Cap;   // a disc with no cap is empty

    uint32_t Ring = AccumulateRing(Result, 0.0, 0.0, 0.0, Parameters.Radius, Radial);
    AccumulateCap(Result, Ring, 0.0, 0.0, 0.0, Parameters.Radius, Radial, Parameters.CapLoopCount, Cap, true);
    EvaluatePolygonDescriptor(Result);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PYRAMID + PRISM + WEDGE
//------------------------------------------------------------------------------------------------------------------------

PolygonCluster ConstructPyramidPrimitive(const PyramidParameters& Parameters)
{
    PolygonCluster Result;
    double   HalfHeight = Parameters.Height * 0.5;
    double   ExtentX = Parameters.HalfBaseX;
    double   ExtentZ = Parameters.HalfBaseZ;

    uint32_t B0 = AccumulateVertexPosition(Result.Attributes, MakeVector(-ExtentX, -HalfHeight, -ExtentZ));
    uint32_t B1 = AccumulateVertexPosition(Result.Attributes, MakeVector( ExtentX, -HalfHeight, -ExtentZ));
    uint32_t B2 = AccumulateVertexPosition(Result.Attributes, MakeVector( ExtentX, -HalfHeight,  ExtentZ));
    uint32_t B3 = AccumulateVertexPosition(Result.Attributes, MakeVector(-ExtentX, -HalfHeight,  ExtentZ));
    uint32_t Apex = AccumulateVertexPosition(Result.Attributes, MakeVector(0.0, HalfHeight, 0.0));

    PushQuadFace(Result, B0, B3, B2, B1);   // base faces -Y
    PushTriFace(Result, B0, B1, Apex);
    PushTriFace(Result, B1, B2, Apex);
    PushTriFace(Result, B2, B3, Apex);
    PushTriFace(Result, B3, B0, Apex);
    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructPrismPrimitive(const PrismParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Sides = EnforceMinimumCount(Parameters.SideCount, 3);
    double   HalfHeight = Parameters.Height * 0.5;

    uint32_t BottomRing = AccumulateRing(Result, 0.0, -HalfHeight, 0.0, Parameters.Radius, Sides);
    uint32_t TopRing    = AccumulateRing(Result, 0.0,  HalfHeight, 0.0, Parameters.Radius, Sides);
    AccumulateWallBand(Result, BottomRing, TopRing, Sides, true);

    // Two N-gon end caps. Top winds CCW seen from +Y, bottom reversed.
    std::vector<uint32_t> TopCorners;
    std::vector<uint32_t> BottomCorners;
    TopCorners.reserve(Sides);
    BottomCorners.reserve(Sides);
    for (uint32_t Segment = 0; Segment < Sides; ++Segment)
    {
        TopCorners.push_back(TopRing + Segment);
        BottomCorners.push_back(BottomRing + (Sides - Segment) % Sides);
    }
    PushNgonFace(Result, TopCorners);
    PushNgonFace(Result, BottomCorners);
    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructWedgePrimitive(const WedgeParameters& Parameters)
{
    PolygonCluster Result;
    double   ExtentX = Parameters.HalfExtentX;
    double   Height  = Parameters.HeightY;
    double   ExtentZ = Parameters.HalfExtentZ;

    // Right-triangular prism: rectangular base, ramp rising along -Z→+Z, two triangular ends.
    uint32_t B0 = AccumulateVertexPosition(Result.Attributes, MakeVector(-ExtentX, 0.0,    -ExtentZ));
    uint32_t B1 = AccumulateVertexPosition(Result.Attributes, MakeVector( ExtentX, 0.0,    -ExtentZ));
    uint32_t B2 = AccumulateVertexPosition(Result.Attributes, MakeVector( ExtentX, 0.0,     ExtentZ));
    uint32_t B3 = AccumulateVertexPosition(Result.Attributes, MakeVector(-ExtentX, 0.0,     ExtentZ));
    uint32_t T0 = AccumulateVertexPosition(Result.Attributes, MakeVector(-ExtentX, Height,  ExtentZ));
    uint32_t T1 = AccumulateVertexPosition(Result.Attributes, MakeVector( ExtentX, Height,  ExtentZ));

    PushQuadFace(Result, B0, B3, B2, B1);   // base -Y
    PushQuadFace(Result, B3, T0, T1, B2);   // back vertical wall (+Z)
    PushQuadFace(Result, B0, B1, T1, T0);   // ramp (the hypotenuse face)
    PushTriFace(Result, B1, B2, T1);        // +X triangular end
    PushTriFace(Result, B0, T0, B3);        // -X triangular end
    EvaluatePolygonDescriptor(Result);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       PLATONIC SOLIDS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Build a cluster from an explicit vertex + face-index table, scaling each vertex to Radius on its own direction. FaceRun
    // is a flat list of {corner-count, indices…} records so triangles + pentagons coexist (dodecahedron needs N-gons).
    PolygonCluster ConstructPlatonicFromTable(const std::vector<Vector3d>& Vertices,
                                              const std::vector<uint32_t>& FaceRun,
                                              double                       Radius)
    {
        PolygonCluster Result;
        for (const Vector3d& Vertex : Vertices)
        {
            AccumulateVertexPosition(Result.Attributes, ScaleVector(NormalizeVector(Vertex), Radius));
        }
        uint32_t Cursor = 0;
        while (Cursor < FaceRun.size())
        {
            uint32_t Corners = FaceRun[Cursor++];
            std::vector<uint32_t> Face;
            Face.reserve(Corners);
            for (uint32_t Corner = 0; Corner < Corners; ++Corner)
            {
                Face.push_back(FaceRun[Cursor++]);
            }
            PushNgonFace(Result, Face);
        }
        EvaluatePolygonDescriptor(Result);
        return Result;
    }
}

PolygonCluster ConstructTetrahedronPrimitive(const PlatonicParameters& Parameters)
{
    std::vector<Vector3d> Vertices =
    {
        MakeVector( 1.0,  1.0,  1.0), MakeVector( 1.0, -1.0, -1.0),
        MakeVector(-1.0,  1.0, -1.0), MakeVector(-1.0, -1.0,  1.0)
    };
    std::vector<uint32_t> FaceRun =
    {
        3, 0, 1, 2,   3, 0, 3, 1,   3, 0, 2, 3,   3, 1, 3, 2
    };
    return ConstructPlatonicFromTable(Vertices, FaceRun, Parameters.Radius);
}

PolygonCluster ConstructOctahedronPrimitive(const PlatonicParameters& Parameters)
{
    std::vector<Vector3d> Vertices =
    {
        MakeVector( 1.0, 0.0, 0.0), MakeVector(-1.0, 0.0, 0.0),
        MakeVector(0.0,  1.0, 0.0), MakeVector(0.0, -1.0, 0.0),
        MakeVector(0.0, 0.0,  1.0), MakeVector(0.0, 0.0, -1.0)
    };
    std::vector<uint32_t> FaceRun =
    {
        3, 4, 0, 2,   3, 4, 2, 1,   3, 4, 1, 3,   3, 4, 3, 0,
        3, 5, 2, 0,   3, 5, 1, 2,   3, 5, 3, 1,   3, 5, 0, 3
    };
    return ConstructPlatonicFromTable(Vertices, FaceRun, Parameters.Radius);
}

PolygonCluster ConstructDodecahedronPrimitive(const PlatonicParameters& Parameters)
{
    const double Phi = (1.0 + std::sqrt(5.0)) * 0.5;
    const double Inv = 1.0 / Phi;
    std::vector<Vector3d> Vertices =
    {
        MakeVector( 1,  1,  1), MakeVector( 1,  1, -1), MakeVector( 1, -1,  1), MakeVector( 1, -1, -1),   //  0-3
        MakeVector(-1,  1,  1), MakeVector(-1,  1, -1), MakeVector(-1, -1,  1), MakeVector(-1, -1, -1),   //  4-7
        MakeVector(0,  Inv,  Phi), MakeVector(0,  Inv, -Phi), MakeVector(0, -Inv,  Phi), MakeVector(0, -Inv, -Phi),   //  8-11
        MakeVector( Inv,  Phi, 0), MakeVector( Inv, -Phi, 0), MakeVector(-Inv,  Phi, 0), MakeVector(-Inv, -Phi, 0),   // 12-15
        MakeVector( Phi, 0,  Inv), MakeVector( Phi, 0, -Inv), MakeVector(-Phi, 0,  Inv), MakeVector(-Phi, 0, -Inv)    // 16-19
    };
    std::vector<uint32_t> FaceRun =
    {
        5,  0,  8, 10,  2, 16,   5,  0, 16, 17,  1, 12,   5,  0, 12, 14,  4,  8,
        5,  8,  4, 18,  6, 10,   5, 12,  1,  9,  5, 14,   5,  1, 17,  3, 11,  9,
        5,  2, 10,  6, 15, 13,   5, 16,  2, 13,  3, 17,   5,  3, 13, 15,  7, 11,
        5,  4, 14,  5, 19, 18,   5,  5,  9, 11,  7, 19,   5,  6, 18, 19,  7, 15
    };
    return ConstructPlatonicFromTable(Vertices, FaceRun, Parameters.Radius);
}

PolygonCluster ConstructIcosahedronPrimitive(const PlatonicParameters& Parameters)
{
    const double Phi = (1.0 + std::sqrt(5.0)) * 0.5;
    std::vector<Vector3d> Vertices =
    {
        MakeVector(-1.0,  Phi, 0.0), MakeVector( 1.0,  Phi, 0.0),
        MakeVector(-1.0, -Phi, 0.0), MakeVector( 1.0, -Phi, 0.0),
        MakeVector(0.0, -1.0,  Phi), MakeVector(0.0,  1.0,  Phi),
        MakeVector(0.0, -1.0, -Phi), MakeVector(0.0,  1.0, -Phi),
        MakeVector( Phi, 0.0, -1.0), MakeVector( Phi, 0.0,  1.0),
        MakeVector(-Phi, 0.0, -1.0), MakeVector(-Phi, 0.0,  1.0)
    };
    std::vector<uint32_t> FaceRun =
    {
        3, 0,11, 5,  3, 0, 5, 1,  3, 0, 1, 7,  3, 0, 7,10,  3, 0,10,11,
        3, 1, 5, 9,  3, 5,11, 4,  3,11,10, 2,  3,10, 7, 6,  3, 7, 1, 8,
        3, 3, 9, 4,  3, 3, 4, 2,  3, 3, 2, 6,  3, 3, 6, 8,  3, 3, 8, 9,
        3, 4, 9, 5,  3, 2, 4,11,  3, 6, 2,10,  3, 8, 6, 7,  3, 9, 8, 1
    };
    return ConstructPlatonicFromTable(Vertices, FaceRun, Parameters.Radius);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ARROW + TORUS-KNOT + HELIX
//------------------------------------------------------------------------------------------------------------------------

PolygonCluster ConstructArrowPrimitive(const ArrowParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Radial = EnforceMinimumCount(Parameters.RadialLoopCount, 3);
    double   ShaftTop = Parameters.ShaftLength;
    double   HeadTop  = Parameters.ShaftLength + Parameters.HeadLength;

    // Shaft: a capped cylinder from Y=0 to Y=ShaftTop. Head: a cone from ShaftTop to HeadTop. Base cap closes the shaft foot.
    uint32_t ShaftBottom = AccumulateRing(Result, 0.0, 0.0, 0.0, Parameters.ShaftRadius, Radial);
    uint32_t ShaftRingTop = AccumulateRing(Result, 0.0, ShaftTop, 0.0, Parameters.ShaftRadius, Radial);
    AccumulateWallBand(Result, ShaftBottom, ShaftRingTop, Radial, true);
    uint32_t FootRing = AccumulateRing(Result, 0.0, 0.0, 0.0, Parameters.ShaftRadius, Radial);
    AccumulateCap(Result, FootRing, 0.0, 0.0, 0.0, Parameters.ShaftRadius, Radial, 1, CapTriangleFan, false);

    // Head base ring (annulus disc closing shaft-top to head-base), cone sidewall to the apex.
    uint32_t HeadBase = AccumulateRing(Result, 0.0, ShaftTop, 0.0, Parameters.HeadRadius, Radial);
    AccumulateCap(Result, HeadBase, 0.0, ShaftTop, 0.0, Parameters.HeadRadius, Radial, 1, CapTriangleFan, false);
    uint32_t HeadRing = AccumulateRing(Result, 0.0, ShaftTop, 0.0, Parameters.HeadRadius, Radial);
    uint32_t Apex     = AccumulateVertexPosition(Result.Attributes, MakeVector(0.0, HeadTop, 0.0));
    for (uint32_t Segment = 0; Segment < Radial; ++Segment)
    {
        uint32_t Next = (Segment + 1) % Radial;
        PushTriFace(Result, HeadRing + Segment, HeadRing + Next, Apex);
    }
    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructTorusKnotPrimitive(const TorusKnotParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Samples = EnforceMinimumCount(Parameters.CurveSampleCount, 3);
    uint32_t Tube    = EnforceMinimumCount(Parameters.TubeSegmentCount, 3);
    double   WindP   = static_cast<double>(EnforceMinimumCount(Parameters.WindP, 1));
    double   WindQ   = static_cast<double>(EnforceMinimumCount(Parameters.WindQ, 1));
    double   Scale   = Parameters.MajorRadius;

    // Standard (p,q) torus-knot parametric curve. Sweep the tube around it and close the loop.
    std::vector<Vector3d> Centreline;
    Centreline.reserve(Samples);
    for (uint32_t Sample = 0; Sample < Samples; ++Sample)
    {
        double Angle  = TwoPi * (static_cast<double>(Sample) / static_cast<double>(Samples));
        double Radius = 2.0 + std::cos(WindQ * Angle);
        double PointX = Scale * 0.5 * Radius * std::cos(WindP * Angle);
        double PointY = Scale * 0.5 * std::sin(WindQ * Angle);
        double PointZ = Scale * 0.5 * Radius * std::sin(WindP * Angle);
        Centreline.push_back(MakeVector(PointX, PointY, PointZ));
    }
    SweepTubeAlongCurve(Result, Centreline, Parameters.TubeRadius, Tube, true);
    EvaluatePolygonDescriptor(Result);
    return Result;
}

PolygonCluster ConstructHelixPrimitive(const HelixParameters& Parameters)
{
    PolygonCluster Result;
    uint32_t Turns   = EnforceMinimumCount(Parameters.TurnCount, 1);
    uint32_t Samples = EnforceMinimumCount(Parameters.CurveSampleCount, 3);
    uint32_t Tube    = EnforceMinimumCount(Parameters.TubeSegmentCount, 3);
    uint32_t Total   = Samples * Turns;

    // Coil centreline: a circle in XZ climbing by Pitch per turn. Open sweep (a spring is not a closed loop).
    std::vector<Vector3d> Centreline;
    Centreline.reserve(Total + 1);
    for (uint32_t Sample = 0; Sample <= Total; ++Sample)
    {
        double Fraction = static_cast<double>(Sample) / static_cast<double>(Samples);   // turns elapsed
        double Angle    = TwoPi * Fraction;
        double Height   = Parameters.Pitch * Fraction - (Parameters.Pitch * Turns) * 0.5;
        Centreline.push_back(MakeVector(Parameters.CoilRadius * std::cos(Angle),
                                        Height,
                                        Parameters.CoilRadius * std::sin(Angle)));
    }
    SweepTubeAlongCurve(Result, Centreline, Parameters.TubeRadius, Tube, false);
    EvaluatePolygonDescriptor(Result);
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CATEGORY ENTRY + NAMES
//------------------------------------------------------------------------------------------------------------------------

PolygonCluster ConstructPrimitiveDefault(PrimitiveShapeCategory Category)
{
    switch (Category)
    {
        case PrimitiveShapeBox:          return ConstructBoxPrimitive(BoxParameters{});
        case PrimitiveShapePlane:        return ConstructPlanePrimitive(PlaneParameters{});
        case PrimitiveShapeSphereUv:     return ConstructSphereUvPrimitive(SphereUvParameters{});
        case PrimitiveShapeSphereIco:    return ConstructSphereIcoPrimitive(SphereIcoParameters{});
        case PrimitiveShapeCylinder:     return ConstructCylinderPrimitive(CylinderParameters{});
        case PrimitiveShapeCone:         return ConstructConePrimitive(ConeParameters{});
        case PrimitiveShapeCapsule:      return ConstructCapsulePrimitive(CapsuleParameters{});
        case PrimitiveShapeTorus:        return ConstructTorusPrimitive(TorusParameters{});
        case PrimitiveShapeTube:         return ConstructTubePrimitive(TubeParameters{});
        case PrimitiveShapeDisc:         return ConstructDiscPrimitive(DiscParameters{});
        case PrimitiveShapePyramid:      return ConstructPyramidPrimitive(PyramidParameters{});
        case PrimitiveShapePrism:        return ConstructPrismPrimitive(PrismParameters{});
        case PrimitiveShapeWedge:        return ConstructWedgePrimitive(WedgeParameters{});
        case PrimitiveShapeTetrahedron:  return ConstructTetrahedronPrimitive(PlatonicParameters{});
        case PrimitiveShapeOctahedron:   return ConstructOctahedronPrimitive(PlatonicParameters{});
        case PrimitiveShapeDodecahedron: return ConstructDodecahedronPrimitive(PlatonicParameters{});
        case PrimitiveShapeIcosahedron:  return ConstructIcosahedronPrimitive(PlatonicParameters{});
        case PrimitiveShapeArrow:        return ConstructArrowPrimitive(ArrowParameters{});
        case PrimitiveShapeTorusKnot:    return ConstructTorusKnotPrimitive(TorusKnotParameters{});
        case PrimitiveShapeHelix:        return ConstructHelixPrimitive(HelixParameters{});
        default:                         return PolygonCluster{};
    }
}

const char* ResolvePrimitiveShapeName(PrimitiveShapeCategory Category)
{
    switch (Category)
    {
        case PrimitiveShapeBox:          return "Box";
        case PrimitiveShapePlane:        return "Plane";
        case PrimitiveShapeSphereUv:     return "SphereUv";
        case PrimitiveShapeSphereIco:    return "SphereIco";
        case PrimitiveShapeCylinder:     return "Cylinder";
        case PrimitiveShapeCone:         return "Cone";
        case PrimitiveShapeCapsule:      return "Capsule";
        case PrimitiveShapeTorus:        return "Torus";
        case PrimitiveShapeTube:         return "Tube";
        case PrimitiveShapeDisc:         return "Disc";
        case PrimitiveShapePyramid:      return "Pyramid";
        case PrimitiveShapePrism:        return "Prism";
        case PrimitiveShapeWedge:        return "Wedge";
        case PrimitiveShapeTetrahedron:  return "Tetrahedron";
        case PrimitiveShapeOctahedron:   return "Octahedron";
        case PrimitiveShapeDodecahedron: return "Dodecahedron";
        case PrimitiveShapeIcosahedron:  return "Icosahedron";
        case PrimitiveShapeArrow:        return "Arrow";
        case PrimitiveShapeTorusKnot:    return "TorusKnot";
        case PrimitiveShapeHelix:        return "Helix";
        default:                         return "unknown";
    }
}

} // namespace Frontier
