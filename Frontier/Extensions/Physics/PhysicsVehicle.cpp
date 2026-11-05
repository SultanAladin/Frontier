/*==========================================================================================================================================
                                                            PHYSICSVEHICLE.CPP
==========================================================================================================================================*/
// 🧩 The chassis body and its hull, the solver's hooks (chassis, forces, torque, ground rays, parallel carcasses), the step, the
//    reset and the view the editor draws.

#include "Frontier/Extensions/Physics/PhysicsVehicle.h"
#include "Frontier/Extensions/Physics/VehicleDeployment.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

using Vehicle::Quat;
using Vehicle::Vec3;

std::array<double, 3> ToArray(const Vec3& Value)
{
    return { Value.x, Value.y, Value.z };
}

Vec3 ToVec(const std::array<double, 3>& Value)
{
    return Vec3{ static_cast<float>(Value[0]), static_cast<float>(Value[1]), static_cast<float>(Value[2]) };
}

// 📝 Column-major world-from-local for a position and a rotation.
std::array<float, 16> Compose(const Vec3& Position, const Quat& Rotation)
{
    const Vec3 X = Rotation.Rotate({ 1.0f, 0.0f, 0.0f }), Y = Rotation.Rotate({ 0.0f, 1.0f, 0.0f }), Z = Rotation.Rotate({ 0.0f, 0.0f, 1.0f });
    return { X.x, X.y, X.z, 0.0f, Y.x, Y.y, Y.z, 0.0f, Z.x, Z.y, Z.z, 0.0f, Position.x, Position.y, Position.z, 1.0f };
}

} // namespace

PhysicsVehicle::PhysicsVehicle()  = default;
PhysicsVehicle::~PhysicsVehicle() = default;

bool PhysicsVehicle::Construct(PhysicsSpace& InSpace, RecordToken InRecord, const Vehicle::VehicleSpecification& Specification,
                               const Vehicle::VehicleSolverConfiguration& InConfiguration, const std::array<double, 3>& StartMetres,
                               double StartYawDegrees, std::string* Error)
{
    return Construct(InSpace, InRecord, Specification, InConfiguration, StartMetres, StartYawDegrees, SpawnPolicy::GridRelease(), Error);
}

bool PhysicsVehicle::Construct(PhysicsSpace& InSpace, RecordToken InRecord, const Vehicle::VehicleSpecification& Specification,
                               const Vehicle::VehicleSolverConfiguration& InConfiguration, const std::array<double, 3>& StartMetres,
                               double StartYawDegrees, const SpawnPolicy& DeploymentRules, std::string* Error)
{
    Space         = &InSpace;
    Record        = InRecord;
    Configuration = InConfiguration;
    const Vehicle::VehicleGeometry& Geometry = Specification.Geometry;
    OriginBelowCentre = Geometry.CoMAuthoredZ();

    // ① the vehicle's own deployment function stands it: the whole footprint is surveyed, so a banked grid, a kerb, a ramp and a flat
    //    studio pad all resolve through the same stand; brakes are handed over exactly as the session's SpawnPolicy asks
    Deployment = SurveyVehicleDeployment(*Space, Geometry, DeploymentRules, StartMetres, StartYawDegrees);
    FitDrivenWheels(Configuration, DeploymentRules.DrivesWheels);
    Deployed   = Deployment.Stand;
    ParkedBrakes           = Deployment.ParkedBrakes;
    SpawnRules             = DeploymentRules;
    HandbrakeLatched       = Deployment.ParkedBrakes.HandbrakeEngaged;
    ParkedPedalShare       = Deployment.ParkedBrakes.PedalShare;
    SecondsSinceDeployment = 0.0f;

    const double Ground = Deployment.Stand.PositionMetres[2];

    // ② the chassis hull about the centre of mass: the ControlVehicle's wedge (floor 0.15 m clear of the road so kerbs pass under
    //    it, bonnet, cabin and tail), heights from the ground at rest
    struct Corner { float X, Y, Height; };
    const Corner Wedge[] = { { 2.95f, 1.0f, 0.15f },  { 2.95f, -1.0f, 0.15f },  { -2.95f, 1.0f, 0.15f },  { -2.95f, -1.0f, 0.15f },
                             { 2.95f, 0.95f, 0.55f }, { 2.95f, -0.95f, 0.55f }, { 1.0f, 1.0f, 0.85f },    { 1.0f, -1.0f, 0.85f },
                             { 0.6f, 0.75f, 1.35f },  { 0.6f, -0.75f, 1.35f },  { -1.8f, 0.75f, 1.35f },  { -1.8f, -0.75f, 1.35f },
                             { -2.95f, 0.95f, 0.95f }, { -2.95f, -0.95f, 0.95f } };
    PhysicsBodyDescriptor Chassis;
    Chassis.Record = Record.Value;
    Chassis.Shape  = PhysicsShape::Hull;
    for (const Corner& Point : Wedge) Chassis.Points.push_back({ Point.X, Point.Y, Point.Height - Geometry.CoMHeight });
    // 📝 The stand the survey resolved: the chassis centre above the surface along its normal, on the surface's own pitch and roll, so a
    //    car on a hill is authored on a hill rather than level inside it.
    const VehicleStandTransform Stand = ResolveStandTransform(Deployment.Stand, Geometry, Geometry.CoMHeight + 0.01);
    const double Yaw = StartYawDegrees * 3.14159265358979323846 / 180.0;
    Chassis.PositionMetres = Stand.PositionMetres;
    Chassis.Orientation    = Stand.Orientation;
    Chassis.Motion         = PhysicsMotion::Moving;
    Chassis.MassKilograms  = Configuration.ChassisMass;
    // 📝 The solver predicts the chassis from ChassisMass, InvChassisInertia and gravity alone, so the body carries exactly those
    //    and no damping of its own.
    Chassis.InertiaKgm2    = { 1.0 / std::max(1.0e-9f, Configuration.InvChassisInertia.x), 1.0 / std::max(1.0e-9f, Configuration.InvChassisInertia.y),
                               1.0 / std::max(1.0e-9f, Configuration.InvChassisInertia.z) };
    Chassis.Friction       = 0.6;
    Chassis.Restitution    = 0.1;
    Chassis.LinearDamping  = 0.0;
    Chassis.AngularDamping = 0.0;
    Chassis.Sleeps         = false;
    Chassis.Swept          = true;
    Body = Space->AddBody(Chassis);
    if (Body == PhysicsNoBody)
    {
        if (Error) *Error = "the physics space refused the chassis body";
        return false;
    }
    LastMass = Configuration.ChassisMass;

    // ③ the solver, starting at the chassis' pose at rest
    Start.Position        = ToVec(Chassis.PositionMetres);
    Start.Orientation     = Quat{ static_cast<float>(Chassis.Orientation[0]), static_cast<float>(Chassis.Orientation[1]),
                                  static_cast<float>(Chassis.Orientation[2]), static_cast<float>(Chassis.Orientation[3]) };
    Start.LinearVelocity  = Vec3{};
    Start.AngularVelocity = Vec3{};
    BuildSolver(Start);
    (void)Yaw;
    return Solver && Solver->Constructed();
}

