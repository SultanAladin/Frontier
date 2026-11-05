/*==========================================================================================================================================
                                                         EFFECT SEQUENCE
                                     ------------------------------------------------------------------
                    Derez and rez — the car (and any cone) coming apart into glowing cubes
                     and flying back together, after the Particle Editor's presets.
==========================================================================================================================================*/

// 🧩 Derez and rez: the portable (CPU) form of the demo's EffectSequence.js, 1:1.
//
// 📝 The Particle Editor's "Derez cube", "Reverse derez" and "Glitch dissolve" presets, brought into
//    the renderer:
//
//      • the surface raster masks the car cell by cell on one release clock (EffectRecords.slang) —
//        glowing white-hot, then cyan, then gone;
//      • the effect raster draws a cube from every anchor of a released cell, its flight a closed-form
//        function of the clock (EffectRaster.slang), into the same surface projection — so the cubes
//        are lit, glow, reflect and bounce light in the GI like any emissive surface;
//      • nothing is simulated or read back: an emitter is 128 bytes of uniform and one instanced draw
//        while it lasts.
//
//    The car's anchors are 1 920 points spread over its own facets by area (body and wheels), uploaded
//    once. A cone (or any box) uses a 6 × 6 grid on each face of its bounds. Eight emitters run at
//    once.
//
// 📝 Nothing here touches a device. The caller copies RecordBytes() into its own uniform binding and
//    the pose table into its own storage binding, which is what makes the effect testable on a
//    headless host.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

inline constexpr uint32_t EffectEmitterSlots = 8;              // [-]
inline constexpr uint32_t EffectEmitterStride = 128;           // [B]  one emitter
inline constexpr uint32_t EffectRecordHeader = 96;             // [B]  EffectRecord up to Emitters
inline constexpr uint32_t EffectRecordExtent = EffectRecordHeader + EffectEmitterSlots * EffectEmitterStride;   // 1120
inline constexpr uint32_t EffectPoseCapacity = 360;            // [-]  6 s of the car's motion at 60 Hz
inline constexpr float EffectPoseSpacing = 1.0f / 60.0f;       // [s]
inline constexpr uint32_t EffectPoseFloats = 16;               // [f32] one EffectPose
inline constexpr uint32_t EffectPoseBytes = EffectPoseFloats * 4u;

inline constexpr uint32_t EffectVehicleBodyCells = 1536;       // [-]   anchors over the body's facets
inline constexpr uint32_t EffectVehicleWheelCells = 96;        // [-]   per wheel
inline constexpr uint32_t EffectVehicleCellCount = EffectVehicleBodyCells + 4u * EffectVehicleWheelCells;   // 1920
inline constexpr uint32_t EffectBoxCellCount = 216;            // [-]   6 × 6 per face of a box
inline constexpr uint32_t EffectCellFloats = 8;                // [f32] local anchor (w family), local normal (w spare)

inline constexpr uint32_t EffectParticlesPerEmitter = 4096;    // [-]   the instanced draw's stride
inline constexpr uint32_t EffectCornerCount = 36 * 9;          // [-]   36 corners × (parent + eight children)

// 📝 What an emitter does.
enum class EffectKind : uint32_t
{
    VehicleDerez = 0,
    VehicleRez = 1,
    BoxDerez = 2,
    BoxRez = 3,
    Debris = 4            // a burst tyre's rubber: released at once, thrown hard, never glowing
};

// 📝 What the surface raster does with the car.
enum class EffectMaskMode : uint32_t
{
    Off = 0,
    Derez = 1,
    Rez = 2,
    Hidden = 3
};

