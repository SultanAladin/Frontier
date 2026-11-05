//==========================================================================================================================================
//                                                        EFFECTSEQUENCE.CPP
//==========================================================================================================================================
//  Derez and rez: eight emitters, one release clock, and the car's recorded motion while it comes
//  apart. Ported 1:1 from Experimental/VehicleHmiPrototype/Source/EffectSequence.js.
//==========================================================================================================================================

#include "EffectSequence.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace Frontier
{

namespace
{

// 📝 The spread's own stream: a linear congruential generator seeded once, so the anchors over a car
//    are the same every run.
class SpreadStream
{
  public:
    explicit SpreadStream(uint32_t Seed) noexcept
        : Accumulator(Seed != 0u ? Seed : 1u)
    {
    }

    float Next() noexcept
    {
        Accumulator = Accumulator * 1664525u + 1013904223u;
        return float(Accumulator) / 4294967296.0f;
    }

  private:
    uint32_t Accumulator;
};

[[nodiscard]] std::array<float, 3> TransformPoint(const std::array<float, 16>& Transform, std::array<float, 3> Point) noexcept
{
    return { Transform[0] * Point[0] + Transform[4] * Point[1] + Transform[8] * Point[2] + Transform[12],
             Transform[1] * Point[0] + Transform[5] * Point[1] + Transform[9] * Point[2] + Transform[13],
             Transform[2] * Point[0] + Transform[6] * Point[1] + Transform[10] * Point[2] + Transform[14] };
}

[[nodiscard]] std::array<float, 3> TransformDirection(const std::array<float, 16>& Transform, std::array<float, 3> Direction) noexcept
{
    return { Transform[0] * Direction[0] + Transform[4] * Direction[1] + Transform[8] * Direction[2],
             Transform[1] * Direction[0] + Transform[5] * Direction[1] + Transform[9] * Direction[2],
             Transform[2] * Direction[0] + Transform[6] * Direction[1] + Transform[10] * Direction[2] };
}

} // namespace

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                         PRESETS
//--------------------------------------------------------------------------------------------------------------------------------------

std::span<const DerezPreset> DerezPresets() noexcept
{
    static constexpr DerezPreset Catalogue[] = {
        { "Derez cube", 0u, 0.3f, 1.0f, 0.5f, 0.6f, 2.6f, 0.4f, 3.5f, 0.075f, 1.2f, "#35d6ff", "#ffffff", "#ff2d6f" },
        { "Reverse derez", 0u, 0.15f, 0.8f, 0.3f, 0.4f, 1.8f, 0.6f, 2.8f, 0.07f, 1.3f, "#35d6ff", "#ffffff", "#ff2d6f" },
        { "Glitch dissolve", 1u, 0.1f, 0.9f, 0.2f, 0.2f, 1.2f, 1.2f, 2.0f, 0.06f, 1.0f, "#00ffd0", "#ffffff", "#ff2d6f" }
    };
    return std::span<const DerezPreset>(Catalogue, sizeof(Catalogue) / sizeof(Catalogue[0]));
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          COLOUR
//--------------------------------------------------------------------------------------------------------------------------------------

std::array<float, 3> ParseEffectColour(const char* Hex) noexcept
{
    uint32_t Value = 0u;
    if (Hex != nullptr)
    {
        for (const char* Character = Hex; *Character != '\0'; ++Character)
        {
            if (*Character == '#')
                continue;
            uint32_t Digit = 0u;
            if (*Character >= '0' && *Character <= '9')
                Digit = uint32_t(*Character - '0');
            else if (*Character >= 'a' && *Character <= 'f')
                Digit = uint32_t(*Character - 'a') + 10u;
            else if (*Character >= 'A' && *Character <= 'F')
                Digit = uint32_t(*Character - 'A') + 10u;
            else
                break;
            Value = Value * 16u + Digit;
        }
    }

    std::array<float, 3> Linear = {};
    for (uint32_t Channel = 0u; Channel < 3u; ++Channel)
    {
        const float Byte = float((Value >> (16u - 8u * Channel)) & 0xFFu) / 255.0f;
        Linear[Channel] = Byte <= 0.04045f ? Byte / 12.92f : std::pow((Byte + 0.055f) / 1.055f, 2.4f);
    }
    return Linear;
}

std::array<std::array<float, 12>, 3> ResolveDerezPalette(const EffectSettings& Settings) noexcept
{
    const auto Record = [&Settings](std::array<float, 3> Colour, float Luminance, std::array<float, 3> Albedo)
    {
        std::array<float, 12> Words = {};
        Words[0] = Albedo[0]; Words[1] = Albedo[1]; Words[2] = Albedo[2]; Words[3] = 0.3f;
        Words[4] = Colour[0] * Luminance * Settings.Intensity;
        Words[5] = Colour[1] * Luminance * Settings.Intensity;
        Words[6] = Colour[2] * Luminance * Settings.Intensity;
        Words[7] = 0.0f;
        Words[8] = 0.0f; Words[9] = 0.0f; Words[10] = 0.04f; Words[11] = 0.0f;
        return Words;
    };
    return { Record(Settings.Glow, 5.0f, { 0.05f, 0.10f, 0.12f }),
             Record(Settings.Hot, 10.0f, { 0.2f, 0.2f, 0.2f }),
             Record(Settings.Glitch, 5.0f, { 0.10f, 0.03f, 0.05f }) };
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          ANCHORS
//--------------------------------------------------------------------------------------------------------------------------------------

std::vector<float> SpreadEffectCells(std::span<const EffectFacet> Facets,
                                      uint32_t Count,
                                      const std::array<float, 16>* Transform) noexcept
{
    std::vector<float> Cells(size_t(Count) * EffectCellFloats, 0.0f);
    if (Facets.empty() || Count == 0u)
        return Cells;

    // 📝 Cumulative area, so a sample can be placed by binary search and the anchors cover the surface
    //    evenly rather than bunching on the small facets.
    std::vector<float> Cumulative(Facets.size(), 0.0f);
    std::vector<std::array<float, 3>> Normals(Facets.size(), std::array<float, 3>{ 0.0f, 0.0f, 1.0f });
    float Total = 0.0f;
    for (size_t Index = 0; Index < Facets.size(); ++Index)
    {
        const std::array<std::array<float, 3>, 3>& Corners = Facets[Index].Corners;
        const std::array<float, 3> U = { Corners[1][0] - Corners[0][0], Corners[1][1] - Corners[0][1], Corners[1][2] - Corners[0][2] };
        const std::array<float, 3> V = { Corners[2][0] - Corners[0][0], Corners[2][1] - Corners[0][1], Corners[2][2] - Corners[0][2] };
        const std::array<float, 3> Cross = { U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0] };
        const float Area = 0.5f * std::sqrt(Cross[0] * Cross[0] + Cross[1] * Cross[1] + Cross[2] * Cross[2]);
        Total += Area;
        Cumulative[Index] = Total;
        if (Area > 0.0f)
            Normals[Index] = { Cross[0] / (2.0f * Area), Cross[1] / (2.0f * Area), Cross[2] / (2.0f * Area) };
    }
    if (!(Total > 0.0f))
        return Cells;

    SpreadStream Random(0xC0FFEEu);
    for (uint32_t Sample = 0u; Sample < Count; ++Sample)
    {
        const float Target = (float(Sample) + Random.Next()) / float(Count) * Total;
        size_t Low = 0u;
        size_t High = Facets.size() - 1u;
        while (Low < High)
        {
            const size_t Middle = (Low + High) >> 1u;
            if (Cumulative[Middle] < Target)
                Low = Middle + 1u;
            else
                High = Middle;
        }

        const std::array<std::array<float, 3>, 3>& Corners = Facets[Low].Corners;
        float R1 = Random.Next();
        float R2 = Random.Next();
        if (R1 + R2 > 1.0f)
        {
            R1 = 1.0f - R1;
            R2 = 1.0f - R2;
        }
        std::array<float, 3> Point = {};
        for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
            Point[Axis] = Corners[0][Axis] + R1 * (Corners[1][Axis] - Corners[0][Axis]) + R2 * (Corners[2][Axis] - Corners[0][Axis]);
        std::array<float, 3> Normal = Normals[Low];
        if (Transform != nullptr)
        {
            Point = TransformPoint(*Transform, Point);
            Normal = TransformDirection(*Transform, Normal);
        }

        float* Cell = Cells.data() + size_t(Sample) * EffectCellFloats;
        Cell[0] = Point[0]; Cell[1] = Point[1]; Cell[2] = Point[2];
        // 📝 w: the facet's family — the colour the cube wears and glows in.
        const float Family = float(Facets[Low].Family);
        std::memcpy(&Cell[3], &Family, sizeof(Family));
        Cell[4] = Normal[0]; Cell[5] = Normal[1]; Cell[6] = Normal[2]; Cell[7] = 0.0f;
    }
    return Cells;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                           POSES
//--------------------------------------------------------------------------------------------------------------------------------------

EffectPoseSample InterpolateEffectPose(const EffectPoseSample& A,
                                        const EffectPoseSample& B,
                                        float Fraction,
                                        float Clock) noexcept
{
    const float Clamped = std::clamp(Fraction, 0.0f, 1.0f);
    const float Alignment = A.Rotation[0] * B.Rotation[0] + A.Rotation[1] * B.Rotation[1]
                          + A.Rotation[2] * B.Rotation[2] + A.Rotation[3] * B.Rotation[3];
    const float Sign = Alignment < 0.0f ? -1.0f : 1.0f;

    EffectPoseSample Result;
    Result.Clock = Clock;
    for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        Result.Position[Axis] = A.Position[Axis] + (B.Position[Axis] - A.Position[Axis]) * Clamped;
        Result.Velocity[Axis] = A.Velocity[Axis] + (B.Velocity[Axis] - A.Velocity[Axis]) * Clamped;
        Result.Angular[Axis] = A.Angular[Axis] + (B.Angular[Axis] - A.Angular[Axis]) * Clamped;
        Result.Rotation[Axis] = A.Rotation[Axis] + (Sign * B.Rotation[Axis] - A.Rotation[Axis]) * Clamped;
    }
    Result.Rotation[3] = A.Rotation[3] + (Sign * B.Rotation[3] - A.Rotation[3]) * Clamped;

    const float Magnitude = std::sqrt(Result.Rotation[0] * Result.Rotation[0] + Result.Rotation[1] * Result.Rotation[1]
                                    + Result.Rotation[2] * Result.Rotation[2] + Result.Rotation[3] * Result.Rotation[3]);
    const float Inverse = Magnitude > 1.0e-6f ? 1.0f / Magnitude : 1.0f;
    for (uint32_t Axis = 0u; Axis < 4u; ++Axis)
        Result.Rotation[Axis] *= Inverse;
    return Result;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                         SEQUENCE
//--------------------------------------------------------------------------------------------------------------------------------------

EffectSequence::EffectSequence()
    : Record(EffectRecordExtent, 0u),
      PoseTable(size_t(EffectPoseCapacity) * EffectPoseFloats, 0.0f)
{
    Controls.Glow = ParseEffectColour(DerezPresets()[0].Glow);
    Controls.Hot = ParseEffectColour(DerezPresets()[0].Hot);
    Controls.Glitch = ParseEffectColour(DerezPresets()[0].Glitch);
    Upload();
}

void EffectSequence::Clear() noexcept
{
    for (std::optional<EffectEmitterState>& Slot : Slots)
        Slot.reset();
    for (std::optional<uint32_t>& Entry : Order)
        Entry.reset();
    MaskMode = EffectMaskMode::Off;
    MaskStarted = 0.0f;
    MaskSeedWord = 1u;
    MaskSlot = -1;
    Recording.reset();
    Recorded = 0u;
    Flushed = 0u;
    Alive = 0u;
    Elapsed = 0.0f;
    Upload();
}

void EffectSequence::AssignBounds(std::array<float, 3> Minimum, std::array<float, 3> Maximum) noexcept
{
    BoundsLow = Minimum;
    BoundsHigh = Maximum;
    Upload();
}

float EffectSequence::QueryDuration(EffectKind Kind) const noexcept
{
    const bool Vehicle = Kind == EffectKind::VehicleDerez || Kind == EffectKind::VehicleRez;
    const float Scale = Vehicle ? 1.0f : 0.45f;
    if (Kind == EffectKind::Debris)
        return 4.0f;
    if (Kind == EffectKind::VehicleRez || Kind == EffectKind::BoxRez)
        return Scale * (0.3f + Controls.Flight * 1.15f) + 0.05f;
    return Scale * (Controls.HoldOffset + Controls.Wave * (Controls.Style == 1u ? 2.0f : 1.15f)) + Controls.Life * 1.15f + 1.2f;
}

float EffectSequence::QueryMaskDuration(EffectMaskMode Mode) const noexcept
{
    if (Mode == EffectMaskMode::Rez)
        return 0.3f + Controls.Flight * 1.15f + 0.4f;
    return Controls.HoldOffset + Controls.Wave * (Controls.Style == 1u ? 2.0f : 1.15f) + 0.1f;
}

uint32_t EffectSequence::Emit(EffectKind Kind,
                               std::array<float, 16> Transform,
                               std::array<float, 3> Velocity,
                               std::array<float, 3> Angular,
                               std::array<float, 3> Centre,
                               std::array<float, 3> Extent,
                               uint32_t Count)
{
    uint32_t Slot = EffectEmitterSlots;
    for (uint32_t Index = 0u; Index < EffectEmitterSlots; ++Index)
        if (!Slots[Index])
        {
            Slot = Index;
            break;
        }
    if (Slot == EffectEmitterSlots)
    {
        // 📝 All busy: the one closest to finishing gives way.
        Slot = 0u;
        for (uint32_t Index = 1u; Index < EffectEmitterSlots; ++Index)
            if (Slots[Index]->End < Slots[Slot]->End)
                Slot = Index;
    }

    Seed = Seed * 1664525u + 1013904223u;
    const uint32_t Chosen = Seed & 0x7FFFFFFFu;

    EffectEmitterState Emitter;
    Emitter.Kind = Kind;
    Emitter.Transform = Transform;
    Emitter.Velocity = Velocity;
    Emitter.Angular = Angular;
    Emitter.Centre = Centre;
    Emitter.Extent = Extent;
    Emitter.Count = Count;
    Emitter.Seed = Chosen;
    Emitter.Start = Elapsed;
    Emitter.End = Elapsed + QueryDuration(Kind);
    Slots[Slot] = Emitter;
    ++TotalEmitted;

    Alive = 0u;
    for (const std::optional<EffectEmitterState>& Occupied : Slots)
        if (Occupied)
            ++Alive;
    Upload();
    return Slot;
}

float EffectSequence::DerezVehicle(std::array<float, 16> BodyTransform,
                                    std::array<float, 3> LinearVelocity,
                                    std::array<float, 3> AngularVelocity,
                                    const EffectPoseSample& Now)
{
    const uint32_t Slot = Emit(EffectKind::VehicleDerez, BodyTransform, LinearVelocity, AngularVelocity,
                               { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, EffectVehicleCellCount);
    MaskMode = EffectMaskMode::Derez;
    MaskStarted = Elapsed;
    MaskSeedWord = Slots[Slot]->Seed;
    MaskSlot = int32_t(Slot);
    Recorded = 0u;
    Flushed = 0u;
    PoseStarted = Elapsed;
    if (Controls.Momentum)
    {
        Recording = Tracking{ Now };
        WritePose(Now);
    }
    else
    {
        Recording.reset();
    }
    Upload();
    return QueryMaskDuration(EffectMaskMode::Derez);
}

float EffectSequence::RezVehicle(std::array<float, 16> BodyTransform)
{
    const std::array<float, 3> Centre = { 0.5f * (BoundsLow[0] + BoundsHigh[0]),
                                          0.5f * (BoundsLow[1] + BoundsHigh[1]),
                                          0.5f * (BoundsLow[2] + BoundsHigh[2]) };
    const uint32_t Slot = Emit(EffectKind::VehicleRez, BodyTransform, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
                               Centre, { 1.0f, 1.0f, 1.0f }, EffectVehicleCellCount);
    MaskMode = EffectMaskMode::Rez;
    MaskStarted = Elapsed;
    MaskSeedWord = Slots[Slot]->Seed;
    MaskSlot = int32_t(Slot);
    Upload();
    return QueryMaskDuration(EffectMaskMode::Rez);
}

void EffectSequence::HideVehicle() noexcept
{
    MaskMode = EffectMaskMode::Hidden;
    Upload();
}

void EffectSequence::ClearVehicleMask() noexcept
{
    MaskMode = EffectMaskMode::Off;
    MaskSlot = -1;                        // the surface raster no longer needs the seed
    Upload();
}

void EffectSequence::DerezBox(std::array<float, 16> Transform,
                               std::array<float, 3> Centre,
                               std::array<float, 3> Extent,
                               std::array<float, 3> Velocity,
                               uint32_t Family)
{
    const uint32_t Slot = Emit(EffectKind::BoxDerez, Transform, Velocity, { 0.0f, 0.0f, 0.0f }, Centre, Extent, EffectBoxCellCount);
    const float Packed = float(Family);
    std::memcpy(&Slots[Slot]->Transform[3], &Packed, sizeof(Packed));
    Upload();
}

float EffectSequence::RezBox(std::array<float, 16> Transform,
                              std::array<float, 3> Centre,
                              std::array<float, 3> Extent,
                              uint32_t Family)
{
    const uint32_t Slot = Emit(EffectKind::BoxRez, Transform, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, Centre, Extent, EffectBoxCellCount);
    const float Packed = float(Family);
    std::memcpy(&Slots[Slot]->Transform[3], &Packed, sizeof(Packed));
    Upload();
    return QueryDuration(EffectKind::BoxRez);
}

void EffectSequence::BurstTyre(std::array<float, 16> Transform,
                                std::array<float, 3> Extent,
                                std::array<float, 3> Velocity,
                                uint32_t Family)
{
    const uint32_t Slot = Emit(EffectKind::Debris, Transform, Velocity, { 0.0f, 0.0f, 0.0f },
                               { 0.0f, 0.0f, 0.0f }, Extent, EffectBoxCellCount);
    const float Packed = float(Family);
    std::memcpy(&Slots[Slot]->Transform[3], &Packed, sizeof(Packed));
    Upload();
}

bool EffectSequence::ApplyPreset(const char* Name) noexcept
{
    if (Name == nullptr)
        return false;
    const std::span<const DerezPreset> Catalogue = DerezPresets();
    for (uint32_t Index = 0u; Index < Catalogue.size(); ++Index)
    {
        if (std::string(Catalogue[Index].Name) != std::string(Name))
            continue;
        const DerezPreset& Preset = Catalogue[Index];
        Controls.Preset = Index;
        Controls.Style = Preset.Style;
        Controls.HoldOffset = Preset.HoldOffset;
        Controls.Wave = Preset.Wave;
        Controls.Burst = Preset.Burst;
        Controls.SpeedMinimum = Preset.SpeedMinimum;
        Controls.SpeedMaximum = Preset.SpeedMaximum;
        Controls.Drag = Preset.Drag;
        Controls.Life = Preset.Life;
        Controls.Size = Preset.Size;
        Controls.Flight = Preset.Flight;
        Controls.Glow = ParseEffectColour(Preset.Glow);
        Controls.Hot = ParseEffectColour(Preset.Hot);
        Controls.Glitch = ParseEffectColour(Preset.Glitch);
        Upload();
        return true;
    }
    return false;
}

//--------------------------------------------------------------------------------------------------------------------------------------
//                                                          FRAME
//--------------------------------------------------------------------------------------------------------------------------------------

void EffectSequence::WritePose(const EffectPoseSample& Pose) noexcept
{
    if (Recorded >= EffectPoseCapacity)
        return;
    float* Words = PoseTable.data() + size_t(Recorded) * EffectPoseFloats;
    Words[0] = Pose.Position[0]; Words[1] = Pose.Position[1]; Words[2] = Pose.Position[2]; Words[3] = Pose.Clock;
    Words[4] = Pose.Rotation[0]; Words[5] = Pose.Rotation[1]; Words[6] = Pose.Rotation[2]; Words[7] = Pose.Rotation[3];
    Words[8] = Pose.Velocity[0]; Words[9] = Pose.Velocity[1]; Words[10] = Pose.Velocity[2]; Words[11] = 0.0f;
    Words[12] = Pose.Angular[0]; Words[13] = Pose.Angular[1]; Words[14] = Pose.Angular[2]; Words[15] = 0.0f;
    ++Recorded;
}

void EffectSequence::RecordPoses(const EffectPoseSample& Now)
{
    if (!Recording)
        return;
    const EffectPoseSample Prior = Recording->Prior;
    while (Recorded < EffectPoseCapacity)
    {
        const float Clock = PoseStarted + float(Recorded) * EffectPoseSpacing;
        if (Clock > Elapsed + 1.0e-6f)
            break;
        const float Fraction = std::clamp((Clock - Prior.Clock) / std::max(Now.Clock - Prior.Clock, 1.0e-6f), 0.0f, 1.0f);
        WritePose(InterpolateEffectPose(Prior, Now, Fraction, Clock));
    }
    Recording->Prior = Now;
    if (Recorded >= EffectPoseCapacity)
        Recording.reset();
    Upload();                                      // the pose count is read by the raster this frame
}

void EffectSequence::StopPoses() noexcept
{
    Recording.reset();
}

void EffectSequence::Advance(float Seconds)
{
    Elapsed += Seconds;
    FrameSeconds = std::max(Seconds, 1.0e-4f);

    uint32_t Live = 0u;
    for (uint32_t Slot = 0u; Slot < EffectEmitterSlots; ++Slot)
    {
        if (!Slots[Slot])
            continue;
        if (Elapsed > Slots[Slot]->End && MaskSlot == int32_t(Slot))
        {
            // 📝 The mask's emitter is kept until the mask itself is cleared: the surface raster reads
            //    its seed from slot 0 of the record.
            ++Live;
            continue;
        }
        if (Elapsed > Slots[Slot]->End)
        {
            Slots[Slot].reset();
            if (MaskSlot == int32_t(Slot))
                MaskSlot = -1;
            continue;
        }
        ++Live;
    }
    Alive = Live;
    Upload();
}

void EffectSequence::Upload() noexcept
{
    const auto WriteFloat = [this](uint32_t Index, float Value)
    {
        std::memcpy(Record.data() + size_t(Index) * sizeof(float), &Value, sizeof(Value));
    };

    WriteFloat(0u, Elapsed);
    const uint32_t Mode = uint32_t(MaskMode);
    std::memcpy(Record.data() + sizeof(float), &Mode, sizeof(Mode));
    WriteFloat(2u, MaskStarted);
    const uint32_t Style = Controls.Style;
    std::memcpy(Record.data() + 3u * sizeof(float), &Style, sizeof(Style));

    WriteFloat(4u, BoundsLow[0]); WriteFloat(5u, BoundsLow[1]); WriteFloat(6u, BoundsLow[2]); WriteFloat(7u, Controls.HoldOffset);
    WriteFloat(8u, BoundsHigh[0]); WriteFloat(9u, BoundsHigh[1]); WriteFloat(10u, BoundsHigh[2]); WriteFloat(11u, FrameSeconds);
    WriteFloat(12u, Controls.Burst); WriteFloat(13u, Controls.SpeedMinimum);
    WriteFloat(14u, Controls.SpeedMaximum); WriteFloat(15u, Controls.Drag);
    WriteFloat(16u, Controls.Life); WriteFloat(17u, Controls.Size); WriteFloat(18u, Controls.Wave); WriteFloat(19u, Controls.Flight);
    WriteFloat(20u, Controls.Faces ? 1.0f : 0.0f);
    WriteFloat(21u, Controls.GlowStrength * Controls.Intensity);
    WriteFloat(22u, Recorded >= 2u ? float(Recorded) : 0.0f);
    WriteFloat(23u, PoseStarted);

    // 📝 Slot 0 also carries the mask's seed (the surface raster reads Emitters[0].Centre.w), so the
    //    mask's emitter always goes into slot 0 of the record — the others follow in any order.
    for (uint32_t Index = 0u; Index < EffectEmitterSlots; ++Index)
        Order[Index] = Index < Slots.size() ? std::optional<uint32_t>(Index) : std::nullopt;
    if (MaskSlot > 0)
    {
        std::optional<uint32_t> MaskEntry = Order[0u];
        Order[0u] = Order[uint32_t(MaskSlot)];
        Order[uint32_t(MaskSlot)] = MaskEntry;
    }

    for (uint32_t Index = 0u; Index < EffectEmitterSlots; ++Index)
    {
        const uint32_t Base = (EffectRecordHeader + Index * EffectEmitterStride) / 4u;
        const std::optional<uint32_t>& Slot = Order[Index];
        if (!Slot || !Slots[*Slot])
        {
            // 📝 An empty slot is cleared, so nothing of the emitter that was there survives: no kind
            //    and no particles. Slot zero still carries the mask's seed.
            for (uint32_t Word = 0u; Word < 32u; ++Word)
                WriteFloat(Base + Word, 0.0f);
            const float NoKind = 255.0f;
            std::memcpy(Record.data() + size_t(Base + 23u) * sizeof(float), &NoKind, sizeof(NoKind));
            const float MaskSeed = Index == 0u ? float(MaskSeedWord) : 0.0f;
            std::memcpy(Record.data() + size_t(Base + 31u) * sizeof(float), &MaskSeed, sizeof(MaskSeed));
            continue;
        }

        const EffectEmitterState& Emitter = *Slots[*Slot];
        for (uint32_t Word = 0u; Word < 16u; ++Word)
            WriteFloat(Base + Word, Emitter.Transform[Word]);
        WriteFloat(Base + 16u, Emitter.Velocity[0]);
        WriteFloat(Base + 17u, Emitter.Velocity[1]);
        WriteFloat(Base + 18u, Emitter.Velocity[2]);
        WriteFloat(Base + 19u, Emitter.Start);
        WriteFloat(Base + 20u, Emitter.Angular[0]);
        WriteFloat(Base + 21u, Emitter.Angular[1]);
        WriteFloat(Base + 22u, Emitter.Angular[2]);
        const float Kind = float(uint32_t(Emitter.Kind));
        std::memcpy(Record.data() + size_t(Base + 23u) * sizeof(float), &Kind, sizeof(Kind));
        WriteFloat(Base + 24u, Emitter.Extent[0]);
        WriteFloat(Base + 25u, Emitter.Extent[1]);
        WriteFloat(Base + 26u, Emitter.Extent[2]);
        const float Count = float(Emitter.Count);
        std::memcpy(Record.data() + size_t(Base + 27u) * sizeof(float), &Count, sizeof(Count));
        WriteFloat(Base + 28u, Emitter.Centre[0]);
        WriteFloat(Base + 29u, Emitter.Centre[1]);
        WriteFloat(Base + 30u, Emitter.Centre[2]);
        const float SeedWord = (Index == 0u && MaskSlot < 0) ? float(MaskSeedWord) : float(Emitter.Seed);
        std::memcpy(Record.data() + size_t(Base + 31u) * sizeof(float), &SeedWord, sizeof(SeedWord));
    }
}

std::span<const float> EffectSequence::Poses() const noexcept
{
    return std::span<const float>(PoseTable.data(), size_t(Recorded) * EffectPoseFloats);
}

std::vector<std::string> EffectSequence::QueryReadout() const
{
    static const char* Names[] = { "off", "derez", "rez", "hidden" };
    std::ostringstream First;
    First << "emitters live: " << Alive << " of " << EffectEmitterSlots << " · emitted " << TotalEmitted
          << " · car mask " << Names[uint32_t(MaskMode) < 4u ? uint32_t(MaskMode) : 0u];
    std::ostringstream Second;
    Second << "car anchors " << EffectVehicleCellCount << " · cone anchors " << EffectBoxCellCount
           << " · style " << (Controls.Style == 1u ? "glitch" : "cubes")
           << " · " << (Controls.Faces ? "face colours" : "palette");
    std::ostringstream Third;
    Third << "momentum " << (Controls.Momentum ? "on" : "off") << " · poses " << Recorded
          << (Recording ? " (recording)" : "");
    return { First.str(), Second.str(), Third.str() };
}

} // namespace Frontier
