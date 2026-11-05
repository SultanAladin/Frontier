#pragma once
/*==========================================================================================================================================
                                                             VEHICLECABIN.H
==========================================================================================================================================*/
// 🧩 The parts of the vehicle a driver touches: a steering wheel driven by steer demand, and the in-cabin
//    dashboard tablet hosting the Launcher and single-vehicle Garage configurator.
//    Default tablet state remains Content::Empty for headless session parity; opening Content::Garage
//    exposes the paint-only 8-swatch configurator, orbiting single-vehicle camera, 3D raycast swatch drop,
//    and X-Ray inspection controls.

#include "Frontier/Engine/Effects/MaterialTransition.h"
#include "Frontier/Engine/Effects/VehicleInspection.h"
#include "Frontier/Engine/Input/InputPacket.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Frontier
{

struct SteeringWheel
{
    float AngleDegrees = 0.0f;      // [-] current lock, turned by steer demand
    float MaximumLockDegrees = 540.0f;

    void ApplySteer(float SteerAxis) noexcept
    {
        AngleDegrees = std::clamp(SteerAxis, -1.0f, 1.0f) * MaximumLockDegrees;
    }
};

struct GarageConfiguratorState
{
    static constexpr uint32_t PaintSwatchCount = static_cast<uint32_t>(ConfiguratorPaintFinishes.size());
    static constexpr float    DefaultAutoOrbitDegreesPerSecond = 11.5f;

    // Paint-only configurator controls (8 exterior body finishes; wheel/interior/trim tabs intentionally omitted).
    MaterialTransition PaintRipple{};
    VehicleInspection  Inspection{};

    // Single-vehicle configurator camera orbiting around the hero vehicle.
    float OrbitYawDegrees = 34.0f;
    float OrbitPitchDegrees = 15.5f;
    float OrbitDistanceMetres = 6.35f;
    bool  AutoOrbit = true;
    bool  OrbitDragging = false;

    // Active drag-and-drop swatch state.
    bool     DraggingSwatch = false;
    uint32_t DraggedSwatchIndex = 0;

    void SelectPaintSwatch(uint32_t SwatchIndex) noexcept
    {
        PaintRipple.TriggerFinish(SwatchIndex, nullptr);
    }

    bool DropPaintSwatchOnBody(uint32_t SwatchIndex,
                               const FacetArchiveData& BodyArchive,
                               const std::array<float, 3>& RayOriginBodyLocal,
                               const std::array<float, 3>& RayDirectionBodyLocal) noexcept
    {
        const BodyRaycastHit Hit = PickVehicleBodyPoint(BodyArchive, RayOriginBodyLocal, RayDirectionBodyLocal);
        if (!Hit.Hit)
            return false;
        PaintRipple.TriggerFinish(SwatchIndex, &Hit.BodyPoint);
        return true;
    }

    void OrbitBy(float DeltaYawDegrees, float DeltaPitchDegrees) noexcept
    {
        OrbitYawDegrees = std::fmod(OrbitYawDegrees + DeltaYawDegrees, 360.0f);
        if (OrbitYawDegrees < 0.0f)
            OrbitYawDegrees += 360.0f;
        OrbitPitchDegrees = std::clamp(OrbitPitchDegrees + DeltaPitchDegrees, 4.0f, 52.0f);
    }

    void Advance(float DeltaSeconds) noexcept
    {
        if (AutoOrbit && !OrbitDragging)
        {
            OrbitYawDegrees = std::fmod(OrbitYawDegrees + DefaultAutoOrbitDegreesPerSecond * std::max(0.0f, DeltaSeconds), 360.0f);
            if (OrbitYawDegrees < 0.0f)
                OrbitYawDegrees += 360.0f;
        }
        PaintRipple.Advance(DeltaSeconds);
        Inspection.Advance(DeltaSeconds);
    }
};

struct VehicleTablet
{
    enum class Content : uint8_t
    {
        Empty = 0,
        Launcher = 1,
        Garage = 2
    };

    bool Powered = true;
    Content Shown = Content::Empty;
    GarageConfiguratorState Garage{};

    [[nodiscard]] bool IsEmpty() const noexcept { return !Powered || Shown == Content::Empty; }
    [[nodiscard]] bool IsGarageActive() const noexcept { return Powered && Shown == Content::Garage; }

    void OpenLauncher() noexcept
    {
        Powered = true;
        Shown = Content::Launcher;
    }

    void OpenGarage() noexcept
    {
        Powered = true;
        Shown = Content::Garage;
    }

    void Advance(float DeltaSeconds) noexcept
    {
        if (IsGarageActive())
            Garage.Advance(DeltaSeconds);
    }
};

struct VehicleCabin
{
    SteeringWheel Wheel{};
    VehicleTablet Tablet{};

    void ApplyInputPacket(const InputPacket& Packet) noexcept { Wheel.ApplySteer(Packet.SteerAxis); }

    void Advance(float DeltaSeconds) noexcept { Tablet.Advance(DeltaSeconds); }
};

} // namespace Frontier