// 📝 The three Particle Editor presets, in this renderer's terms. Their timings (a 0.6 s hold and a
//    1.6 s wave for one showcase cube) are scaled to a respawn's pace — about a second and a half
//    each way — keeping each preset's character.
struct DerezPreset
{
    const char* Name = "";
    uint32_t Style = 0u;          // 0 cubes, 1 glitch
    float HoldOffset = 0.3f;      // [s]
    float Wave = 1.0f;            // [s]   spread of the release wave across the bounds
    float Burst = 0.5f;           // [m/s] launch along the surface normal
    float SpeedMinimum = 0.6f;    // [m/s] slowest scattered launch
    float SpeedMaximum = 2.6f;    // [m/s] fastest
    float Drag = 0.4f;            // [1/s]
    float Life = 3.5f;            // [s]   a cube's life once released
    float Size = 0.075f;          // [m]   cube edge
    float Flight = 1.2f;          // [s]   a rez cube's flight
    const char* Glow = "#35d6ff";
    const char* Hot = "#ffffff";
    const char* Glitch = "#ff2d6f";
};

[[nodiscard]] std::span<const DerezPreset> DerezPresets() noexcept;

// 📝 The editor's effect controls. Faces: the cubes are the car's own facets — their colours, glowing
//    in them — rather than the palette. Momentum: the car carries on moving while it comes apart, and
//    every cube leaves with the car's velocity at its release.
struct EffectSettings
{
    uint32_t Preset = 0u;
    uint32_t Style = 0u;
    float HoldOffset = 0.3f;
    float Wave = 1.0f;
    float Burst = 0.5f;
    float SpeedMinimum = 0.6f;
    float SpeedMaximum = 2.6f;
    float Drag = 0.4f;
    float Life = 3.5f;
    float Size = 0.075f;
    float Flight = 1.2f;
    std::array<float, 3> Glow = { 0.035f, 0.67f, 1.0f };
    std::array<float, 3> Hot = { 1.0f, 1.0f, 1.0f };
    std::array<float, 3> Glitch = { 1.0f, 0.026f, 0.159f };
    float Intensity = 1.0f;
    bool Faces = true;
    bool Momentum = true;
    float GlowStrength = 6.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                         COLOUR
//------------------------------------------------------------------------------------------------------------------------

// 📐 "#rrggbb" (sRGB) → linear Rec.709.
[[nodiscard]] std::array<float, 3> ParseEffectColour(const char* Hex) noexcept;

// 📝 The three palette material records (families DerezGlow / DerezHot / DerezGlitch), twelve floats
//    each in the order the demo writes them: albedo rgb and 0.3, emission rgb and 0, then four
//    spare words. Luminance is the family's (5 for glow and glitch, 10 for the white-hot core).
[[nodiscard]] std::array<std::array<float, 12>, 3> ResolveDerezPalette(const EffectSettings& Settings) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                         ANCHORS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One facet of the surface the anchors are spread over.
struct EffectFacet
{
    std::array<std::array<float, 3>, 3> Corners = {};
    uint32_t Family = 0u;
};

// 📝 Anchors spread over a facet set by area (deterministic), EffectCellFloats floats each: the local
//    position with the facet's family in w (the colour a cube wears), then the local normal.
//    Transform, when given, is a column-major 4 × 4 taking the anchors from the facet set's frame into
//    the emitter's local frame.
[[nodiscard]] std::vector<float> SpreadEffectCells(std::span<const EffectFacet> Facets,
                                                    uint32_t Count,
                                                    const std::array<float, 16>* Transform = nullptr) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                          POSES
//------------------------------------------------------------------------------------------------------------------------

// 📝 One sample of the car's motion during a derez, on the effect clock: centre of mass and clock,
//    the body → world quaternion (x, y, z, w), the linear velocity and the angular velocity.
struct EffectPoseSample
{
    std::array<float, 3> Position = {};
    float Clock = 0.0f;
    std::array<float, 4> Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3> Velocity = {};
    std::array<float, 3> Angular = {};
};

// 📝 Interpolates between two samples, taking the shorter arc through the quaternion's double cover.
[[nodiscard]] EffectPoseSample InterpolateEffectPose(const EffectPoseSample& A,
                                                      const EffectPoseSample& B,
                                                      float Fraction,
                                                      float Clock) noexcept;

//------------------------------------------------------------------------------------------------------------------------
//                                                        SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

// 📝 One emitter, as the host holds it. Transform is column-major local → world; its [0].w carries the
//    family for a box emitter (the affine transform leaves it free).
struct EffectEmitterState
{
    EffectKind Kind = EffectKind::VehicleDerez;
    std::array<float, 16> Transform = { 1.0f, 0.0f, 0.0f, 0.0f,
                                        0.0f, 1.0f, 0.0f, 0.0f,
                                        0.0f, 0.0f, 1.0f, 0.0f,
                                        0.0f, 0.0f, 0.0f, 1.0f };
    std::array<float, 3> Velocity = {};
    std::array<float, 3> Angular = {};
    std::array<float, 3> Centre = {};
    std::array<float, 3> Extent = { 1.0f, 1.0f, 1.0f };
    uint32_t Count = 0u;
    uint32_t Seed = 1u;
    float Start = 0.0f;
    float End = 0.0f;
};

class EffectSequence
{
  public:
    EffectSequence();

