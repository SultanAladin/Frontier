/*==========================================================================================================================================
                                                         SOFTWAREPRESENTATION.CPP
==========================================================================================================================================*/

#include "Frontier/Game/SoftwarePresentation.h"

#include "Frontier/Engine/Effects/DihedralEdges.h"
#include "Frontier/Engine/Effects/ScanSparkles.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>

namespace Frontier
{

namespace
{

constexpr float Pi = 3.14159265358979f;

struct Vec3
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    Vec3 operator+(const Vec3& O) const { return { X + O.X, Y + O.Y, Z + O.Z }; }
    Vec3 operator-(const Vec3& O) const { return { X - O.X, Y - O.Y, Z - O.Z }; }
    Vec3 operator*(float S) const { return { X * S, Y * S, Z * S }; }
};

float Dot(const Vec3& A, const Vec3& B) { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
Vec3 Cross(const Vec3& A, const Vec3& B) { return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X }; }
Vec3 Normalize(Vec3 V)
{
    const float Length = std::sqrt(Dot(V, V));
    return Length > 1e-8f ? V * (1.0f / Length) : Vec3{ 0.0f, 0.0f, 1.0f };
}

struct Frame
{
    int Width = 0, Height = 0;
    std::vector<std::array<float, 3>> Colour;
    std::vector<float> Depth;

    void Clear()
    {
        for (int Y = 0; Y < Height; ++Y)
        {
            const float T = static_cast<float>(Y) / static_cast<float>(std::max(Height - 1, 1));
            for (int X = 0; X < Width; ++X)
                Colour[Y * Width + X] = { 0.16f + 0.5f * T, 0.17f + 0.52f * T, 0.19f + 0.55f * T };   // studio haze
        }
        std::fill(Depth.begin(), Depth.end(), 1e30f);
    }
};

struct Projector
{
    Vec3 Eye, Forward, Right, Up;
    float TanHalfFov = 0.52f;
    float Aspect = 1.6f;

