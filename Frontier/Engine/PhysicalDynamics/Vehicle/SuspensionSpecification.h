/*==========================================================================================================================================
                                                        SUSPENSIONSPECIFICATION.H
==========================================================================================================================================*/
// 🧩 Two-mass progressive coilover strut, digressive bump/rebound damper, elastomer stops, and anti-roll bar specifications.

#pragma once

#include "Engine/PhysicalDynamics/Vehicle/VehicleMath.h"

namespace Frontier::Vehicle
{

struct StrutSpecification
{
    float RestLengthMetres       = 0.34f;      // [m] unloaded suspension strut travel length
    float MinLengthMetres        = 0.18f;      // [m] full-jounce bump-stop minimum length
    float MaxLengthMetres        = 0.46f;      // [m] full-droop rebound-stop maximum length

    float SpringRateNpm          = 42000.0f;   // [N/m] linear coil spring stiffness
    float CubicProgressiveRate   = 850000.0f;  // [N/m^3] progressive rising-rate cubic term
    float PreloadForceNewtons    = 850.0f;     // [N] static spring preload at RestLengthMetres

    float BumpDampingLowNsm      = 3600.0f;    // [N*s/m] low-speed compression damping
    float BumpDampingHighNsm     = 1950.0f;    // [N*s/m] high-speed digressive compression slope
    float ReboundDampingLowNsm   = 4900.0f;    // [N*s/m] low-speed rebound damping
    float ReboundDampingHighNsm  = 2600.0f;    // [N*s/m] high-speed digressive rebound slope
    float DigressiveKneeMps      = 0.16f;      // [m/s] blow-off valve velocity knee

    float BumpStopZoneMetres     = 0.035f;     // [m] elastomer cushion engagement before MinLengthMetres
    float BumpStopRateNpm        = 185000.0f;  // [N/m] elastomer bump-stop stiffness
    float DroopStopRateNpm       = 95000.0f;   // [N/m] rebound travel limiter stiffness
    float UnsprungMassKg         = 34.0f;      // [kg] wheel + hub + rotor + upright corner mass
    float StaticCamberRadians    = -0.026f;    // [rad] -1.5 deg static negative camber
    float CamberGainPerMetre     = -0.22f;     // [rad/m] double-wishbone negative camber gain under bump
};

struct AntiRollBarSpecification
{
    float FrontStiffnessNpm = 18500.0f;   // [N/m] front axle roll-bar differential rate
    float RearStiffnessNpm  = 14500.0f;   // [N/m] rear axle roll-bar differential rate
};

} // namespace Frontier::Vehicle