    // 📝 Seconds an emitter of this kind lives: the latest release or arrival plus a cube's flight and
    //    its children.
    [[nodiscard]] float QueryDuration(EffectKind Kind) const noexcept;

    // 📝 Seconds until every cell of the car has let go (derez) or is back (rez) — when the mask is
    //    complete.
    [[nodiscard]] float QueryMaskDuration(EffectMaskMode Mode) const noexcept;

    // 📝 Claims a slot — a free one, or the one closest to finishing when all eight are busy — and
    //    returns its index.
    uint32_t Emit(EffectKind Kind,
                  std::array<float, 16> Transform,
                  std::array<float, 3> Velocity,
                  std::array<float, 3> Angular,
                  std::array<float, 3> Centre,
                  std::array<float, 3> Extent,
                  uint32_t Count);

    // 📝 The car dissolves: mask on, one emitter carrying the car's velocity and spin into its cubes.
    //    With momentum the car keeps moving and its pose is recorded from now on, so each cube leaves
    //    from where the car is at its cell's release, with the car's velocity there. Returns the mask's
    //    duration, which is when the caller may hide the car.
    float DerezVehicle(std::array<float, 16> BodyTransform,
                       std::array<float, 3> LinearVelocity,
                       std::array<float, 3> AngularVelocity,
                       const EffectPoseSample& Now);

    // 📝 The car assembles: cubes fly in from a shell around it. Returns the mask's duration, which is
    //    when the caller may show the car again.
    float RezVehicle(std::array<float, 16> BodyTransform);

    // 📝 The car is gone (derez finished) or back (rez finished).
    void HideVehicle() noexcept;
    void ClearVehicleMask() noexcept;

    // 📝 A box-shaped thing (a cone) bursting into cubes, or assembling from them. Family is what the
    //    cubes wear with face colours (a cone's orange picks its band and base by height).
    void DerezBox(std::array<float, 16> Transform,
                  std::array<float, 3> Centre,
                  std::array<float, 3> Extent,
                  std::array<float, 3> Velocity = { 0.0f, 0.0f, 0.0f },
                  uint32_t Family = 60u);
    float RezBox(std::array<float, 16> Transform,
                 std::array<float, 3> Centre,
                 std::array<float, 3> Extent,
                 uint32_t Family = 60u);

    // 📝 A tyre letting go: rubber chunks thrown out of the wheel's box at once, with the car's
    //    velocity (no glow).
    void BurstTyre(std::array<float, 16> Transform,
                   std::array<float, 3> Extent,
                   std::array<float, 3> Velocity,
                   uint32_t Family = 9u);

    // 📝 Applies a preset by name; returns false for an unknown name.
    bool ApplyPreset(const char* Name) noexcept;

    // 📝 One frame: advances the clock, records the 60 Hz pose samples the derez needs and retires
    //    finished emitters. FrameSeconds is the real elapsed time (motion vectors).
    void Advance(float Seconds);