    [[nodiscard]] bool Project(const Vec3& Point, float& OutX, float& OutY, float& OutZ) const
    {
        const Vec3 Rel = Point - Eye;
        OutZ = Dot(Rel, Forward);
        if (OutZ < 0.15f)
            return false;
        OutX = Dot(Rel, Right) / (OutZ * TanHalfFov * Aspect);
        OutY = Dot(Rel, Up) / (OutZ * TanHalfFov);
        return true;
    }
};

void Triangle(Frame& Target, const Projector& Camera, const Vec3 World[3], const std::array<float, 3>& Albedo,
              float Emission, float Opacity = 1.0f, bool WriteDepth = true, bool Additive = false)
{
    if (Opacity <= 1e-4f && !Additive)
        return;
    float Sx[3], Sy[3], Sz[3];
    for (int Corner = 0; Corner < 3; ++Corner)
        if (!Camera.Project(World[Corner], Sx[Corner], Sy[Corner], Sz[Corner]))
            return;
    const Vec3 Normal = Normalize(Cross(World[1] - World[0], World[2] - World[0]));
    const Vec3 Light = Normalize(Vec3{ 0.25f, 0.15f, 1.0f });   // the softbox, overhead
    const float Lit = 0.28f + 0.72f * std::max(0.0f, std::fabs(Dot(Normal, Light)));

    float Px[3], Py[3];
    for (int Corner = 0; Corner < 3; ++Corner)
    {
        Px[Corner] = (Sx[Corner] * 0.5f + 0.5f) * Target.Width;
        Py[Corner] = (0.5f - Sy[Corner] * 0.5f) * Target.Height;
    }
    const int MinX = std::max(0, static_cast<int>(std::floor(std::min({ Px[0], Px[1], Px[2] }))));
    const int MaxX = std::min(Target.Width - 1, static_cast<int>(std::ceil(std::max({ Px[0], Px[1], Px[2] }))));
    const int MinY = std::max(0, static_cast<int>(std::floor(std::min({ Py[0], Py[1], Py[2] }))));
    const int MaxY = std::min(Target.Height - 1, static_cast<int>(std::ceil(std::max({ Py[0], Py[1], Py[2] }))));
    const float Denominator = (Py[1] - Py[2]) * (Px[0] - Px[2]) + (Px[2] - Px[1]) * (Py[0] - Py[2]);
    if (std::fabs(Denominator) < 1e-9f)
        return;
    for (int Y = MinY; Y <= MaxY; ++Y)
        for (int X = MinX; X <= MaxX; ++X)
        {
            const float W0 = ((Py[1] - Py[2]) * (X - Px[2]) + (Px[2] - Px[1]) * (Y - Py[2])) / Denominator;
            const float W1 = ((Py[2] - Py[0]) * (X - Px[2]) + (Px[0] - Px[2]) * (Y - Py[2])) / Denominator;
            const float W2 = 1.0f - W0 - W1;
            if (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f)
                continue;
            const float Z = 1.0f / (W0 / Sz[0] + W1 / Sz[1] + W2 / Sz[2]);
            const int Index = Y * Target.Width + X;
            if (Z > Target.Depth[Index] + 1e-3f)
                continue;
            if (WriteDepth && Opacity >= 0.92f && !Additive)
                Target.Depth[Index] = Z;

            if (Additive)
            {
                Target.Colour[Index][0] = std::min(1.0f, Target.Colour[Index][0] + Albedo[0]);
                Target.Colour[Index][1] = std::min(1.0f, Target.Colour[Index][1] + Albedo[1]);
                Target.Colour[Index][2] = std::min(1.0f, Target.Colour[Index][2] + Albedo[2]);
            }
            else
            {
                const float Brightness = std::min(1.6f, Lit + Emission);
                const std::array<float, 3> Shaded = {
                    std::min(1.0f, Albedo[0] * Brightness),
                    std::min(1.0f, Albedo[1] * Brightness),
                    std::min(1.0f, Albedo[2] * Brightness)
                };
                const float Alpha = std::clamp(Opacity, 0.0f, 1.0f);
                for (int C = 0; C < 3; ++C)
                    Target.Colour[Index][C] = Target.Colour[Index][C] * (1.0f - Alpha) + Shaded[C] * Alpha;
            }
        }
}

void AdditiveEdge(Frame& Target, const Projector& Camera, const Vec3& A, const Vec3& B,
                  const std::array<float, 3>& Colour, float Alpha)
{
    if (Alpha <= 1e-4f)
        return;
    float Ax, Ay, Az, Bx, By, Bz;
    if (!Camera.Project(A, Ax, Ay, Az) || !Camera.Project(B, Bx, By, Bz))
        return;
    const float X0 = (Ax * 0.5f + 0.5f) * Target.Width, Y0 = (0.5f - Ay * 0.5f) * Target.Height;
    const float X1 = (Bx * 0.5f + 0.5f) * Target.Width, Y1 = (0.5f - By * 0.5f) * Target.Height;
    const int Steps = static_cast<int>(std::max(std::fabs(X1 - X0), std::fabs(Y1 - Y0)));
    if (Steps <= 0)
        return;
    for (int Step = 0; Step <= Steps; ++Step)
    {
        const float T = static_cast<float>(Step) / static_cast<float>(Steps);
        const int X = static_cast<int>(X0 + (X1 - X0) * T), Y = static_cast<int>(Y0 + (Y1 - Y0) * T);
        if (X < 0 || Y < 0 || X >= Target.Width || Y >= Target.Height)
            continue;
        const float Z = 1.0f / ((1.0f - T) / Az + T / Bz);
        const int Index = Y * Target.Width + X;
        if (Z <= Target.Depth[Index] + 0.015f)
        {
            for (int C = 0; C < 3; ++C)
                Target.Colour[Index][C] = std::min(1.0f, Target.Colour[Index][C] + Colour[C] * Alpha);
        }
    }
}

void Quad(Frame& Target, const Projector& Camera, const Vec3 Corners[4], const std::array<float, 3>& Albedo,
          float Emission, float Opacity = 1.0f, bool WriteDepth = true, bool Additive = false)
{
    const Vec3 First[3] = { Corners[0], Corners[1], Corners[2] };
    const Vec3 Second[3] = { Corners[0], Corners[2], Corners[3] };
    Triangle(Target, Camera, First, Albedo, Emission, Opacity, WriteDepth, Additive);
    Triangle(Target, Camera, Second, Albedo, Emission, Opacity, WriteDepth, Additive);
}

std::array<float, 3> FamilyAlbedo(uint32_t Family, const std::array<float, 3>& Paint, float& Emission)
{
    Emission = 0.0f;
    switch (Family)
    {
        case 6: return Paint;                                   // BodyCoat
        case 7: return { 0.05f, 0.07f, 0.09f };                 // VehicleGlass
        case 8: case 12: return { 0.03f, 0.03f, 0.035f };       // plastic / trim
        case 9: return { 0.012f, 0.012f, 0.012f };              // Tyre
        case 10: Emission = 0.55f; return { 0.10f, 0.45f, 0.55f };  // Hub: the wheel light
        case 11: return { 0.25f, 0.05f, 0.04f };                // Brake
        case 13: Emission = 0.35f; return { 0.72f, 0.78f, 0.85f };  // Headlight
        case 14: Emission = 0.35f; return { 0.65f, 0.06f, 0.07f };  // Taillight
        case 70: case 71: case 76: case 77: return { 0.04f, 0.04f, 0.045f };  // cabin
        case 73: return { 0.05f, 0.05f, 0.055f };               // SteeringWheel
        case 74: Emission = 0.04f; return { 0.02f, 0.025f, 0.03f };          // DisplayGlass
        case 101: return { 0.85f, 0.86f, 0.87f };               // PlateFace
        case 102: return { 0.02f, 0.02f, 0.02f };               // PlateInk
        default: return { 0.16f, 0.16f, 0.17f };
    }
}

Vec3 Transform(const Vec3& Local, const Vec3& Origin, float CosYaw, float SinYaw)
{
    return { Origin.X + Local.X * CosYaw - Local.Y * SinYaw,
             Origin.Y + Local.X * SinYaw + Local.Y * CosYaw,
             Origin.Z + Local.Z };
}

const DihedralEdgeSet& CachedDihedralEdges(const FacetArchiveData& Archive, float ThresholdDegrees)
{
    struct Key
    {
        const FacetArchiveData* Ptr = nullptr;
        int Millidegrees = 0;
        bool operator==(const Key& Other) const noexcept { return Ptr == Other.Ptr && Millidegrees == Other.Millidegrees; }
    };
    struct KeyHash
    {
        size_t operator()(const Key& K) const noexcept
        {
            return std::hash<const FacetArchiveData*>{}(K.Ptr) ^ (static_cast<size_t>(K.Millidegrees) * 2654435761u);
        }
    };
    static thread_local std::unordered_map<Key, DihedralEdgeSet, KeyHash> Cache;
    const Key Lookup{ &Archive, static_cast<int>(std::lround(ThresholdDegrees * 1000.0f)) };
    auto It = Cache.find(Lookup);
    if (It == Cache.end())
        It = Cache.emplace(Lookup, ExtractDihedralEdges(Archive, ThresholdDegrees)).first;
    return It->second;
}

void DrawArchiveSolidAndGhost(Frame& Target, const Projector& Camera, const FacetArchiveData& Archive,
                              const Vec3& Origin, const Vec3& VehicleOrigin, float YawDegrees,
                              float ZRebase, float YSign,
                              const std::array<float, 3>& BasePaint,
                              const MaterialTransition* Ripple,
                              const VehicleInspection& Inspection,
                              float DihedralThresholdDegrees)
{
    const float Yaw = YawDegrees * Pi / 180.0f;
    const float Cos = std::cos(Yaw), Sin = std::sin(Yaw);

    // 1. Solid pass (with 3D material transition ripple on family 6 + make-it-disappear Scan dissolve).
    for (uint32_t Facet = 0; Facet < Archive.FacetCount; ++Facet)
    {
        Vec3 World[3];
        Vec3 CentroidLocal{ 0.0f, 0.0f, 0.0f };
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const uint32_t Vertex = Archive.Facets[Facet * 3 + Corner];
            const Vec3 Local = { Archive.Positions[Vertex * 3], YSign * Archive.Positions[Vertex * 3 + 1],
                                 Archive.Positions[Vertex * 3 + 2] - ZRebase };
            World[Corner] = Transform(Local, Origin, Cos, Sin);
            // Express centroid in hero vehicle frame so wheels dissolve in sync with body ScanX.
            const Vec3 RelWorld = World[Corner] - VehicleOrigin;
            CentroidLocal.X += (RelWorld.X * Cos + RelWorld.Y * Sin) / 3.0f;
            CentroidLocal.Y += (-RelWorld.X * Sin + RelWorld.Y * Cos) / 3.0f;
            CentroidLocal.Z += RelWorld.Z / 3.0f;
        }

        const uint32_t Family = Archive.Families[Facet];
        const float SolidAlpha = Inspection.ResolveSolidAlpha(CentroidLocal.X, Family);
        if (SolidAlpha > 1e-3f)
        {
            std::array<float, 3> EffectivePaint = BasePaint;
            float WaveGlow = 0.0f;
            if (Family == 6u && Ripple)
            {
                EffectivePaint = Ripple->ResolveAlbedo({ CentroidLocal.X, CentroidLocal.Y, CentroidLocal.Z }, &WaveGlow);
            }
            float Emission = WaveGlow * 0.28f;
            const auto Albedo = FamilyAlbedo(Family, EffectivePaint, Emission);
            Emission += WaveGlow * 0.28f;
            Triangle(Target, Camera, World, Albedo, Emission, SolidAlpha, SolidAlpha >= 0.92f, false);
        }

        // 2. Additive double-sided Ghost Fresnel + scanline + wavefront glow.
        if (Inspection.View == InspectionView::Ghost || Inspection.View == InspectionView::Scan)
        {
            const Vec3 CenterWorld = (World[0] + World[1] + World[2]) * (1.0f / 3.0f);
            const Vec3 Normal = Normalize(Cross(World[1] - World[0], World[2] - World[0]));
            const Vec3 ViewDir = Normalize(Camera.Eye - CenterWorld);
            const float AbsNdotV = std::fabs(Dot(Normal, ViewDir));
            const auto GhostRgb = Inspection.ResolveGhostAdditive(CentroidLocal.X, CentroidLocal.Z, AbsNdotV);
            if (GhostRgb[0] + GhostRgb[1] + GhostRgb[2] > 1e-4f)
            {
                Triangle(Target, Camera, World, GhostRgb, 0.0f, 1.0f, false, true);
            }
        }
    }

