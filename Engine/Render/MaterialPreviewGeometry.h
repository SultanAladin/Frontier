/*==========================================================================================================================================
                                                       MATERIALPREVIEWGEOMETRY.H
==========================================================================================================================================*/
// 🧩 Shared UV/tangent sphere for material viewports and GPU thumbnail baking.

#pragma once
#include "SurfaceGeometry.h"
#include <numbers>

namespace Frontier
{
inline SurfaceGeometry ConstructMaterialPreviewSphere()
{
    SurfaceGeometry Geometry;
    constexpr uint32_t Slices = 128, Rings = 64;
    for (uint32_t Ring = 0; Ring <= Rings; ++Ring)
        for (uint32_t Slice = 0; Slice <= Slices; ++Slice)
        {
            const double θ = std::numbers::pi * Ring / Rings;
            const double φ = 2 * std::numbers::pi * Slice / Slices;
            const std::array<float, 3> Normal{float(std::sin(θ) * std::cos(φ)), float(std::sin(θ) * std::sin(φ)), float(std::cos(θ))};
            SurfaceCorner Corner;
            Corner.Position = {Normal[0] * .85f, Normal[1] * .85f, Normal[2] * .85f};
            Corner.Normal = Normal;
            Corner.Coordinates = {float(Slice) / Slices, float(Ring) / Rings};
            Corner.Tangent = {float(-std::sin(φ)), float(std::cos(φ)), 0, -1};
            Geometry.Corners.push_back(Corner);
        }
    for (uint32_t Ring = 0; Ring < Rings; ++Ring)
        for (uint32_t Slice = 0; Slice < Slices; ++Slice)
        {
            const uint32_t First = Ring * (Slices + 1) + Slice;
            for (const auto Index : {First, First + 1, First + Slices + 1, First + 1, First + Slices + 2, First + Slices + 1})
                Geometry.TriangleCorners.push_back(Index);
        }
    Geometry.MaterialCoordinatesEnabled = true;
    return Geometry;
}
// 📝 Profile equations and angular resolution follow the HTML createBallGeometry implementation.
inline SurfaceGeometry ConstructMaterialPreviewShaderBall()
{
    SurfaceGeometry Geometry;
    constexpr uint32_t Rings = 340, Slices = 192;
    const auto Radius = [](double θ)
    {
        const double Y = 1.43 * std::cos(θ);
        const auto Groove = [Y](double Centre, double Width, double Depth)
        { return Depth * std::exp(-std::pow((Y - Centre) / Width, 6)); };
        return std::max(.001, 1.43 * std::sin(θ) - Groove(.69, .021, .032) - Groove(.6, .014, .023) -
            Groove(-.81, .018, .028) - Groove(-.89, .018, .028) - Groove(-.97, .018, .028));
    };
    const auto Rotate = [](std::array<float, 3> Direction)
    {
        const float X = Direction[0] * std::cos(-.27f) - Direction[1] * std::sin(-.27f);
        const float Y = Direction[0] * std::sin(-.27f) + Direction[1] * std::cos(-.27f);
        return std::array<float, 3>{X * std::cos(-.4f) + Direction[2] * std::sin(-.4f), Y,
            -X * std::sin(-.4f) + Direction[2] * std::cos(-.4f)};
    };
    for (uint32_t Ring = 0; Ring <= Rings; ++Ring)
    {
        const double θ = std::numbers::pi * Ring / Rings;
        const double Derivative = (Radius(θ + .00001) - Radius(θ - .00001)) / .00002;
        const double YDerivative = -1.43 * std::sin(θ);
        const double Length = std::max(.00001, std::hypot(Derivative, YDerivative));
        for (uint32_t Slice = 0; Slice <= Slices; ++Slice)
        {
            const double φ = 2 * std::numbers::pi * Slice / Slices;
            SurfaceCorner Corner;
            Corner.Position = Rotate({float(Radius(θ) * std::sin(φ) * .85 / 1.43), float(.85 * std::cos(θ)), float(Radius(θ) * std::cos(φ) * .85 / 1.43)});
            Corner.Normal = Rotate({float(-YDerivative * std::sin(φ) / Length), float(Derivative / Length), float(-YDerivative * std::cos(φ) / Length)});
            if (Ring == 0 || Ring == Rings) Corner.Normal = Rotate({0, Ring == 0 ? 1.f : -1.f, 0});
            Corner.Coordinates = {float(Slice) / Slices, float(Ring) / Rings};
            const auto Tangent = Rotate({float(std::cos(φ)), 0, float(-std::sin(φ))});
            Corner.Tangent = {Tangent[0], Tangent[1], Tangent[2], 1};
            Geometry.Corners.push_back(Corner);
        }
    }
    for (uint32_t Ring = 0; Ring < Rings; ++Ring)
        for (uint32_t Slice = 0; Slice < Slices; ++Slice)
        {
            const uint32_t First = Ring * (Slices + 1) + Slice;
            for (uint32_t Index : {First, First + Slices + 1, First + 1, First + 1, First + Slices + 1, First + Slices + 2})
                Geometry.TriangleCorners.push_back(Index);
        }
    Geometry.MaterialCoordinatesEnabled = true;
    return Geometry;
}
// 📝 Keep the isolated ball reusable. The library's specimen adds separately classified, GPU-shaded pedestal surfaces.
inline SurfaceGeometry ConstructMaterialPreviewSpecimen()
{
    auto Geometry = ConstructMaterialPreviewShaderBall();
    constexpr float Scale = .55f, Centre = 1.65f * Scale - .85f, Pitch = .18f;
    const auto Rotate = [](std::array<float, 3> Direction)
    {
        return std::array<float, 3>{Direction[0], Direction[1] * std::cos(Pitch) - Direction[2] * std::sin(Pitch),
            Direction[1] * std::sin(Pitch) + Direction[2] * std::cos(Pitch)};
    };
    for (auto &Corner : Geometry.Corners)
    {
        for (float &Coefficient : Corner.Position) Coefficient *= 1.43f / .85f * Scale;
        Corner.Position[1] += Centre;
        Corner.Position = Rotate(Corner.Position);
        Corner.Normal = Rotate(Corner.Normal);
        const auto Tangent = Rotate({Corner.Tangent[0], Corner.Tangent[1], Corner.Tangent[2]});
        std::copy(Tangent.begin(), Tangent.end(), Corner.Tangent.begin());
    }
    constexpr uint32_t Slices = 128;
    const auto AppendSegment = [&](float FirstRadius, float FirstHeight, float SecondRadius, float SecondHeight, uint32_t Role,
                                   std::array<float, 2> FirstNormal, std::array<float, 2> SecondNormal)
    {
        const uint32_t Offset = uint32_t(Geometry.Corners.size());
        for (uint32_t Ring = 0; Ring < 2; ++Ring)
            for (uint32_t Slice = 0; Slice <= Slices; ++Slice)
            {
                const float Angle = 2.f * std::numbers::pi_v<float> * float(Slice) / Slices;
                const float Radius = Ring ? SecondRadius : FirstRadius, Height = Ring ? SecondHeight : FirstHeight;
                const auto Normal = Ring ? SecondNormal : FirstNormal;
                SurfaceCorner Corner;
                Corner.Position = Rotate({Radius * std::sin(Angle) * Scale, Height * Scale - .85f, Radius * std::cos(Angle) * Scale});
                Corner.Normal = Rotate({Normal[0] * std::sin(Angle), Normal[1], Normal[0] * std::cos(Angle)});
                const auto Tangent = Rotate({std::cos(Angle), 0, -std::sin(Angle)});
                Corner.Tangent = {Tangent[0], Tangent[1], Tangent[2], -1};
                Corner.Coordinates = {-float(Role), float(Ring)};
                Geometry.Corners.push_back(Corner);
            }
        for (uint32_t Slice = 0; Slice < Slices; ++Slice)
        {
            const uint32_t First = Offset + Slice;
            if (FirstRadius > 0)
                for (uint32_t Index : {First, First + 1, First + Slices + 1}) Geometry.TriangleCorners.push_back(Index);
            if (SecondRadius > 0)
                for (uint32_t Index : {First + 1, First + Slices + 2, First + Slices + 1}) Geometry.TriangleCorners.push_back(Index);
        }
    };
    const auto AppendCylinder = [&](float Top, float Bottom, float Height, float Elevation, uint32_t Role)
    {
        const float Slope = Bottom - Top, Length = std::hypot(Height, Slope);
        const std::array<float, 2> Side{Height / Length, Slope / Length};
        AppendSegment(0, Elevation - Height * .5f, Bottom, Elevation - Height * .5f, Role, {0,-1}, {0,-1});
        AppendSegment(Bottom, Elevation - Height * .5f, Top, Elevation + Height * .5f, Role, Side, Side);
        AppendSegment(Top, Elevation + Height * .5f, 0, Elevation + Height * .5f, Role, {0,1}, {0,1});
    };
    AppendCylinder(1.2f, 1.26f, .18f, .115f, 1);
    AppendCylinder(1.26f, 1.22f, .055f, .032f, 2);
    for (uint32_t Segment = 0; Segment < 12; ++Segment)
    {
        const float First = 2.f * std::numbers::pi_v<float> * float(Segment) / 12;
        const float Second = 2.f * std::numbers::pi_v<float> * float(Segment + 1) / 12;
        AppendSegment(1.215f + .012f * std::cos(First), .2f + .012f * std::sin(First),
            1.215f + .012f * std::cos(Second), .2f + .012f * std::sin(Second), 3,
            {std::cos(First), std::sin(First)}, {std::cos(Second), std::sin(Second)});
    }
    return Geometry;
}
// 📝 Separated inspection surfaces share one immutable upload; projection selects one without replacing descriptors.
inline SurfaceGeometry ConstructMaterialInspectionGeometry()
{
    auto Geometry = ConstructMaterialPreviewSpecimen();
    const auto Append = [&](SurfaceGeometry Surface, float Translation)
    {
        const auto Offset = uint32_t(Geometry.Corners.size());
        for (auto Corner : Surface.Corners)
        {
            Corner.Position[0] += Translation;
            Geometry.Corners.push_back(Corner);
        }
        for (auto Index : Surface.TriangleCorners) Geometry.TriangleCorners.push_back(Offset + Index);
    };
    Append(ConstructMaterialPreviewSphere(), 16);
    SurfaceGeometry Plane;
    Plane.MaterialCoordinatesEnabled = true;
    for (const auto &Position : std::array<std::array<float, 3>, 4>{{{-.8f,-.8f,0},{.8f,-.8f,0},{.8f,.8f,0},{-.8f,.8f,0}}})
    {
        SurfaceCorner Corner;
        Corner.Position = Position;
        Corner.Normal = {0,0,1};
        Corner.Coordinates = {(Position[0] + .8f) / 1.6f, (Position[1] + .8f) / 1.6f};
        Plane.Corners.push_back(Corner);
    }
    Plane.TriangleCorners = {0,1,2,0,2,3};
    Append(Plane, 32);
    SurfaceGeometry Cube;
    Cube.MaterialCoordinatesEnabled = true;
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        for (float Sign : {-1.f, 1.f})
        {
            const uint32_t Offset = uint32_t(Cube.Corners.size());
            for (const auto &Coordinate : std::array<std::array<float, 2>,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}})
            {
                SurfaceCorner Corner;
                Corner.Position[Axis] = Sign * .62f;
                Corner.Position[(Axis + 1) % 3] = Coordinate[0] * .62f;
                Corner.Position[(Axis + 2) % 3] = Coordinate[1] * .62f;
                Corner.Normal[Axis] = Sign;
                Corner.Tangent = {0,0,0,Sign};
                Corner.Tangent[(Axis + 1) % 3] = 1;
                Corner.Coordinates = {(Coordinate[0] + 1) / 2, (Coordinate[1] + 1) / 2};
                // 📝 Tilt the specimen to expose three faces, retaining orthogonal unit normals/tangents.
                const auto Rotate = [](std::array<float, 3> Direction)
                {
                    const float X = Direction[0] * .8660254f + Direction[2] * .5f;
                    const float Z = -Direction[0] * .5f + Direction[2] * .8660254f;
                    return std::array<float, 3>{X, Direction[1] * .9396926f - Z * .3420201f,
                        Direction[1] * .3420201f + Z * .9396926f};
                };
                Corner.Position = Rotate(Corner.Position);
                Corner.Normal = Rotate(Corner.Normal);
                auto Tangent = Rotate({Corner.Tangent[0],Corner.Tangent[1],Corner.Tangent[2]});
                std::copy(Tangent.begin(), Tangent.end(), Corner.Tangent.begin());
                Cube.Corners.push_back(Corner);
            }
            for (auto Index : Sign > 0 ? std::array<uint32_t,6>{0,1,2,0,2,3} : std::array<uint32_t,6>{0,2,1,0,3,2})
                Cube.TriangleCorners.push_back(Offset + Index);
        }
    Append(Cube, 48);
    return Geometry;
}
} // namespace Frontier