    // 📝 After the vehicle steps: every 60 Hz sample time since the last frame, interpolated between
    //    the frames' poses.
    void RecordPoses(const EffectPoseSample& Now);

    // 📝 Ends the recording (the car is hidden, then moved); the samples stay for the cubes still in
    //    flight.
    void StopPoses() noexcept;

    [[nodiscard]] EffectSettings& Settings() noexcept { return Controls; }
    [[nodiscard]] const EffectSettings& Settings() const noexcept { return Controls; }

    [[nodiscard]] float Clock() const noexcept { return Elapsed; }
    [[nodiscard]] uint32_t Live() const noexcept { return Alive; }
    [[nodiscard]] uint32_t Emitted() const noexcept { return TotalEmitted; }
    [[nodiscard]] EffectMaskMode Mask() const noexcept { return MaskMode; }
    [[nodiscard]] float MaskStart() const noexcept { return MaskStarted; }
    [[nodiscard]] uint32_t MaskSeed() const noexcept { return MaskSeedWord; }

    // 📝 The record the effect raster reads: EffectRecordExtent bytes, rewritten every frame.
    [[nodiscard]] std::span<const uint8_t> RecordBytes() const noexcept { return Record; }

    // 📝 The pose table, EffectPoseFloats floats per sample, in clock order.
    [[nodiscard]] std::span<const float> Poses() const noexcept;
    [[nodiscard]] uint32_t PoseCount() const noexcept { return Recorded; }
    [[nodiscard]] uint32_t FlushedPoses() const noexcept { return Flushed; }
    [[nodiscard]] bool IsRecording() const noexcept { return Recording.has_value(); }

    // 📝 The slots to draw, in record order: the mask's emitter first, because the surface raster reads
    //    its seed from Emitters[0].Centre.w. A nullopt entry is an empty slot.
    [[nodiscard]] const std::array<std::optional<uint32_t>, EffectEmitterSlots>& DrawOrder() const noexcept { return Order; }
    [[nodiscard]] const std::array<std::optional<EffectEmitterState>, EffectEmitterSlots>& Emitters() const noexcept { return Slots; }

    // 📝 The car's local bounds the mask and the anchors share.
    void AssignBounds(std::array<float, 3> Minimum, std::array<float, 3> Maximum) noexcept;
    [[nodiscard]] const std::array<float, 3>& BoundsMinimum() const noexcept { return BoundsLow; }
    [[nodiscard]] const std::array<float, 3>& BoundsMaximum() const noexcept { return BoundsHigh; }

    [[nodiscard]] std::vector<std::string> QueryReadout() const;

    // 📝 Forgets every emitter, the mask and the recorded poses.
    void Clear() noexcept;

  private:
    void Upload() noexcept;
    void WritePose(const EffectPoseSample& Pose) noexcept;

    EffectSettings Controls = {};
    float Elapsed = 0.0f;
    float FrameSeconds = 1.0f / 60.0f;
    std::array<std::optional<EffectEmitterState>, EffectEmitterSlots> Slots;
    std::array<std::optional<uint32_t>, EffectEmitterSlots> Order;
    EffectMaskMode MaskMode = EffectMaskMode::Off;
    float MaskStarted = 0.0f;
    uint32_t MaskSeedWord = 1u;
    int32_t MaskSlot = -1;
    std::array<float, 3> BoundsLow = { -2.5f, -1.0f, -0.6f };
    std::array<float, 3> BoundsHigh = { 2.5f, 1.0f, 0.9f };
    std::vector<uint8_t> Record;
    std::vector<float> PoseTable;
    uint32_t Recorded = 0u;
    uint32_t Flushed = 0u;
    float PoseStarted = 0.0f;
    uint32_t Alive = 0u;
    uint32_t TotalEmitted = 0u;
    struct Tracking
    {
        EffectPoseSample Prior = {};
    };
    std::optional<Tracking> Recording;
    uint32_t Seed = 0x9E3779B9u;
};

} // namespace Frontier