    // 3. 14° body / 28° wheel dihedral wireframe pass (#8fd8f2).
    if (Inspection.View != InspectionView::Solid || Inspection.ResolveHoloAmount() > 1e-3f)
    {
        const DihedralEdgeSet& Edges = CachedDihedralEdges(Archive, DihedralThresholdDegrees);
        constexpr std::array<float, 3> WireSrgbLinear = { 0.279f, 0.687f, 0.888f };
        for (const DihedralEdgeSegment& Seg : Edges.Segments)
        {
            const Vec3 LocalA = { Seg.A[0], YSign * Seg.A[1], Seg.A[2] - ZRebase };
            const Vec3 LocalB = { Seg.B[0], YSign * Seg.B[1], Seg.B[2] - ZRebase };
            const Vec3 WorldA = Transform(LocalA, Origin, Cos, Sin);
            const Vec3 WorldB = Transform(LocalB, Origin, Cos, Sin);
            const Vec3 MidRel = (WorldA + WorldB) * 0.5f - VehicleOrigin;
            const float MidVehicleX = MidRel.X * Cos + MidRel.Y * Sin;
            const float Alpha = Inspection.ResolveWireframeOpacity(MidVehicleX);
            AdditiveEdge(Target, Camera, WorldA, WorldB, WireSrgbLinear, Alpha);
        }
    }
}

