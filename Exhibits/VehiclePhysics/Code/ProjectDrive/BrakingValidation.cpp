/*==========================================================================================================================================
                                                           BRAKINGVALIDATION.CPP
==========================================================================================================================================*/
// 🧩 unit tests for the disk-brake hydraulic/thermal law + ABS modulator.
//   Build: g++ -std=c++17 -O2 BrakingValidation.cpp -o brakes && ./brakes
//============================================================================================================================================

#include "BrakeHydraulics.h"

#include <cmath>
#include <cstdio>

using namespace Frontier::Vehicle;

static int g_succeeded = 0, g_fail = 0;
static void Assess(const char* name, bool ok, double got, double ref)
{
    std::printf("  [%s] %-58s got %10.3f  (ref %10.3f)\n", ok ? "PASS" : "FAIL", name, got, ref);
    if (ok) ++g_succeeded; else ++g_fail;
}

int main()
{
    std::printf("\n=== Disk-brake hydraulics + thermal fade + ABS ===\n\n");
    const float dt = 1.0f / 240.0f;

    // ---- 1. Static torque formula (T = 2·μ·P·A·R_eff) ----
    {
        BrakingParameters p = BrakingParameters::DefaultGT3();
        const float A = p.PistonArea();
        const float R = p.EffectiveRadius();
        const float expect = 2.0f * p.FrictionCoeffCold * (p.MaxBrakePressure * A) * R;
        const float got = p.BrakeTorque(p.MaxBrakePressure, p.AmbientTemp);
        Assess("brake torque at full pressure matches 2*mu*P*A*Reff", std::fabs(got - expect) < 1.0f, got, expect);
        Assess("piston area = pi*r^2*N (44 mm x4)", std::fabs(A - 3.14159265f*0.022f*0.022f*4.0f) < 1e-6f, A, 0.006082);
        Assess("effective radius = mean of disk radii", std::fabs(R - 0.125f) < 1e-4f, R, 0.125);
        Assess("full-pressure torque is substantial (>4 kN·m)", got > 4000.0f, got, 4000.0);
    }

    // ---- 2. Thermal FADE: friction coefficient drops with temperature ----
    {
        BrakingParameters p = BrakingParameters::DefaultGT3();
        const float muCold = p.FrictionCoefficient(300.0f);           // ambient-ish
        const float muMid  = p.FrictionCoefficient(0.5f*(p.FadeStartTemp + p.FadeEndTemp));
        const float muHot  = p.FrictionCoefficient(1000.0f);          // beyond fade end
        Assess("cold friction = FrictionCoeffCold", std::fabs(muCold - p.FrictionCoeffCold) < 1e-4f, muCold, p.FrictionCoeffCold);
        Assess("hot friction saturates at FrictionCoeffHot", std::fabs(muHot - p.FrictionCoeffHot) < 1e-4f, muHot, p.FrictionCoeffHot);
        Assess("mid-fade friction is between hot and cold", muMid < muCold && muMid > muHot, muMid, 0.40);
    }

    // ---- 3. Heavy sustained braking heats the disk and FADES the torque ----
    {
        BrakeHydraulics bs;
        bs.Configure(1, BrakingParameters::DefaultGT3(), AbsParameters{ /*Enabled*/false });
        BrakeWheelInput in;
        in.PedalCommand = 1.0f; in.WheelOmega = 200.0f; in.SlipRatio = 0.0f; in.Airspeed = 0.0f; in.Braked = true;
        float tqPeak = 0.0f;   // peak (cold, full-pressure) torque before fade assigns in
        for (int s = 0; s < 480; ++s) { const auto o = bs.Step(0, in, dt); tqPeak = std::max(tqPeak, o.BrakeTorque_Nm); }
        const auto endReading = bs.Reading(0);
        const float tqEnd = endReading.Torque_Nm;
        Assess("disk heats past the fade threshold (>573 K)", endReading.Temperature_K > 573.0f, endReading.Temperature_K, 700.0);
        Assess("torque fades as the disk heats (T_end < cold peak)", tqEnd < tqPeak - 1.0f, tqEnd, tqPeak);
    }

    // ---- 4. Cooling: a hot disk with no braking and airflow cools toward ambient ----
    {
        BrakeHydraulics bs;
        bs.Configure(1, BrakingParameters::DefaultGT3(), AbsParameters{false});
        // heat it first
        BrakeWheelInput hot; hot.PedalCommand = 1.0f; hot.WheelOmega = 200.0f; hot.Airspeed = 0.0f; hot.Braked = true;
        for (int s = 0; s < 480; ++s) bs.Step(0, hot, dt);
        const float T0 = bs.Reading(0).Temperature_K;
        // Car stopped, no brake, cooling air over the disk (isolates convective cooling from any residual drag).
        BrakeWheelInput cool; cool.PedalCommand = 0.0f; cool.WheelOmega = 0.0f; cool.Airspeed = 30.0f; cool.Braked = false;
        for (int s = 0; s < 240*10; ++s) bs.Step(0, cool, dt);
        const float T1 = bs.Reading(0).Temperature_K;
        Assess("hot disk cools with airflow (T drops)", T1 < T0 - 5.0f, T1, T0);
        Assess("cooling never undershoots ambient", T1 >= BrakingParameters::DefaultGT3().AmbientTemp - 0.01f, T1, 293.15);
    }

    // ---- 5. Hydraulic line pressure rises at a finite rate toward the command ----
    {
        BrakeHydraulics bs;
        bs.Configure(1, BrakingParameters::DefaultGT3(), AbsParameters{false});
        BrakeWheelInput in; in.PedalCommand = 1.0f; in.WheelOmega = 100.0f; in.Braked = true;
        const auto o1 = bs.Step(0, in, dt);               // one step only
        const float pMax = BrakingParameters::DefaultGT3().MaxBrakePressure;
        Assess("pressure does not jump to max in one step (finite rise rate)", o1.Pressure_Pa < pMax, o1.Pressure_Pa, pMax);
        for (int s = 0; s < 240; ++s) bs.Step(0, in, dt);  // settle
        Assess("pressure reaches the commanded max after settling", bs.Reading(0).Pressure_Pa > 0.99f*pMax, bs.Reading(0).Pressure_Pa, pMax);
    }

    // ---- 6. ABS: modulates (dumps) pressure when slip exceeds the release threshold ----
    {
        BrakeHydraulics bs;
        AbsParameters abs; abs.Enabled = true;
        bs.Configure(1, BrakingParameters::DefaultGT3(), abs);
        // Wheel is locking hard: large negative slip, car moving fast.
        BrakeWheelInput lock; lock.PedalCommand = 1.0f; lock.WheelOmega = 5.0f; lock.SlipRatio = -0.6f; lock.Airspeed = 40.0f; lock.Braked = true;
        bool sawDump = false; float pDump = 1e30f;
        for (int s = 0; s < 240; ++s) { const auto o = bs.Step(0, lock, dt); if (o.AbsActive) { sawDump = true; pDump = std::min(pDump, o.Pressure_Pa); } }
        Assess("ABS engages (dumps pressure) on a locking wheel", sawDump, sawDump ? 1.0 : 0.0, 1.0);

        // With ABS off, the same locking wheel holds full pressure.
        BrakeHydraulics bs2; bs2.Configure(1, BrakingParameters::DefaultGT3(), AbsParameters{false});
        for (int s = 0; s < 240; ++s) bs2.Step(0, lock, dt);
        const float pLockedOff = bs2.Reading(0).Pressure_Pa;
        Assess("ABS reduces line pressure vs a locked (ABS-off) wheel", pDump < pLockedOff, pDump, pLockedOff);
    }

    // ---- 7. ABS re-applies when the wheel recovers grip (hysteresis) ----
    {
        BrakeHydraulics bs;
        AbsParameters abs; abs.Enabled = true;
        bs.Configure(1, BrakingParameters::DefaultGT3(), abs);
        // First drive it into a dump...
        BrakeWheelInput lock; lock.PedalCommand = 1.0f; lock.WheelOmega = 5.0f; lock.SlipRatio = -0.6f; lock.Airspeed = 40.0f; lock.Braked = true;
        for (int s = 0; s < 60; ++s) bs.Step(0, lock, dt);
        // ...then the wheel recovers grip (low slip): ABS should re-apply.
        BrakeWheelInput grip = lock; grip.SlipRatio = -0.02f;
        bool reapplied = false;
        for (int s = 0; s < 120; ++s) { const auto o = bs.Step(0, grip, dt); if (!o.AbsActive) reapplied = true; }
        Assess("ABS re-applies pressure once grip returns", reapplied, reapplied ? 1.0 : 0.0, 1.0);
    }

    std::printf("\n=== %d succeeded, %d failed ===\n\n", g_succeeded, g_fail);
    return g_fail == 0 ? 0 : 1;
}