void PhysicsVehicle::BuildSolver(const Vehicle::ChassisKinematics& At)
{
    Solver = std::make_unique<Vehicle::VehicleSolver>();
    Vehicle::VehicleSolver::Hooks Hooks;
    Hooks.ReadChassis       = [this] { return ReadChassis(); };
    Hooks.ApplyForceAtPoint = [this](const Vec3& Force, const Vec3& Point) { Space->ApplyForce(Body, ToArray(Force), ToArray(Point)); };
    Hooks.ApplyTorque       = [this](const Vec3& Torque) { Space->ApplyTorque(Body, ToArray(Torque)); };
    Hooks.Ground            = [this](const Vec3& Point, Vec3& Surface, Vec3& Normal) { return QueryGround(Point, Surface, Normal); };
    Hooks.Dispatch          = [this](uint32_t Count, const std::function<void(uint32_t)>& Task) { Space->Dispatch(Count, Task); };
    Solver->Build(Configuration, Hooks, At);
}

Vehicle::ChassisKinematics PhysicsVehicle::ReadChassis() const
{
    const PhysicsBodyReading Reading = Space->ReadBody(Body);
    Vehicle::ChassisKinematics Chassis;
    Chassis.Position        = ToVec(Reading.PositionMetres);
    Chassis.Orientation     = Quat{ static_cast<float>(Reading.Orientation[0]), static_cast<float>(Reading.Orientation[1]),
                                    static_cast<float>(Reading.Orientation[2]), static_cast<float>(Reading.Orientation[3]) };
    Chassis.LinearVelocity  = ToVec(Reading.LinearVelocityMps);
    Chassis.AngularVelocity = ToVec(Reading.AngularVelocityRads);
    return Chassis;
}

// 📝 The road at a point: the column above and below it (1 m each way), the chassis itself passed through. Other bodies count
//    (a tyre rides over a fallen cone or a ramp).
bool PhysicsVehicle::QueryGround(const Vec3& Point, Vec3& Surface, Vec3& Normal) const
{
    PhysicsRayHit Hit;
    if (!Space->CastRay(PhysicsRay{ { Point.x, Point.y, Point.z + 1.0 }, { 0.0, 0.0, -1.0 }, 2.0, Body }, Hit)) return false;
    Surface = ToVec(Hit.PointMetres);
    Normal  = ToVec(Hit.Normal);
    return true;
}