void DrawScanCurtainsAndSparkles(Frame& Target, const Projector& Camera,
                                 const Vec3& VehicleOrigin, float YawDegrees,
                                 const VehicleInspection& Inspection)
{
    const float Yaw = YawDegrees * Pi / 180.0f;
    const float Cos = std::cos(Yaw), Sin = std::sin(Yaw);

    // Dual scan curtain sheets at ScanX.
    const auto CurtainOpacities = Inspection.ResolveCurtainOpacities();
    if (CurtainOpacities[0] > 1e-4f || CurtainOpacities[1] > 1e-4f)
    {
        const float ScanX = Inspection.ResolveScanX();
        const Vec3 WideSheet[4] = {
            Transform({ ScanX, -1.20f, -0.55f }, VehicleOrigin, Cos, Sin),
            Transform({ ScanX,  1.20f, -0.55f }, VehicleOrigin, Cos, Sin),
            Transform({ ScanX,  1.20f,  1.95f }, VehicleOrigin, Cos, Sin),
            Transform({ ScanX, -1.20f,  1.95f }, VehicleOrigin, Cos, Sin),
        };
        const Vec3 CoreSheet[4] = {
            Transform({ ScanX, -1.20f,  0.38f }, VehicleOrigin, Cos, Sin),
            Transform({ ScanX,  1.20f,  0.38f }, VehicleOrigin, Cos, Sin),
            Transform({ ScanX,  1.20f,  0.93f }, VehicleOrigin, Cos, Sin),
            Transform({ ScanX, -1.20f,  0.93f }, VehicleOrigin, Cos, Sin),
        };
        Quad(Target, Camera, WideSheet, { 0.039f * CurtainOpacities[0], 0.509f * CurtainOpacities[0], 0.939f * CurtainOpacities[0] },
             0.0f, 1.0f, false, true);
        Quad(Target, Camera, CoreSheet, { 0.480f * CurtainOpacities[1], 0.840f * CurtainOpacities[1], 0.980f * CurtainOpacities[1] },
             0.0f, 1.0f, false, true);
    }

    // 130 floating #8fd8f2 sparkles.
    const float SparkleOpacity = Inspection.ResolveSparkleOpacity();
    if (SparkleOpacity > 1e-4f)
    {
        static const std::vector<ScanSparkleSeed> Cloud = BuildScanSparkleCloud();
        for (const ScanSparkleSeed& Seed : Cloud)
        {
            const EvaluatedSparkle S = EvaluateScanSparkle(Seed, Inspection.ElapsedSeconds);
            const Vec3 WorldPos = Transform({ S.LocalPosition[0], S.LocalPosition[1], S.LocalPosition[2] },
                                            VehicleOrigin, Cos, Sin);
            float Sx = 0.0f, Sy = 0.0f, Sz = 0.0f;
            if (!Camera.Project(WorldPos, Sx, Sy, Sz))
                continue;
            const int Cx = static_cast<int>((Sx * 0.5f + 0.5f) * Target.Width);
            const int Cy = static_cast<int>((0.5f - Sy * 0.5f) * Target.Height);
            const float Weight = S.Brightness * SparkleOpacity;
            for (int Dy = -1; Dy <= 1; ++Dy)
                for (int Dx = -1; Dx <= 1; ++Dx)
                {
                    const int Px = Cx + Dx, Py = Cy + Dy;
                    if (Px < 0 || Py < 0 || Px >= Target.Width || Py >= Target.Height)
                        continue;
                    const int Index = Py * Target.Width + Px;
                    if (Sz > Target.Depth[Index] + 0.02f)
                        continue;
                    const float Falloff = (Dx == 0 && Dy == 0) ? 1.0f : 0.35f;
                    for (int C = 0; C < 3; ++C)
                        Target.Colour[Index][C] = std::min(1.0f, Target.Colour[Index][C] + ScanSparkleLinearColour[C] * Weight * Falloff);
                }
        }
    }
}

