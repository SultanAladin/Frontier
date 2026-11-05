/*==========================================================================================================================================
                                                             PLUME SPACE
                                     ------------------------------------------------------------------
                  The smoke-and-fire cell lattices: toroidal windows that follow the car, one
                      fixed-rate step at no more than 60 Hz, and a half-rate march.
==========================================================================================================================================*/

// 🧩 Smoke and fire: the portable (CPU) form of the demo's PlumeSpace.js, 1:1.
//
// 📝 Frontier's tyre smoke (VolumetricDynamics/SmokeSequence.h + SmokeTransport.slang) solves a 32³
//    velocity field on the CPU at 60 Hz with 12 cold-started Jacobi sweeps, uploads it every tick,
//    advects a dense display volume over every one of its cells on the device, relocates both lattices
//    by a full copy when the car leaves the guard band, and marches every display pixel. The same
//    physics here costs far less:
//
//    | Concern            | Frontier (C++)                               | This exhibit                                     |
//    |--------------------|----------------------------------------------|--------------------------------------------------|
//    | velocity solve     | CPU, 32³, uploaded every tick                | device, never read back or uploaded              |
//    | pressure           | 12 Jacobi sweeps from zero each tick         | warm-started sweeps on the toroidal lattice      |
//    | moving window      | full-lattice copy on relocation (both sides) | toroidal relabelling, zero copies                |
//    | obstruction        | separate occupancy intake + chassis box      | one sample of the scene distance field, moving   |
//    |                    |                                              | occupants contribute their point velocity        |
//    | idle cost          | lattice resident until faint                 | no pass recorded at all once the plume has faded |
//    | march              | every display pixel, shadow march per sample | trace resolution, coverage-clipped, TAA-resolved |
//    | quality            | fixed at build                               | four grades, rebuilt live without a reload       |
//
//    One step runs at most once per frame at no more than 60 Hz (a 120 Hz display steps every other
//    frame), its duration the time accumulated since the last step, clamped to 1/30 s — semi-Lagrangian
//    transport is unconditionally stable, so a slow frame is one longer step rather than a spiral of
//    short ones.
//
// 📝 The host fills the 256-byte PlumeRecord and the 144-byte FireLightRecord below; the device owns
//    every lattice. Nothing here touches a GPU: the caller copies Bytes() into its own uniform binding.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Frontier
{

inline constexpr uint32_t PlumeRecordBytes = 256;    // [B]   PlumeRecord: sixteen vec4
inline constexpr uint32_t PlumeFireLightBytes = 144; // [B]   FireLightRecord
inline constexpr uint32_t PlumeFireLightLimit = 4;   // [-]
inline constexpr float PlumeStepSeconds = 1.0f / 60.0f;   // [s] shortest step
inline constexpr float PlumeStepCeiling = 1.0f / 30.0f;   // [s] longest step
inline constexpr float PlumeFadeCeiling = 12.0f;          // [s] gone this long after the last source
inline constexpr int32_t PlumeRestartOrigin = 1 << 24;    // [cell] a prior window nothing lies in
inline constexpr uint32_t PlumeDetailExtent = 32;         // [texel] tiling detail-noise volume edge
inline constexpr float PlumeFloorHeight = -0.5f;          // [m] the window's floor, under the course
inline constexpr float PlumeFlameScale = 80.0f;           // [-] flame emission at PlumeFire 1

inline constexpr uint32_t PlumeSourceStride = 12;    // [f32] position·radius, velocity·spare, 4 rates
inline constexpr uint32_t PlumeSourceCapacity = 24;  // [-]

// 📝 The smoke-and-fire quality slider — independent of the GI grade. Extent [fine cells, each even],
//    CellExtent [m], PressureSweeps (even), MarchSamples. A wider window keeps a long burnout's cloud
//    resident; a finer cell resolves the curls.
struct PlumeGrade
{
    const char* Name = "Medium";
    std::array<uint32_t, 3> Extent = { 96u, 96u, 24u };   // [cells] fine (thermo) lattice
    float CellExtent = 0.30f;                              // [m] fine cell edge
    uint32_t PressureSweeps = 12u;                         // [-] red-black Jacobi sweeps, even
    uint32_t MarchSamples = 40u;                           // [-] samples along the march
};

inline constexpr std::array<PlumeGrade, 4> PlumeGrades = {{
    { "Low",    { 64u, 64u, 20u },  0.40f,  8u, 32u },
    { "Medium", { 96u, 96u, 24u },  0.30f, 12u, 40u },
    { "High",   { 128u, 128u, 32u }, 0.25f, 16u, 48u },
    { "Ultra",  { 160u, 160u, 48u }, 0.20f, 20u, 64u }
}};

// 📝 Every transport and optics reading the Smoke & fire editor exposes, at the values the exhibit
//    opens with. CoarseGasSettings and the tyre_burnout preset seeded them; the soot pair, the octaves
//    and the detail are new.
struct PlumeSettings
{
    bool PlumeEnabled = true;
    int32_t Inspection = 0;

    float PlumeEmissionRate = 1.0f;
    float PlumeDensity = 1.2f;
    float PlumeHeat = 1.0f;
    float PlumeBuoyancy = 2.4f;
    float PlumeExtinction = 3.6f;
    float PlumeAlbedo = 0.88f;
    float PlumeFire = 1.0f;
    float PlumeSoot = 1.0f;
    float PlumeSootAlbedo = 0.05f;
    float PlumeVorticity = 3.0f;
    float PlumeTurbulence = 0.6f;
    float PlumeTurbulenceScale = 3.7f;
    float PlumeDissipation = 0.10f;
    float PlumeCooling = 0.55f;
    float PlumeFlameDecay = 4.0f;
    float PlumeWindSpeed = 0.8f;
    float PlumeWindDirection = 30.0f;
    float PlumeScattering = 0.75f;
    float PlumeAnisotropy = 0.38f;
    float PlumeAmbient = 1.0f;
    float PlumeFireLight = 1.0f;
    float PlumeDetail = 0.7f;
    float PlumeDetailScale = 2.2f;
    float PlumeFlameCore = 1800.0f;
    float PlumeFlameTip = 1150.0f;
    float PlumeEmbers = 1.0f;
};

// 📝 One fire light: a world position, a reach, and a linear radiance. Brightest first; the record
//    keeps the first PlumeFireLightLimit of them.
struct PlumeFireLight
{
    std::array<float, 3> Position = { 0.0f, 0.0f, 0.0f };
    float Radius = 0.0f;
    std::array<float, 3> Radiance = { 0.0f, 0.0f, 0.0f };
};

class PlumeSpace
{
  public:
    explicit PlumeSpace(const PlumeGrade& Configuration = PlumeGrades[2]);

    // 📝 Switches to another grade: new lattices, a clean plume. The programs do not depend on the
    //    extents — the record carries them — so a grade change costs allocations, never compiles.
    void ReconstructGrade(const PlumeGrade& Configuration);

    // 📝 Called once per frame with the sources of this frame. Decides residency and whether a step
    //    runs, moves the window when the focus leaves its middle half, and writes the step record, the
    //    sources and the coverage reset. Sources is PlumeSourceCapacity × PlumeSourceStride floats.
    void Schedule(float Seconds,
                  float FrameSeconds,
                  std::array<float, 3> Focus,
                  std::span<const float> Sources,
                  uint32_t SourceCount,
                  const PlumeSettings& Settings,
                  std::array<float, 3> SunDirection = { 0.0f, 0.0f, 1.0f });

    // 📝 Marks that the transport of the pending step has been recorded on the device, flipping the
    //    state parity the way RecordTransport does at the end of its pass.
    void CommitStep() noexcept;

    // 📝 Lights: brightest first; written every frame, resident or not, because the direct radiance
    //    pass lights the scene with them.
    void AssignFireLights(std::span<const PlumeFireLight> Lights);

    // 📝 Forgets the plume at once: the next source restarts a clean lattice.
    void Clear() noexcept;

    [[nodiscard]] std::span<const uint8_t> RecordBytes() const noexcept { return Record; }
    [[nodiscard]] std::span<const uint8_t> FireLightBytes() const noexcept { return FireLights; }
    [[nodiscard]] std::span<const uint8_t> SourceBytes() const noexcept { return Sources; }

    [[nodiscard]] bool IsResident() const noexcept { return Resident; }
    [[nodiscard]] bool IsStepPending() const noexcept { return StepPending; }
    [[nodiscard]] uint32_t State() const noexcept { return Parity; }
    [[nodiscard]] uint32_t QueryFireLightCount() const noexcept { return FireLightCount; }
    [[nodiscard]] const PlumeGrade& Configuration() const noexcept { return Grade; }

    [[nodiscard]] std::array<uint32_t, 3> ThermoExtent() const noexcept { return Thermo; }
    [[nodiscard]] std::array<uint32_t, 3> VelocityExtent() const noexcept { return Velocity; }
    [[nodiscard]] std::array<int32_t, 2> Origin() const noexcept { return Window; }
    [[nodiscard]] std::array<int32_t, 2> PriorOrigin() const noexcept { return Prior; }
    [[nodiscard]] uint32_t StepsTaken() const noexcept { return Steps; }

    // 📝 The dispatch counts: one workgroup per 4 cells on each lattice (the demo's ceil(cells / 4)).
    [[nodiscard]] std::array<uint32_t, 3> CoarseDispatch() const noexcept;
    [[nodiscard]] std::array<uint32_t, 3> FineDispatch() const noexcept;
    [[nodiscard]] std::array<uint32_t, 3> MarchDispatch(std::array<uint32_t, 2> TraceExtent) const noexcept;

    // 📝 A human-readable description of the resident lattices (the editor's readout).
    [[nodiscard]] std::string QueryDescription() const;

    // 📐 The tiling detail volume: three octaves of periodic value noise and one of inverted Worley
    //    cells, so the march can fold them into billows. Periodic in every axis, so a repeat sampler
    //    never shows a seam. Returns PlumeDetailExtent³ RGBA8 texels, x fastest then y then z.
    [[nodiscard]] static std::vector<uint8_t> ConstructDetailVolume();

  private:
    void RecentreWindow(std::array<float, 3> Focus, bool Force);
    void EncodeRecord(float Step, float FrameSeconds, uint32_t SourceCount, const PlumeSettings& Settings);

    PlumeGrade Grade{};
    std::array<uint32_t, 3> Thermo = {};
    std::array<uint32_t, 3> Velocity = {};
    float CoarseEdge = 0.0f;
    std::array<int32_t, 2> Window = { 0, 0 };
    std::array<int32_t, 2> Prior = { PlumeRestartOrigin, PlumeRestartOrigin };

    std::vector<uint8_t> Record;
    std::vector<uint8_t> FireLights;
    std::vector<uint8_t> Sources;
    std::array<float, 3> Sun = { 0.0f, 0.0f, 1.0f };

    bool Resident = false;
    bool StepPending = false;
    uint32_t Parity = 0u;
    float Accumulator = 0.0f;
    float QuietSeconds = 0.0f;
    float QuietPrior = 0.0f;
    uint32_t StepOrdinal = 0u;
    float ElapsedSeconds = 0.0f;
    uint32_t Steps = 0u;
    uint32_t FireLightCount = 0u;
};

} // namespace Frontier