void PhysicsVehicle::Step(double Seconds)
{
    if (!Solver) return;

    // 📝 The spawn brake rules outrank the pedals for the opening BrakeHoldSeconds of the deployment: brakes can be handed over disabled
    //    and released on a ramp, or parked on a turntable. After that the driver's own demand blends back in, never in one step.
    float EffectiveBrake     = std::clamp(Controls.Brake, 0.0f, 1.0f);
    float EffectiveHandbrake = Controls.Handbrake ? 1.0f : 0.0f;
    SpawnRules.ResolvePedalDemand(SecondsSinceDeployment, EffectiveBrake, EffectiveHandbrake, EffectiveBrake, EffectiveHandbrake);

    // 📝 A parked vehicle keeps its handbrake latched until its driver actually asks for drive.
    if (HandbrakeLatched && Controls.Throttle > 0.05f)
    {
        HandbrakeLatched = false;
    }
    if (HandbrakeLatched)
    {
        EffectiveHandbrake = 1.0f;
    }

    Vehicle::DriverInput Input;
    Input.Throttle  = std::clamp(Controls.Throttle, 0.0f, 1.0f);
    Input.Brake     = EffectiveBrake;
    Input.Steer     = std::clamp(Controls.Steer, -1.0f, 1.0f);
    Input.Handbrake = EffectiveHandbrake >= 0.5f;
    SecondsSinceDeployment += static_cast<float>(std::abs(Seconds));
    Solver->AssignInput(Input);
    Solver->Step(static_cast<float>(Seconds));
    // 📝 Fuel leaves the chassis: the body follows the solver's sprung mass (inertia at the same radii of gyration).
    const float Mass = Solver->Telemetry().ChassisMass;
    if (Mass > 1.0f && std::fabs(Mass - LastMass) > 0.25f)
    {
        const double Scale = static_cast<double>(Mass) / static_cast<double>(Configuration.ChassisMass);
        Space->AssignMass(Body, Mass, { Scale / Configuration.InvChassisInertia.x, Scale / Configuration.InvChassisInertia.y,
                                        Scale / Configuration.InvChassisInertia.z });
        LastMass = Mass;
    }
}

void PhysicsVehicle::Reset()
{
    PhysicsBodyReading Pose;
    Pose.PositionMetres      = ToArray(Start.Position);
    Pose.Orientation         = { Start.Orientation.x, Start.Orientation.y, Start.Orientation.z, Start.Orientation.w };
    Pose.LinearVelocityMps   = { 0.0, 0.0, 0.0 };
    Pose.AngularVelocityRads = { 0.0, 0.0, 0.0 };
    Space->PlaceBody(Body, Pose);
    if (LastMass != Configuration.ChassisMass)
    {
        Space->AssignMass(Body, Configuration.ChassisMass, { 1.0 / Configuration.InvChassisInertia.x, 1.0 / Configuration.InvChassisInertia.y,
                                                             1.0 / Configuration.InvChassisInertia.z });
        LastMass = Configuration.ChassisMass;
    }
    // 📝 A reset is a fresh deployment: the spawn brake rules and the parked handbrake apply again from zero.
    SecondsSinceDeployment = 0.0f;
    HandbrakeLatched       = ParkedBrakes.HandbrakeEngaged;
    BuildSolver(Start);
}

void PhysicsVehicle::ReadView(PhysicsVehicleView& View) const
{
    View.Record = Record;
    View.Input  = Controls;
    if (!Solver) return;
    View.Telemetry = Solver->Telemetry();
    const Vehicle::ChassisKinematics Chassis = ReadChassis();
    // 📝 The authored model's origin sits OriginBelowCentre under the centre of mass in the body's own axes.
    const Vec3 Origin = Chassis.Position - Chassis.Orientation.Rotate({ 0.0f, 0.0f, static_cast<float>(OriginBelowCentre) });
    View.Body = Compose(Origin, Chassis.Orientation);
    const float Cold = Configuration.Tyre.InflationPressure;
    for (uint32_t Wheel = 0; Wheel < 4; ++Wheel)
    {
        View.FieldValid[Wheel] = false;
        if (Wheel >= View.Telemetry.WheelCount) continue;
        const Vehicle::WheelTelemetry& Reading = View.Telemetry.Wheels[Wheel];
        const Quat Material = (Reading.HubRotation * Quat::AxisAngle(Vec3{ 0.0f, 1.0f, 0.0f }, Reading.SpinAngle)).Normalized();
        View.Wheels[Wheel]  = Compose(Reading.HubPosition, Material);
        View.FieldValid[Wheel] = Solver->SampleCarcassField(Wheel, View.Fields[Wheel]);
        // 📝 The analytic profile's inputs (VehicleVisual.slang): the road's direction in the material axes, deflection, pressure, shear.
        const Vec3 Down = Material.Conjugate().Rotate(Vec3{ -Reading.ContactNormal.x, -Reading.ContactNormal.y, -Reading.ContactNormal.z });
        View.ContactAngle[Wheel]  = std::atan2(Down.z, Down.x);
        View.Deflection[Wheel]    = Reading.InContact ? std::max(0.0f, Reading.TyreDeflection) : 0.0f;
        View.GaugePressure[Wheel] = Reading.Thermal.Valid && Reading.Thermal.GaugePressure > 0.0 ? static_cast<float>(Reading.Thermal.GaugePressure) : Cold;
        View.LateralShift[Wheel]  = std::clamp(Reading.LateralForce / 145000.0f, -0.042f, 0.042f);
    }
}

} // namespace Frontier