bool WriteStudioFramePpm(const std::string& Path, const std::vector<PresentationVehicle>& Vehicles,
                         const Vec3& Focus, float CameraYawDegrees, float CameraPitchDegrees,
                         float CameraDistanceMetres, int Width, int Height)
{
    Frame Target;
    Target.Width = Width;
    Target.Height = Height;
    Target.Colour.resize(static_cast<size_t>(Width) * Height);
    Target.Depth.resize(static_cast<size_t>(Width) * Height);
    Target.Clear();

    const float Yaw = CameraYawDegrees * Pi / 180.0f;
    const float Pitch = CameraPitchDegrees * Pi / 180.0f;
    Projector Camera;
    Camera.Eye = { Focus.X + CameraDistanceMetres * std::cos(Pitch) * std::cos(Yaw),
                   Focus.Y + CameraDistanceMetres * std::cos(Pitch) * std::sin(Yaw),
                   Focus.Z + CameraDistanceMetres * std::sin(Pitch) };
    Camera.Forward = Normalize(Focus - Camera.Eye);
    Camera.Right = Normalize(Cross(Camera.Forward, Vec3{ 0.0f, 0.0f, 1.0f }));
    Camera.Up = Cross(Camera.Right, Camera.Forward);
    Camera.TanHalfFov = std::tan(24.0f * Pi / 180.0f);
    Camera.Aspect = static_cast<float>(Width) / static_cast<float>(Height);

    // Studio floor cove + turntable + overhead softbox.
    const Vec3 Surround[4] = { { -40.0f, -40.0f, -0.02f }, { 40.0f, -40.0f, -0.02f }, { 40.0f, 40.0f, -0.02f }, { -40.0f, 40.0f, -0.02f } };
    Quad(Target, Camera, Surround, { 0.05f, 0.05f, 0.06f }, 0.0f);
    constexpr int Segments = 64;
    for (int Segment = 0; Segment < Segments; ++Segment)
    {
        const float A0 = 2.0f * Pi * Segment / Segments, A1 = 2.0f * Pi * (Segment + 1) / Segments;
        const Vec3 Fan[3] = { { Focus.X, Focus.Y, 0.0f },
                              { Focus.X + 17.6f * std::cos(A0), Focus.Y + 17.6f * std::sin(A0), 0.0f },
                              { Focus.X + 17.6f * std::cos(A1), Focus.Y + 17.6f * std::sin(A1), 0.0f } };
        Triangle(Target, Camera, Fan, { 0.82f, 0.83f, 0.84f }, 0.0f);
        const Vec3 Turntable[3] = { { Focus.X, Focus.Y, 0.005f },
                                    { Focus.X + 4.65f * std::cos(A0), Focus.Y + 4.65f * std::sin(A0), 0.005f },
                                    { Focus.X + 4.65f * std::cos(A1), Focus.Y + 4.65f * std::sin(A1), 0.005f } };
        Triangle(Target, Camera, Turntable, { 0.72f, 0.73f, 0.74f }, 0.0f);
    }
    const Vec3 Softbox[4] = {
        { Focus.X - 12.0f, Focus.Y - 5.5f, 5.85f }, { Focus.X + 12.0f, Focus.Y - 5.5f, 5.85f },
        { Focus.X + 12.0f, Focus.Y + 5.5f, 5.85f }, { Focus.X - 12.0f, Focus.Y + 5.5f, 5.85f }
    };
    Quad(Target, Camera, Softbox, { 0.98f, 0.99f, 1.0f }, 0.9f);

    for (const PresentationVehicle& Vehicle : Vehicles)
    {
        const Vec3 Origin = { Vehicle.PositionMetres[0], Vehicle.PositionMetres[1], Vehicle.PositionMetres[2] };
        const MaterialTransition* Ripple = Vehicle.UsePaintRipple ? &Vehicle.PaintRipple : nullptr;
        const std::array<float, 3> BasePaint = Vehicle.UsePaintRipple ? Vehicle.PaintRipple.CurrentPaint : Vehicle.Paint;

        if (Vehicle.Body && Vehicle.Body->IsValid())
        {
            DrawArchiveSolidAndGhost(Target, Camera, *Vehicle.Body, Origin, Origin, Vehicle.YawDegrees,
                                     0.11285f, 1.0f, BasePaint, Ripple, Vehicle.Inspection, 14.0f);
        }
        if (Vehicle.Tyre && Vehicle.Tyre->IsValid())
        {
            const float MountX[2] = { 1.7274f, -1.6686f };
            for (int Axle = 0; Axle < 2; ++Axle)
                for (int Side = 0; Side < 2; ++Side)
                {
                    const float Sign = Side == 0 ? 1.0f : -1.0f;
                    const Vec3 Local = { MountX[Axle], Sign * 1.0475f, -0.02145f };
                    const float YawRad = Vehicle.YawDegrees * Pi / 180.0f;
                    const Vec3 Hub = Transform(Local, Origin, std::cos(YawRad), std::sin(YawRad));
                    DrawArchiveSolidAndGhost(Target, Camera, *Vehicle.Tyre, Hub, Origin, Vehicle.YawDegrees,
                                             0.0f, Sign, BasePaint, nullptr, Vehicle.Inspection, 28.0f);
                    if (Vehicle.Rim && Vehicle.Rim->IsValid())
                        DrawArchiveSolidAndGhost(Target, Camera, *Vehicle.Rim, Hub, Origin, Vehicle.YawDegrees,
                                                 0.0f, Sign, BasePaint, nullptr, Vehicle.Inspection, 28.0f);
                }
        }
        DrawScanCurtainsAndSparkles(Target, Camera, Origin, Vehicle.YawDegrees, Vehicle.Inspection);
    }

    std::ofstream Stream(Path, std::ios::binary);
    if (!Stream)
        return false;
    Stream << "P6\n" << Width << " " << Height << "\n255\n";
    for (const auto& Pixel : Target.Colour)
        for (int Channel = 0; Channel < 3; ++Channel)
        {
            const float Srgb = std::pow(std::clamp(Pixel[Channel], 0.0f, 1.0f), 1.0f / 2.2f);
            Stream.put(static_cast<char>(std::lround(Srgb * 255.0f)));
        }
    return static_cast<bool>(Stream);
}

} // namespace

