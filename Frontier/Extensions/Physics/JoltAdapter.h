/*==========================================================================================================================================
                                                               JOLTADAPTER.H
==========================================================================================================================================*/
// 🧩 The PhysicsAdapter on Jolt: one Jolt solver with the extension's layers, every body shape the adapter offers, forces at points,
//    placement and the nearest-hit ray query. Only the Physics extension's sources include this header.

#pragma once

#include "Frontier/Extensions/Physics/JoltExtension.h"
#include "Frontier/Extensions/Physics/PhysicsAdapter.h"

#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <atomic>
#include <vector>

namespace Frontier
{

// 📝 Counts the contacts Jolt adds (callbacks arrive on the job threads).
class JoltContactTally final : public JPH::ContactListener
{
  public:
    void OnContactAdded(const JPH::Body&, const JPH::Body&, const JPH::ContactManifold&, JPH::ContactSettings&) override
    {
        Added.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<uint64_t> Added{ 0 };
};

class JoltAdapter final : public PhysicsAdapter
{
  public:
    explicit JoltAdapter(const PhysicsSpaceSettings& Settings);
    ~JoltAdapter() override;

    JoltAdapter(const JoltAdapter&)            = delete;
    JoltAdapter& operator=(const JoltAdapter&) = delete;

    uint32_t AddBody(const PhysicsBodyDescriptor& Descriptor) override;
    void     Prepare() override;
    void     Step(double Seconds, uint32_t CollisionSteps) override;

    [[nodiscard]] PhysicsBodyReading ReadBody(uint32_t Body) const override;
    void PlaceBody(uint32_t Body, const PhysicsBodyReading& Pose) override;
    void ApplyForce(uint32_t Body, const std::array<double, 3>& Force, const std::array<double, 3>& Point) override;
    void ApplyTorque(uint32_t Body, const std::array<double, 3>& Torque) override;
    [[nodiscard]] bool CastRay(const PhysicsRay& Ray, PhysicsRayHit& Hit) const override;
    void AssignMass(uint32_t Body, double MassKilograms, const std::array<double, 3>& InertiaKgm2) override;
    void Dispatch(uint32_t Count, const std::function<void(uint32_t)>& Task) override;

    [[nodiscard]] size_t      QueryAwakeCount() const override;
    [[nodiscard]] uint64_t    QueryContactsAdded() const noexcept override { return Contacts.Added.load(std::memory_order_relaxed); }
    [[nodiscard]] uint32_t    QueryWorkers() const noexcept override { return Backend.QueryWorkers(); }
    [[nodiscard]] const char* QuerySolverName() const noexcept override { return "Jolt"; }

  private:
    [[nodiscard]] JPH::RefConst<JPH::Shape> ConstructShape(const PhysicsBodyDescriptor& Descriptor) const;

    JoltExtension            Backend;
    JPH::PhysicsSystem       Solver;
    JoltContactTally         Contacts;
    std::vector<JPH::BodyID> Bodies;        // [-] by body index
    std::vector<uint32_t>    BodyIndices;   // [-] Jolt body index → body index (PhysicsNoBody: not ours)
};

} // namespace Frontier
