#include "Frontier/Engine/Effects/PlumeSolver.h"
#include <algorithm>
#include <cmath>

namespace Frontier
{
const std::array<PlumeGrade, 4> PlumeGrades = {{
    {"Low",    {64, 64, 20},   0.40f, 8,  32},
    {"Medium", {96, 96, 24},   0.30f, 12, 40},
    {"High",   {128, 128, 32}, 0.25f, 16, 48},
    {"Ultra",  {160, 160, 48}, 0.20f, 20, 64},
}};
namespace {
constexpr float SpeedCeiling = 40.0f;   // PlumeSpeedCeiling (PlumeRecords.wgsl)
constexpr float SinkCoefficient = 0.12f; // Plume.Forces.y in EncodeRecord
float ClampF(float V, float Lo, float Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
int ClampI(int V, int N) { return V < 0 ? 0 : (V > N - 1 ? N - 1 : V); }
}

PlumeSolver::PlumeSolver(const PlumeGrade& Grade) { Resize(Grade); }

void PlumeSolver::Resize(const PlumeGrade& Grade)
{
    for (int A = 0; A < 3; ++A) { ThermoExtent[A] = Grade.Extent[A]; VelocityExtent[A] = Grade.Extent[A] / 2; }
    CellExtent = Grade.CellExtent; CoarseEdge = 2.0f * Grade.CellExtent;
    const size_t V = size_t(VelocityExtent[0]) * VelocityExtent[1] * VelocityExtent[2];
    const size_t T = size_t(ThermoExtent[0]) * ThermoExtent[1] * ThermoExtent[2];
    Vel_.assign(V * 4, 0.0f); VelNext_.assign(V * 4, 0.0f);
    Thermo_.assign(T * 4, 0.0f); ThermoNext_.assign(T * 4, 0.0f);
    Pressure_.assign(V, 0.0f); PressureNext_.assign(V, 0.0f); Divergence_.assign(V, 0.0f); Curl_.assign(V * 4, 0.0f);
    StepsTaken = 0;
}
void PlumeSolver::Clear() noexcept { std::fill(Vel_.begin(), Vel_.end(), 0.0f); std::fill(Thermo_.begin(), Thermo_.end(), 0.0f); std::fill(Pressure_.begin(), Pressure_.end(), 0.0f); StepsTaken = 0; }

float PlumeSolver::SampleVel(int C, float X, float Y, float Z) const noexcept
{
    const int x0 = ClampI(int(std::floor(X)), VelocityExtent[0]), y0 = ClampI(int(std::floor(Y)), VelocityExtent[1]), z0 = ClampI(int(std::floor(Z)), VelocityExtent[2]);
    const int x1 = ClampI(x0 + 1, VelocityExtent[0]), y1 = ClampI(y0 + 1, VelocityExtent[1]), z1 = ClampI(z0 + 1, VelocityExtent[2]);
    const float fx = ClampF(X - std::floor(X), 0, 1), fy = ClampF(Y - std::floor(Y), 0, 1), fz = ClampF(Z - std::floor(Z), 0, 1);
    auto At = [&](int x, int y, int z) { return Vel_[size_t(VIndex(x, y, z)) * 4 + C]; };
    const float a = At(x0,y0,z0)*(1-fx)+At(x1,y0,z0)*fx, b = At(x0,y1,z0)*(1-fx)+At(x1,y1,z0)*fx;
    const float c = At(x0,y0,z1)*(1-fx)+At(x1,y0,z1)*fx, d = At(x0,y1,z1)*(1-fx)+At(x1,y1,z1)*fx;
    return (a*(1-fy)+b*fy)*(1-fz) + (c*(1-fy)+d*fy)*fz;
}
float PlumeSolver::SampleThermo(int C, float X, float Y, float Z) const noexcept
{
    const int x0 = ClampI(int(std::floor(X)), ThermoExtent[0]), y0 = ClampI(int(std::floor(Y)), ThermoExtent[1]), z0 = ClampI(int(std::floor(Z)), ThermoExtent[2]);
    const int x1 = ClampI(x0 + 1, ThermoExtent[0]), y1 = ClampI(y0 + 1, ThermoExtent[1]), z1 = ClampI(z0 + 1, ThermoExtent[2]);
    const float fx = ClampF(X - std::floor(X), 0, 1), fy = ClampF(Y - std::floor(Y), 0, 1), fz = ClampF(Z - std::floor(Z), 0, 1);
    auto At = [&](int x, int y, int z) { return Thermo_[size_t(TIndex(x, y, z)) * 4 + C]; };
    const float a = At(x0,y0,z0)*(1-fx)+At(x1,y0,z0)*fx, b = At(x0,y1,z0)*(1-fx)+At(x1,y1,z0)*fx;
    const float c = At(x0,y0,z1)*(1-fx)+At(x1,y0,z1)*fx, d = At(x0,y1,z1)*(1-fx)+At(x1,y1,z1)*fx;
    return (a*(1-fy)+b*fy)*(1-fz) + (c*(1-fy)+d*fy)*fz;
}
float PlumeSolver::NeighbourPressure(int X, int Y, int Z, float Centre) const noexcept
{   // PlumePressure.wgsl ResolveNeighbourPressure: below floor → Neumann; outside → Dirichlet 0; solid → Neumann.
    if (Z < 0) return Centre;
    if (X < 0 || Y < 0 || Z >= VelocityExtent[2] || X >= VelocityExtent[0] || Y >= VelocityExtent[1]) return 0.0f;
    if (Vel_[size_t(VIndex(X, Y, Z)) * 4 + 3] > 0.5f) return Centre;
    return Pressure_[size_t(VIndex(X, Y, Z))];
}
void PlumeSolver::AddSource(const PlumeSource& S, float Dt, const PlumeSettings& Settings)
{   // Deposit into thermo cells within Radius (world → lattice: window centred on origin for the CPU mirror).
    const float Inv = 1.0f / CellExtent;
    const int R = std::max(1, int(std::ceil(S.Radius * Inv)));
    const int cx = int(std::floor(S.Position[0] * Inv)) + ThermoExtent[0] / 2, cy = int(std::floor(S.Position[1] * Inv)) + ThermoExtent[1] / 2, cz = int(std::floor(S.Position[2] * Inv));
    for (int z = cz - R; z <= cz + R; ++z) for (int y = cy - R; y <= cy + R; ++y) for (int x = cx - R; x <= cx + R; ++x)
    {
        if (x < 0 || y < 0 || z < 0 || x >= ThermoExtent[0] || y >= ThermoExtent[1] || z >= ThermoExtent[2]) continue;
        const float dx = (x - cx) * CellExtent, dy = (y - cy) * CellExtent, dz = (z - cz) * CellExtent;
        const float W = std::exp(-(dx*dx + dy*dy + dz*dz) / std::max(1e-6f, S.Radius * S.Radius));
        float* T = &Thermo_[size_t(TIndex(x, y, z)) * 4];
        T[0] += S.SmokeRate * Settings.PlumeDensity * Dt * W;   // density (soot yield folded via PlumeSoot below)
        T[1] += S.HeatRate * (25.0f * Settings.PlumeHeat) * Dt * W * 0.04f;
        T[2] += S.FlameRate * Settings.PlumeFire * Dt * W;
        T[3] += S.SootRate * (2.4f * Settings.PlumeSoot) * Dt * W;
    }
}

void PlumeSolver::Step(float Dt, const PlumeSettings& S, const std::vector<PlumeSource>& Sources, int Sweeps)
{
    if (!(Dt > 0.0f) || !S.PlumeEnabled) return;
    Dt = std::min(Dt, 1.0f / 30.0f); // StepCeiling (PlumeSpace.js)
    for (const auto& Src : Sources) AddSource(Src, Dt, S);
    // Retention factors — EncodeRecord (PlumeSpace.js) verbatim.
    const float Diss = std::min(0.95f, S.PlumeDissipation);
    const float RetSmoke = std::pow(1.0f - Diss, Dt), RetHeat = std::pow(1.0f - std::min(0.95f, S.PlumeCooling), Dt);
    const float RetFlame = std::exp(-S.PlumeFlameDecay * Dt), RetVel = std::pow(1.0f - 0.08f, Dt);
    const float WindA = S.PlumeWindDirection * 3.14159265f / 180.0f;
    const float Wind[3] = {std::cos(WindA) * S.PlumeWindSpeed, std::sin(WindA) * S.PlumeWindSpeed, 0.0f};
    const int VX = VelocityExtent[0], VY = VelocityExtent[1], VZ = VelocityExtent[2];
    // ① AdvectVelocity: semi-Lagrangian + viscous relaxation + saturated buoyancy Lift = B·g/(1+0.125·g).
    for (int z = 0; z < VZ; ++z) for (int y = 0; y < VY; ++y) for (int x = 0; x < VX; ++x)
    {
        const size_t I = size_t(VIndex(x, y, z)) * 4;
        if (Vel_[I + 3] > 0.5f) { for (int c = 0; c < 4; ++c) VelNext_[I + c] = Vel_[I + c]; continue; }
        const float wx = (x + 0.5f) * CoarseEdge, wy = (y + 0.5f) * CoarseEdge, wz = (z + 0.5f) * CoarseEdge;
        const float ox = SampleVel(0, x, y, z), oy = SampleVel(1, x, y, z), oz = SampleVel(2, x, y, z);
        const float dx = (wx - ox * Dt) / CoarseEdge - 0.5f, dy = (wy - oy * Dt) / CoarseEdge - 0.5f, dz = (wz - oz * Dt) / CoarseEdge - 0.5f;
        float vx = Wind[0] + (SampleVel(0, dx, dy, dz) - Wind[0]) * RetVel;
        float vy = Wind[1] + (SampleVel(1, dx, dy, dz) - Wind[1]) * RetVel;
        float vz = Wind[2] + (SampleVel(2, dx, dy, dz) - Wind[2]) * RetVel;
        const float tx = wx / CellExtent - 0.5f, ty = wy / CellExtent - 0.5f, tz = wz / CellExtent - 0.5f;
        const float heat = SampleThermo(1, tx, ty, tz), dens = SampleThermo(0, tx, ty, tz), soot = SampleThermo(3, tx, ty, tz);
        vz += (S.PlumeBuoyancy * heat / (1.0f + 0.125f * heat) - SinkCoefficient * (dens + soot)) * Dt;
        const float Sp = std::sqrt(vx*vx + vy*vy + vz*vz);
        if (Sp > SpeedCeiling) { const float K = SpeedCeiling / Sp; vx *= K; vy *= K; vz *= K; }
        VelNext_[I] = vx; VelNext_[I + 1] = vy; VelNext_[I + 2] = vz; VelNext_[I + 3] = 0.0f;
    }
    Vel_.swap(VelNext_);
    // ② Curl + confinement (PlumeVorticity.wgsl): F = ε·h·(∇|ω|/|∇|ω|| × ω)·dt.
    const float Scale = 0.5f / CoarseEdge;
    auto VelAt = [&](int x, int y, int z, int c) { return Vel_[size_t(VIndex(ClampI(x, VX), ClampI(y, VY), ClampI(z, VZ))) * 4 + c]; };
    for (int z = 0; z < VZ; ++z) for (int y = 0; y < VY; ++y) for (int x = 0; x < VX; ++x)
    {
        float* C = &Curl_[size_t(VIndex(x, y, z)) * 4];
        C[0] = ((VelAt(x,y+1,z,2)-VelAt(x,y-1,z,2)) - (VelAt(x,y,z+1,1)-VelAt(x,y,z-1,1))) * Scale;
        C[1] = ((VelAt(x,y,z+1,0)-VelAt(x,y,z-1,0)) - (VelAt(x+1,y,z,2)-VelAt(x-1,y,z,2))) * Scale;
        C[2] = ((VelAt(x+1,y,z,1)-VelAt(x-1,y,z,1)) - (VelAt(x,y+1,z,0)-VelAt(x,y-1,z,0))) * Scale;
        C[3] = std::sqrt(C[0]*C[0] + C[1]*C[1] + C[2]*C[2]);
    }
    if (S.PlumeVorticity > 0.0f)
        for (int z = 0; z < VZ; ++z) for (int y = 0; y < VY; ++y) for (int x = 0; x < VX; ++x)
        {
            const size_t I = size_t(VIndex(x, y, z)) * 4; if (Vel_[I + 3] > 0.5f) continue;
            auto CurlMag = [&](int xx, int yy, int zz) { return Curl_[size_t(VIndex(ClampI(xx,VX), ClampI(yy,VY), ClampI(zz,VZ))) * 4 + 3]; };
            const float ex = (CurlMag(x+1,y,z)-CurlMag(x-1,y,z))*Scale, ey = (CurlMag(x,y+1,z)-CurlMag(x,y-1,z))*Scale, ez = (CurlMag(x,y,z+1)-CurlMag(x,y,z-1))*Scale;
            const float Len = std::sqrt(ex*ex + ey*ey + ez*ez); if (Len <= 1e-5f) continue;
            const float* W = &Curl_[size_t(VIndex(x, y, z)) * 4]; const float K = S.PlumeVorticity * CoarseEdge * Dt / Len;
            Vel_[I] += K * (ey * W[2] - ez * W[1]); Vel_[I + 1] += K * (ez * W[0] - ex * W[2]); Vel_[I + 2] += K * (ex * W[1] - ey * W[0]);
        }
    // ③ Divergence: ∇·u · (0.5/h).
    for (int z = 0; z < VZ; ++z) for (int y = 0; y < VY; ++y) for (int x = 0; x < VX; ++x)
    {
        const size_t I = size_t(VIndex(x, y, z));
        if (Vel_[I * 4 + 3] > 0.5f) { Divergence_[I] = 0.0f; continue; }
        Divergence_[I] = ((VelAt(x+1,y,z,0)-VelAt(x-1,y,z,0)) + (VelAt(x,y+1,z,1)-VelAt(x,y-1,z,1)) + (VelAt(x,y,z+1,2)-VelAt(x,y,z-1,2))) * Scale;
    }
    // ④ Jacobi (warm-started): q' = (Σ neighbours − ∇·u·h²)/6 — the h² factor is in PlumePressure.wgsl; omitting it diverges.
    const float H2 = CoarseEdge * CoarseEdge;
    for (int Sweep = 0; Sweep < std::max(0, Sweeps); ++Sweep)
    {
        for (int z = 0; z < VZ; ++z) for (int y = 0; y < VY; ++y) for (int x = 0; x < VX; ++x)
        {
            const size_t I = size_t(VIndex(x, y, z)); const float Centre = Pressure_[I];
            if (Vel_[I * 4 + 3] > 0.5f) { PressureNext_[I] = Centre; continue; }
            const float Sum = NeighbourPressure(x+1,y,z,Centre)+NeighbourPressure(x-1,y,z,Centre)+NeighbourPressure(x,y+1,z,Centre)
                            + NeighbourPressure(x,y-1,z,Centre)+NeighbourPressure(x,y,z+1,Centre)+NeighbourPressure(x,y,z-1,Centre);
            PressureNext_[I] = (Sum - Divergence_[I] * H2) / 6.0f;
        }
        Pressure_.swap(PressureNext_);
    }
    // ⑤ Project: u −= ∇q·(0.5/h) — wait: WGSL gradient already includes Scale; velocity -= Gradient (gradient of q in world units).
    for (int z = 0; z < VZ; ++z) for (int y = 0; y < VY; ++y) for (int x = 0; x < VX; ++x)
    {
        const size_t I = size_t(VIndex(x, y, z)) * 4; if (Vel_[I + 3] > 0.5f) continue;
        const float Centre = Pressure_[size_t(VIndex(x, y, z))];
        float* V = &Vel_[I];
        V[0] -= (NeighbourPressure(x+1,y,z,Centre)-NeighbourPressure(x-1,y,z,Centre)) * Scale;
        V[1] -= (NeighbourPressure(x,y+1,z,Centre)-NeighbourPressure(x,y-1,z,Centre)) * Scale;
        V[2] -= (NeighbourPressure(x,y,z+1,Centre)-NeighbourPressure(x,y,z-1,Centre)) * Scale;
        const float Sp = std::sqrt(V[0]*V[0]+V[1]*V[1]+V[2]*V[2]);
        if (Sp > SpeedCeiling) { const float K = SpeedCeiling / Sp; V[0]*=K; V[1]*=K; V[2]*=K; }
    }
    // ⑥ AdvectThermo: semi-Lagrangian on the fine lattice (velocity upsampled ×2), retention per channel.
    const int TX = ThermoExtent[0], TY = ThermoExtent[1], TZ = ThermoExtent[2];
    for (int z = 0; z < TZ; ++z) for (int y = 0; y < TY; ++y) for (int x = 0; x < TX; ++x)
    {
        const float wx = (x + 0.5f) * CellExtent, wy = (y + 0.5f) * CellExtent, wz = (z + 0.5f) * CellExtent;
        const float vx = SampleVel(0, wx / CoarseEdge - 0.5f, wy / CoarseEdge - 0.5f, wz / CoarseEdge - 0.5f);
        const float vy = SampleVel(1, wx / CoarseEdge - 0.5f, wy / CoarseEdge - 0.5f, wz / CoarseEdge - 0.5f);
        const float vz = SampleVel(2, wx / CoarseEdge - 0.5f, wy / CoarseEdge - 0.5f, wz / CoarseEdge - 0.5f);
        const float dx = (wx - vx * Dt) / CellExtent - 0.5f, dy = (wy - vy * Dt) / CellExtent - 0.5f, dz = (wz - vz * Dt) / CellExtent - 0.5f;
        float* T = &ThermoNext_[size_t(TIndex(x, y, z)) * 4];
        T[0] = SampleThermo(0, dx, dy, dz) * RetSmoke; T[1] = SampleThermo(1, dx, dy, dz) * RetHeat;
        T[2] = SampleThermo(2, dx, dy, dz) * RetFlame; T[3] = SampleThermo(3, dx, dy, dz) * RetSmoke;
    }
    Thermo_.swap(ThermoNext_);
    ++StepsTaken;
}
float PlumeSolver::QueryDensity(int X, int Y, int Z) const noexcept { return Thermo_[size_t(TIndex(ClampI(X,ThermoExtent[0]),ClampI(Y,ThermoExtent[1]),ClampI(Z,ThermoExtent[2]))) * 4]; }
float PlumeSolver::QueryHeat(int X, int Y, int Z) const noexcept { return Thermo_[size_t(TIndex(ClampI(X,ThermoExtent[0]),ClampI(Y,ThermoExtent[1]),ClampI(Z,ThermoExtent[2]))) * 4 + 1]; }
float PlumeSolver::QueryMaximumDensity() const noexcept { float M = 0; for (size_t i = 0; i < Thermo_.size(); i += 4) M = std::max(M, Thermo_[i]); return M; }
float PlumeSolver::QueryMaximumSpeed() const noexcept { float M = 0; for (size_t i = 0; i < Vel_.size(); i += 4) M = std::max(M, std::sqrt(Vel_[i]*Vel_[i]+Vel_[i+1]*Vel_[i+1]+Vel_[i+2]*Vel_[i+2])); return M; }
float PlumeSolver::QueryDivergenceMagnitude() const noexcept
{
    const int VX = VelocityExtent[0], VY = VelocityExtent[1], VZ = VelocityExtent[2]; const float Scale = 0.5f / CoarseEdge; double Sum = 0;
    auto At = [&](int x, int y, int z, int c) { return Vel_[size_t(VIndex(ClampI(x,VX),ClampI(y,VY),ClampI(z,VZ))) * 4 + c]; };
    for (int z = 0; z < VZ; ++z) for (int y = 0; y < VY; ++y) for (int x = 0; x < VX; ++x)
    { const double D = ((At(x+1,y,z,0)-At(x-1,y,z,0)) + (At(x,y+1,z,1)-At(x,y-1,z,1)) + (At(x,y,z+1,2)-At(x,y,z-1,2))) * Scale; Sum += D * D; }
    return VX*VY*VZ ? float(std::sqrt(Sum / (double(VX)*VY*VZ))) : 0.0f;
}
bool PlumeSolver::IsFinite() const noexcept { for (float V : Vel_) if (!std::isfinite(V)) return false; for (float V : Thermo_) if (!std::isfinite(V)) return false; return true; }
} // namespace Frontier