bool RenderLobbyPpm(const std::string& Path, const std::vector<PresentationVehicle>& Vehicles,
                    float CameraYawDegrees, float CameraPitchDegrees, float CameraDistanceMetres,
                    int Width, int Height)
{
    return WriteStudioFramePpm(Path, Vehicles, Vec3{ 0.0f, 0.0f, 0.55f },
                               CameraYawDegrees, CameraPitchDegrees, CameraDistanceMetres, Width, Height);
}

bool RenderHmiGaragePpm(const std::string& Path,
                        const std::vector<PresentationVehicle>& AllSceneVehicles,
                        uint32_t HeroVehicleIdentifier,
                        const GarageConfiguratorState& Garage,
                        int Width,
                        int Height)
{
    // Single-vehicle invariant: filter out every non-hero vehicle before raster/lighting.
    std::vector<PresentationVehicle> SingleHero;
    for (const PresentationVehicle& Candidate : AllSceneVehicles)
    {
        if (Candidate.VehicleIdentifier == HeroVehicleIdentifier || (SingleHero.empty() && HeroVehicleIdentifier == 0u))
        {
            PresentationVehicle Configured = Candidate;
            Configured.Inspection = Garage.Inspection;
            Configured.PaintRipple = Garage.PaintRipple;
            Configured.Paint = Garage.PaintRipple.CurrentPaint;
            Configured.UsePaintRipple = true;
            SingleHero.push_back(Configured);
            break;
        }
    }
    if (SingleHero.empty() && !AllSceneVehicles.empty())
    {
        PresentationVehicle Configured = AllSceneVehicles.front();
        Configured.Inspection = Garage.Inspection;
        Configured.PaintRipple = Garage.PaintRipple;
        Configured.Paint = Garage.PaintRipple.CurrentPaint;
        Configured.UsePaintRipple = true;
        SingleHero.push_back(Configured);
    }
    if (SingleHero.empty())
        return false;

    const auto& Hero = SingleHero.front();
    const Vec3 Focus = { Hero.PositionMetres[0], Hero.PositionMetres[1], Hero.PositionMetres[2] + 0.18f };
    return WriteStudioFramePpm(Path, SingleHero, Focus,
                               Hero.YawDegrees + Garage.OrbitYawDegrees,
                               Garage.OrbitPitchDegrees,
                               Garage.OrbitDistanceMetres,
                               Width, Height);
}

} // namespace Frontier
