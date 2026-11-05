//==========================================================================================================================================
//                                                          PLUMESPACE.CPP
//==========================================================================================================================================
//  The smoke-and-fire lattices' host side: residency, the toroidal moving window, the step record, the
//  fire lights and the tiling detail volume. Ported 1:1 from
//  Experimental/VehicleHmiPrototype/Source/PlumeSpace.js.
//==========================================================================================================================================

#include "PlumeSpace.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace Frontier
{

namespace
{

[[nodiscard]] uint32_t HashCell(int32_t X, int32_t Y, int32_t Z, int32_t Period, int32_t Seed) noexcept
{
    const auto Wrap = [](int32_t Value, int32_t Modulus)
    {
        const int32_t Remainder = Value % Modulus;
        return Remainder < 0 ? Remainder + Modulus : Remainder;
    };
    uint32_t Word = uint32_t(Wrap(X, Period)) * 73856093u
                  ^ uint32_t(Wrap(Y, Period)) * 19349663u
                  ^ uint32_t(Wrap(Z, Period)) * 83492791u
                  ^ uint32_t(Seed) * 2654435761u;
    Word = uint32_t(int32_t(Word ^ (Word >> 15)) * int32_t(0x2C1B3C6D));
    Word = uint32_t(int32_t(Word ^ (Word >> 12)) * int32_t(0x297A2D39));
    return (Word ^ (Word >> 15)) & 0xFFFFFFFFu;
}

[[nodiscard]] double HashUnit(int32_t X, int32_t Y, int32_t Z, int32_t Period, int32_t Seed) noexcept
{
    return double(HashCell(X, Y, Z, Period, Seed)) / 4294967296.0;
}

[[nodiscard]] double SmoothFraction(double Fraction) noexcept
{
    return Fraction * Fraction * (3.0 - 2.0 * Fraction);
}

[[nodiscard]] double ValueNoise(int32_t X, int32_t Y, int32_t Z, int32_t Period, int32_t Seed, int32_t Edge) noexcept
{
    const double Scale = double(Period) / double(Edge);
    const double U = double(X) * Scale;
    const double V = double(Y) * Scale;
    const double W = double(Z) * Scale;
    const int32_t I = int32_t(std::floor(U));
    const int32_t J = int32_t(std::floor(V));
    const int32_t K = int32_t(std::floor(W));
    const double A = SmoothFraction(U - double(I));
    const double B = SmoothFraction(V - double(J));
    const double C = SmoothFraction(W - double(K));

    double Sum = 0.0;
    for (int32_t Corner = 0; Corner < 8; ++Corner)
    {
        const double Dx = double(Corner & 1);
        const double Dy = double((Corner >> 1) & 1);
        const double Dz = double((Corner >> 2) & 1);
        Sum += HashUnit(I + int32_t(Dx), J + int32_t(Dy), K + int32_t(Dz), Period, Seed)
             * (Dx > 0.5 ? A : 1.0 - A) * (Dy > 0.5 ? B : 1.0 - B) * (Dz > 0.5 ? C : 1.0 - C);
    }
    return Sum;
}

[[nodiscard]] double WorleyNoise(int32_t X, int32_t Y, int32_t Z, int32_t Period, int32_t Seed, int32_t Edge) noexcept
{
    const double Scale = double(Period) / double(Edge);
    const double U = double(X) * Scale;
    const double V = double(Y) * Scale;
    const double W = double(Z) * Scale;
    const int32_t I = int32_t(std::floor(U));
    const int32_t J = int32_t(std::floor(V));
    const int32_t K = int32_t(std::floor(W));

    double Nearest = 9.0;
    for (int32_t Dz = -1; Dz <= 1; ++Dz)
        for (int32_t Dy = -1; Dy <= 1; ++Dy)
            for (int32_t Dx = -1; Dx <= 1; ++Dx)
            {
                const int32_t Cx = I + Dx;
                const int32_t Cy = J + Dy;
                const int32_t Cz = K + Dz;
                const double Px = double(Cx) + HashUnit(Cx, Cy, Cz, Period, Seed);
                const double Py = double(Cy) + HashUnit(Cx, Cy, Cz, Period, Seed + 1);
                const double Pz = double(Cz) + HashUnit(Cx, Cy, Cz, Period, Seed + 2);
                const double DeltaX = Px - U;
                const double DeltaY = Py - V;
                const double DeltaZ = Pz - W;
                Nearest = std::min(Nearest, DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
            }
    return std::max(0.0, 1.0 - std::sqrt(Nearest));
}

} // namespace

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                       CONFIGURATION
//--------------------------------------------------------------------------------------------------------------------------------------

PlumeSpace::PlumeSpace(const PlumeGrade& Configuration)
    : Record(PlumeRecordBytes, 0u),
      FireLights(PlumeFireLightBytes, 0u),
      Sources(size_t(PlumeSourceCapacity) * PlumeSourceStride * sizeof(float), 0u)
{
    ReconstructGrade(Configuration);
}

void PlumeSpace::ReconstructGrade(const PlumeGrade& Configuration)
{
    Grade = Configuration;
    Thermo = Configuration.Extent;
    Velocity = { Configuration.Extent[0] / 2u, Configuration.Extent[1] / 2u, Configuration.Extent[2] / 2u };
    CoarseEdge = 2.0f * Configuration.CellExtent;
    Window = { 0, 0 };
    Prior = { PlumeRestartOrigin, PlumeRestartOrigin };
    Parity = 0u;
    Steps = 0u;
    ElapsedSeconds = 0.0f;
    Accumulator = 0.0f;
    Clear();
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                         SCHEDULE
//--------------------------------------------------------------------------------------------------------------------------------------

void PlumeSpace::Schedule(float Seconds,
                          float FrameSeconds,
                          std::array<float, 3> Focus,
                          std::span<const float> SourcesIn,
                          uint32_t SourceCount,
                          const PlumeSettings& Settings,
                          std::array<float, 3> SunDirection)
{
    Sun = SunDirection;
    StepPending = false;

    const bool Active = Settings.PlumeEnabled && SourceCount > 0u;
    if (Active)
    {
        QuietSeconds = 0.0f;
        if (!Resident)
        {
            Resident = true;
            Prior = { PlumeRestartOrigin, PlumeRestartOrigin };
            Accumulator = PlumeStepSeconds;
            QuietPrior = 0.0f;
            RecentreWindow(Focus, true);
        }
    }
    else if (Resident)
    {
        QuietSeconds += Seconds;
        if (QuietSeconds >= PlumeFadeCeiling || !Settings.PlumeEnabled)
            Resident = false;
    }

    if (!Resident)
    {
        Accumulator = 0.0f;
        return;
    }

    Accumulator += Seconds;
    ElapsedSeconds += Seconds;
    if (Accumulator < PlumeStepSeconds)
        return;

    const float Step = std::min(Accumulator, PlumeStepCeiling);
    Accumulator = 0.0f;
    RecentreWindow(Focus, false);
    EncodeRecord(Step, FrameSeconds, SourceCount, Settings);

    const size_t CopyFloats = std::min<size_t>(SourcesIn.size(), size_t(SourceCount) * PlumeSourceStride);
    std::memset(Sources.data(), 0, Sources.size());
    if (CopyFloats > 0u)
        std::memcpy(Sources.data(), SourcesIn.data(), CopyFloats * sizeof(float));

    StepPending = true;
}

void PlumeSpace::AssignPressureSweeps(uint32_t Sweeps) noexcept
{
    // 📝 Red-black Jacobi relaxes in pairs of sweeps, so an odd count is rounded up to the next even
    //    one; below four the projection has not converged enough to be worth running.
    const uint32_t Paired = (std::max(Sweeps, 4u) + 1u) & ~1u;
    Grade.PressureSweeps = Paired;
}

void PlumeSpace::RecentreWindow(std::array<float, 3> Focus, bool Force)
{
    const std::array<int32_t, 2> Cell = { int32_t(std::floor(Focus[0] / CoarseEdge)),
                                          int32_t(std::floor(Focus[1] / CoarseEdge)) };
    const std::array<int32_t, 2> Half = { int32_t(Velocity[0] >> 1u), int32_t(Velocity[1] >> 1u) };
    const std::array<int32_t, 2> Centre = { Window[0] + Half[0], Window[1] + Half[1] };
    if (Force || std::abs(Cell[0] - Centre[0]) > Half[0] / 2 || std::abs(Cell[1] - Centre[1]) > Half[1] / 2)
        Window = { Cell[0] - Half[0], Cell[1] - Half[1] };
}

void PlumeSpace::EncodeRecord(float Step, float FrameSeconds, uint32_t SourceCount, const PlumeSettings& Settings)
{
    // 📝 Frontier's continuous fade once the sources stop: whatever remains reaches zero at FadeCeiling.
    float Fade = 1.0f;
    if (QuietSeconds > 0.0f)
    {
        const float Remaining = std::max(0.0f, PlumeFadeCeiling - QuietSeconds);
        Fade = Remaining / std::max(Step, PlumeFadeCeiling - QuietPrior);
    }
    QuietPrior = QuietSeconds;

    const float Dissipation = std::min(0.95f, Settings.PlumeDissipation);
    const double WindAngle = double(Settings.PlumeWindDirection) * 3.141592653589793 / 180.0;
    const std::array<float, 3> Wind = { float(std::cos(WindAngle)) * Settings.PlumeWindSpeed,
                                        float(std::sin(WindAngle)) * Settings.PlumeWindSpeed,
                                        0.0f };
    const float Edge = Grade.CellExtent;

    const auto WriteInt = [&](uint32_t Index, int32_t Value)
    {
        std::memcpy(Record.data() + Index * sizeof(int32_t), &Value, sizeof(Value));
    };
    const auto WriteUint = [&](uint32_t Index, uint32_t Value)
    {
        std::memcpy(Record.data() + Index * sizeof(uint32_t), &Value, sizeof(Value));
    };
    const auto WriteFloat = [&](uint32_t Index, float Value)
    {
        std::memcpy(Record.data() + Index * sizeof(float), &Value, sizeof(Value));
    };
    const auto WriteVec3 = [&](uint32_t Index, std::array<float, 3> Value)
    {
        for (uint32_t Axis = 0; Axis < 3u; ++Axis)
            WriteFloat(Index + Axis, Value[Axis]);
    };

    WriteInt(0, Window[0]);
    WriteInt(1, Window[1]);
    WriteInt(2, 0);
    WriteInt(3, int32_t(StepOrdinal++));
    WriteInt(4, Prior[0]);
    WriteInt(5, Prior[1]);
    WriteInt(6, 0);
    WriteInt(7, int32_t(SourceCount));

    WriteUint(8, Velocity[0]);
    WriteUint(9, Velocity[1]);
    WriteUint(10, Velocity[2]);
    WriteUint(11, 0u);
    WriteUint(12, Thermo[0]);
    WriteUint(13, Thermo[1]);
    WriteUint(14, Thermo[2]);
    WriteUint(15, Settings.Inspection == 10 ? 1u : 0u);

    WriteFloat(16, Edge);
    WriteFloat(17, 2.0f * Edge);
    WriteFloat(18, PlumeFloorHeight);
    WriteFloat(19, Step);

    WriteFloat(20, Settings.PlumeBuoyancy);
    WriteFloat(21, 0.12f);
    WriteFloat(22, Settings.PlumeVorticity);
    WriteFloat(23, Settings.PlumeTurbulence);

    WriteFloat(24, float(std::pow(1.0 - double(Dissipation), double(Step))) * Fade);
    WriteFloat(25, float(std::pow(1.0 - double(std::min(0.95f, Settings.PlumeCooling)), double(Step))) * Fade);
    WriteFloat(26, float(std::exp(-double(Settings.PlumeFlameDecay) * double(Step))) * Fade);
    WriteFloat(27, float(std::pow(1.0 - 0.08, double(Step))));

    WriteVec3(28, Wind);
    WriteFloat(31, ElapsedSeconds);

    // 📝 The sources' bounds, grown by 3.5 radii (the Gaussian's reach), for the lattice's cheap reject.
    std::array<float, 3> BoundsMinimum = { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> BoundsMaximum = { 0.0f, 0.0f, 0.0f };
    {
        bool First = true;
        const float* Data = reinterpret_cast<const float*>(Sources.data());
        for (uint32_t Index = 0; Index < SourceCount; ++Index)
        {
            const size_t Offset = size_t(Index) * PlumeSourceStride;
            const float Reach = std::max(Data[Offset + 3u], 0.3f) * 3.5f;
            for (uint32_t Axis = 0; Axis < 3u; ++Axis)
            {
                const float Low = Data[Offset + Axis] - Reach;
                const float High = Data[Offset + Axis] + Reach;
                if (First)
                {
                    BoundsMinimum[Axis] = Low;
                    BoundsMaximum[Axis] = High;
                }
                else
                {
                    BoundsMinimum[Axis] = std::min(BoundsMinimum[Axis], Low);
                    BoundsMaximum[Axis] = std::max(BoundsMaximum[Axis], High);
                }
            }
            First = false;
        }
        for (uint32_t Axis = 0; Axis < 3u; ++Axis)
        {
            if (!std::isfinite(BoundsMinimum[Axis]))
                BoundsMinimum[Axis] = 0.0f;
            if (!std::isfinite(BoundsMaximum[Axis]))
                BoundsMaximum[Axis] = 0.0f;
        }
    }
    WriteVec3(32, BoundsMinimum);
    WriteFloat(35, std::max(FrameSeconds, 1.0e-3f));
    WriteVec3(36, BoundsMaximum);
    WriteFloat(39, PlumeFlameScale * Settings.PlumeFire);

    WriteFloat(40, Settings.PlumeExtinction);
    WriteFloat(41, Settings.PlumeAlbedo);
    WriteFloat(42, 3.0f * Settings.PlumeExtinction);
    WriteFloat(43, float(Grade.MarchSamples));

    WriteVec3(44, Sun);
    WriteFloat(47, Settings.PlumeSootAlbedo);

    WriteFloat(48, 2.4f * Settings.PlumeSoot);
    WriteFloat(49, 25.0f * Settings.PlumeHeat);
    WriteFloat(50, float(std::pow(1.0 - 0.8 * double(Dissipation), double(Step))) * Fade);
    WriteFloat(51, 0.0f);

    WriteFloat(52, Settings.PlumeScattering);
    WriteFloat(53, Settings.PlumeAnisotropy);
    WriteFloat(54, Settings.PlumeAmbient);
    WriteFloat(55, Settings.PlumeFireLight);

    WriteFloat(56, Settings.PlumeDetail);
    WriteFloat(57, Settings.PlumeDetailScale);
    WriteFloat(58, Settings.PlumeTurbulenceScale);
    WriteFloat(59, 0.0f);

    WriteFloat(60, Settings.PlumeFlameCore);
    WriteFloat(61, Settings.PlumeFlameTip);
    WriteFloat(62, 30.0f);
    WriteFloat(63, 0.0f);

    Prior = Window;
}

void PlumeSpace::CommitStep() noexcept
{
    if (!StepPending)
        return;
    Parity = 1u - Parity;
    StepPending = false;
    ++Steps;
}

void PlumeSpace::AssignFireLights(std::span<const PlumeFireLight> Lights)
{
    std::memset(FireLights.data(), 0, FireLights.size());
    const uint32_t Count = std::min<uint32_t>(PlumeFireLightLimit, uint32_t(Lights.size()));
    uint32_t Written = Count;
    std::memcpy(FireLights.data(), &Written, sizeof(Written));

    for (uint32_t Index = 0; Index < Count; ++Index)
    {
        const PlumeFireLight& Light = Lights[Index];
        const std::array<float, 4> Position = { Light.Position[0], Light.Position[1], Light.Position[2], Light.Radius };
        std::memcpy(FireLights.data() + (4u + Index * 4u) * sizeof(float), Position.data(), sizeof(Position));
        const std::array<float, 4> Radiance = { Light.Radiance[0], Light.Radiance[1], Light.Radiance[2], 0.0f };
        std::memcpy(FireLights.data() + (20u + Index * 4u) * sizeof(float), Radiance.data(), sizeof(Radiance));
    }
    FireLightCount = Count;
}

void PlumeSpace::Clear() noexcept
{
    Resident = false;
    StepPending = false;
    QuietSeconds = 0.0f;
    QuietPrior = 0.0f;
    Prior = { PlumeRestartOrigin, PlumeRestartOrigin };
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                         DISPATCHES
//--------------------------------------------------------------------------------------------------------------------------------------

std::array<uint32_t, 3> PlumeSpace::CoarseDispatch() const noexcept
{
    return { (Velocity[0] + 3u) / 4u, (Velocity[1] + 3u) / 4u, (Velocity[2] + 3u) / 4u };
}

std::array<uint32_t, 3> PlumeSpace::FineDispatch() const noexcept
{
    return { (Thermo[0] + 3u) / 4u, (Thermo[1] + 3u) / 4u, (Thermo[2] + 3u) / 4u };
}

std::array<uint32_t, 3> PlumeSpace::MarchDispatch(std::array<uint32_t, 2> TraceExtent) const noexcept
{
    return { (TraceExtent[0] + 7u) / 8u, (TraceExtent[1] + 7u) / 8u, 1u };
}

std::string PlumeSpace::QueryDescription() const
{
    std::ostringstream Stream;
    Stream << Thermo[0] << "x" << Thermo[1] << "x" << Thermo[2] << " @ " << int(Grade.CellExtent * 100.0f) << " cm / "
           << Velocity[0] << "x" << Velocity[1] << "x" << Velocity[2] << " @ " << int(2.0f * Grade.CellExtent * 100.0f) << " cm, "
           << "span " << int(Thermo[0] * Grade.CellExtent) << " x " << int(Thermo[1] * Grade.CellExtent) << " x "
           << int(Thermo[2] * Grade.CellExtent) << " m, "
           << (Thermo[0] * Thermo[1] * Thermo[2] + Velocity[0] * Velocity[1] * Velocity[2]) << " cells, "
           << Grade.Name << ", " << Grade.PressureSweeps << " sweeps, " << Grade.MarchSamples << " samples";
    return Stream.str();
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                      DETAIL VOLUME
//--------------------------------------------------------------------------------------------------------------------------------------

std::vector<uint8_t> PlumeSpace::ConstructDetailVolume()
{
    const int32_t Edge = int32_t(PlumeDetailExtent);
    std::vector<uint8_t> Texels(size_t(Edge) * Edge * Edge * 4u, 0u);
    for (int32_t Z = 0; Z < Edge; ++Z)
        for (int32_t Y = 0; Y < Edge; ++Y)
            for (int32_t X = 0; X < Edge; ++X)
            {
                const size_t Offset = (size_t(Z) * size_t(Edge) + size_t(Y)) * size_t(Edge) * 4u + size_t(X) * 4u;
                const double Billow = WorleyNoise(X, Y, Z, 4, 11, Edge);
                Texels[Offset + 0u] = uint8_t(std::lround(255.0 * (0.55 * ValueNoise(X, Y, Z, 4, 1, Edge) + 0.45 * Billow)));
                Texels[Offset + 1u] = uint8_t(std::lround(255.0 * (0.60 * ValueNoise(X, Y, Z, 8, 2, Edge) + 0.40 * WorleyNoise(X, Y, Z, 8, 21, Edge))));
                Texels[Offset + 2u] = uint8_t(std::lround(255.0 * ValueNoise(X, Y, Z, 16, 3, Edge)));
                Texels[Offset + 3u] = uint8_t(std::lround(255.0 * ValueNoise(X, Y, Z, 8, 4, Edge)));
            }
    return Texels;
}

} // namespace Frontier
