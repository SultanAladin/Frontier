//============================================================================================================================================
//                                                             LOBBYSESSION.JS
//============================================================================================================================================
// 🧩 Live 5-driver studio Lobby session — simulates online players entering & leaving the four surrounding turntables
//    around the local hero car (NOVA-7), updating 3D rank plates, vehicle finishes, GDF cascades, surface cards, and BVH TLAS.

import { LobbyDrivers, LobbyGuestPool } from "./LobbyDrivers.js";
import { LinearColour } from "./VehicleAppearance.js";
import { EncodeMaterialRecords, MaterialRecordBytes } from "./MaterialSpecification.js";
import { VehicleGeometry, ResolveSuspensionMountLocal, ControlVehicleSockets } from "./VehicleSpecification.js";
import { ConstructRigidTransform, ConstructAxisQuaternion, MultiplyQuaternion, TransformPoint } from "./TransformProjection.js";
import { EaseCinematic } from "./LobbyCameraMotion.js";

const StaticStrutLength = ControlVehicleSockets.SuspensionMount[0][2] - ControlVehicleSockets.AxleMount[0][2];
const WheelRadius       = 0.50855;
const ApproachDistance  = 6.6;   // [m] distance behind the turntable where arriving cars roll in from
const TransitionSeconds = 1.45;  // [s] roll-in / roll-out duration

const BayLabels = Object.freeze([
    "Bay 1 · North-East",
    "Bay 2 · North-West",
    "Bay 3 · South-West",
    "Bay 4 · South-East"
]);

export class LobbySession
{
    constructor(Host)
    {
        this.Host = Host;
        this.Settings = {
            AutoSimulate: true,
            IntervalSeconds: 6.5
        };
        this.Timer      = 3.4;   // first player transition happens ~3.4 s after entering the studio
        this.PoolCursor = 4;     // indices 0..3 are initially seated; 4..7 wait in matchmaking queue
        this.History    = [];
        this.Bays       = [];

        const Scene = Host && Host.Scene;
        if (!Scene || !Scene.GuestBays || Scene.GuestBays.length === 0)
            return;

        // Share VolumeIndex across all pool plate sets (identical 0.56×0.15×0.016 plate box)
        const ReferencePlateSet = Scene.GuestBays[0].BasePlateSet;
        const SharedVolumeIndex = Scene.FacetSets[ReferencePlateSet]?.VolumeIndex ?? 0;
        for (const [, SetIdx] of Scene.GuestPlateSets.entries())
        {
            if (Scene.FacetSets[SetIdx] && Scene.FacetSets[SetIdx].VolumeIndex === undefined)
                Scene.FacetSets[SetIdx].VolumeIndex = SharedVolumeIndex;
        }

        Scene.GuestBays.forEach((Bay, Slot) =>
        {
            const InitialDriver = LobbyGuestPool[Slot] || LobbyDrivers[Slot + 1];
            this.Bays.push({
                Slot,
                Label:       BayLabels[Slot] || `Bay ${Slot + 1}`,
                Bay,
                Occupied:    true,
                Phase:       "parked",   // "parked" | "entering" | "leaving" | "empty"
                Progress:    1.0,
                SpinAngle:   0.0,
                Driver:      InitialDriver
            });
        });
    }

    QueryActive()
    {
        return this.Bays.length > 0;
    }

    QueryOccupiedCount()
    {
        // Local hero NOVA-7 (1) + occupied/transitioning guest bays
        return 1 + this.Bays.filter((SlotEntry) => SlotEntry.Phase === "parked" || SlotEntry.Phase === "entering").length;
    }

    QuerySummaryLines()
    {
        if (!this.QueryActive())
            return ["Lobby session inactive on Proving Grounds — switch to Lobby · Studio."];
        const Lines = [`Herobay 0 · ${LobbyDrivers[0].NameTag} (${LobbyDrivers[0].Rank}) · LOCAL HOST`];
        for (const SlotEntry of this.Bays)
        {
            if (SlotEntry.Phase === "empty")
                Lines.push(`${SlotEntry.Label} · [open turntable]`);
            else
                Lines.push(`${SlotEntry.Label} · ${SlotEntry.Driver.NameTag} (${SlotEntry.Driver.Rank}) · ${SlotEntry.Phase}`);
        }
        return Lines;
    }

    PickNextPoolDriver()
    {
        const ActiveTags = new Set([LobbyDrivers[0].NameTag]);
        for (const SlotEntry of this.Bays)
            if (SlotEntry.Phase !== "empty" && SlotEntry.Driver)
                ActiveTags.add(SlotEntry.Driver.NameTag);

        for (let Attempt = 0; Attempt < LobbyGuestPool.length; ++Attempt)
        {
            const Candidate = LobbyGuestPool[(this.PoolCursor + Attempt) % LobbyGuestPool.length];
            if (!ActiveTags.has(Candidate.NameTag))
            {
                this.PoolCursor = (this.PoolCursor + Attempt + 1) % LobbyGuestPool.length;
                return Candidate;
            }
        }
        const Fallback = LobbyGuestPool[this.PoolCursor % LobbyGuestPool.length];
        this.PoolCursor = (this.PoolCursor + 1) % LobbyGuestPool.length;
        return Fallback;
    }

    TriggerJoin(TargetSlot = -1)
    {
        if (!this.QueryActive())
            return false;
        const EmptySlots = this.Bays.filter((Entry) => Entry.Phase === "empty");
        const Chosen = TargetSlot >= 0 ? this.Bays[TargetSlot] : EmptySlots[0];
        if (!Chosen || Chosen.Phase !== "empty")
            return false;

        const Incoming = this.PickNextPoolDriver();
        Chosen.Driver    = Incoming;
        Chosen.Occupied  = true;
        Chosen.Phase     = "entering";
        Chosen.Progress  = 0.0;
        Chosen.SpinAngle = 0.0;

        this.ApplyDriverAppearance(Chosen);
        this.AssignBayVisibility(Chosen, true);
        this.PositionBayCar(Chosen, -ApproachDistance);
        this.SpawnBayParticles(Chosen, Incoming.Colour);
        this.RecordEvent(`${Incoming.NameTag} (${Incoming.Rank}) joined ${Chosen.Label}`);
        return true;
    }

    TriggerLeave(TargetSlot = -1)
    {
        if (!this.QueryActive())
            return false;
        const ParkedSlots = this.Bays.filter((Entry) => Entry.Phase === "parked");
        const Chosen = TargetSlot >= 0 ? this.Bays[TargetSlot]
                     : ParkedSlots[Math.floor(Math.random() * ParkedSlots.length)];
        if (!Chosen || Chosen.Phase !== "parked")
            return false;

        Chosen.Phase    = "leaving";
        Chosen.Progress = 0.0;
        this.SpawnBayParticles(Chosen, Chosen.Driver?.Colour || "#63a8ff");
        this.RecordEvent(`${Chosen.Driver.NameTag} (${Chosen.Driver.Rank}) leaving ${Chosen.Label}`);
        return true;
    }

    FillAllBays()
    {
        for (const SlotEntry of this.Bays)
            if (SlotEntry.Phase === "empty")
                this.TriggerJoin(SlotEntry.Slot);
    }

    ClearAllBays()
    {
        for (const SlotEntry of this.Bays)
            if (SlotEntry.Phase === "parked")
                this.TriggerLeave(SlotEntry.Slot);
    }

    Advance(StepSeconds)
    {
        if (!this.QueryActive() || StepSeconds <= 0.0)
            return;

        let SceneTopologyDirty = false;
        for (const SlotEntry of this.Bays)
        {
            if (SlotEntry.Phase === "entering")
            {
                SlotEntry.Progress = Math.min(1.0, SlotEntry.Progress + StepSeconds / TransitionSeconds);
                const Smooth = EaseCinematic(SlotEntry.Progress);
                const Longitudinal = -ApproachDistance * (1.0 - Smooth);
                this.PositionBayCar(SlotEntry, Longitudinal);
                if (SlotEntry.Progress >= 1.0)
                {
                    SlotEntry.Phase = "parked";
                    this.PositionBayCar(SlotEntry, 0.0, false);
                    SceneTopologyDirty = true;
                }
            }
            else if (SlotEntry.Phase === "leaving")
            {
                SlotEntry.Progress = Math.min(1.0, SlotEntry.Progress + StepSeconds / TransitionSeconds);
                const Smooth = EaseCinematic(SlotEntry.Progress);
                // Reverses smoothly off the turntable toward the studio perimeter, then derezzes out
                const Longitudinal = -ApproachDistance * Smooth;
                this.PositionBayCar(SlotEntry, Longitudinal);
                if (SlotEntry.Progress >= 1.0)
                {
                    SlotEntry.Phase    = "empty";
                    SlotEntry.Occupied = false;
                    this.AssignBayVisibility(SlotEntry, false);
                    SceneTopologyDirty = true;
                }
            }
        }

        if (SceneTopologyDirty)
            this.SynchroniseGpuScene();

        if (!this.Settings.AutoSimulate)
            return;

        this.Timer -= StepSeconds;
        if (this.Timer > 0.0)
            return;
        this.Timer = Math.max(2.0, Number(this.Settings.IntervalSeconds) || 6.5);

        // Don't start a new event while a car is mid-roll unless nothing else can run
        const Busy = this.Bays.some((Entry) => Entry.Phase === "entering" || Entry.Phase === "leaving");
        if (Busy)
            return;

        const EmptyCount  = this.Bays.filter((Entry) => Entry.Phase === "empty").length;
        const ParkedCount = this.Bays.filter((Entry) => Entry.Phase === "parked").length;
        if (EmptyCount > 0 && (ParkedCount <= 1 || Math.random() < 0.58))
            this.TriggerJoin();
        else if (ParkedCount > 0)
            this.TriggerLeave();
    }

    PositionBayCar(SlotEntry, LongitudinalOffset, Moving = true)
    {
        const Scene = this.Host.Scene;
        const { Placement, Body, Plate, Wheels } = SlotEntry.Bay;
        const Yaw = Placement.Yaw;
        const ForwardX = Math.cos(Yaw);
        const ForwardY = Math.sin(Yaw);
        const PosX = Placement.Position[0] + ForwardX * LongitudinalOffset;
        const PosY = Placement.Position[1] + ForwardY * LongitudinalOffset;
        const YawQuat = [0.0, 0.0, Math.sin(0.5 * Yaw), Math.cos(0.5 * Yaw)];
        const BodyTransform = ConstructRigidTransform([PosX, PosY, VehicleGeometry.CoMHeight], YawQuat);

        const Spin = LongitudinalOffset / WheelRadius;
        const SpinQuat = ConstructAxisQuaternion([0.0, 1.0, 0.0], Spin);
        const WheelOrientation = MultiplyQuaternion(YawQuat, SpinQuat);

        Scene.RetainOccupantTransform(Body);
        Scene.Occupants[Body].Transform = BodyTransform;
        Scene.Occupants[Body].Moving = Moving;

        Scene.RetainOccupantTransform(Plate);
        Scene.Occupants[Plate].Transform = BodyTransform;
        Scene.Occupants[Plate].Moving = Moving;

        for (let Wheel = 0; Wheel < 4; ++Wheel)
        {
            const WheelOcc = Wheels[Wheel];
            Scene.RetainOccupantTransform(WheelOcc);
            const Mount = ResolveSuspensionMountLocal(Wheel);
            const Hub = TransformPoint(BodyTransform, [Mount[0], Mount[1], Mount[2] - StaticStrutLength]);
            Scene.Occupants[WheelOcc].Transform = ConstructRigidTransform(Hub, WheelOrientation);
            Scene.Occupants[WheelOcc].Moving = Moving;
        }
        this.Host.PriorValid = false;
    }

    ApplyDriverAppearance(SlotEntry)
    {
        const Host = this.Host;
        const Scene = Host.Scene;
        const Driver = SlotEntry.Driver;
        if (!Host.Materials || !Host.MaterialRecords || !Driver)
            return;

        const PaintMat = Host.Materials[SlotEntry.Bay.PaintFamily];
        if (PaintMat && Driver.Colour)
        {
            PaintMat.Albedo     = LinearColour(Driver.Colour);
            PaintMat.Roughness  = Driver.Metallic === false ? 0.42 : 0.085;
            PaintMat.Metalness  = Driver.Metallic === false ? 0.08 : 0.88;
            PaintMat.CoatWeight = Driver.Metallic === false ? 0.25 : 1.0;
            Host.Exchange.Device.queue.writeBuffer(
                Host.MaterialRecords,
                SlotEntry.Bay.PaintFamily * MaterialRecordBytes,
                EncodeMaterialRecords([PaintMat])
            );
        }

        const LightMat = Host.Materials[SlotEntry.Bay.LightFamily];
        if (LightMat && Driver.Light)
        {
            LightMat.Emission = Driver.Light.slice();
            Host.Exchange.Device.queue.writeBuffer(
                Host.MaterialRecords,
                SlotEntry.Bay.LightFamily * MaterialRecordBytes,
                EncodeMaterialRecords([LightMat])
            );
        }

        const TargetPlateSet = Scene.GuestPlateSets.get(Driver.NameTag);
        if (TargetPlateSet !== undefined)
            Scene.Occupants[SlotEntry.Bay.Plate].FacetSet = TargetPlateSet;
    }

    AssignBayVisibility(SlotEntry, Visible)
    {
        const Scene = this.Host.Scene;
        const Indices = [SlotEntry.Bay.Body, SlotEntry.Bay.Plate, ...SlotEntry.Bay.Wheels];
        const Bounds = Indices.map((Idx) => Scene.QueryWorldBounds(Idx));
        for (const Idx of Indices)
        {
            Scene.Occupants[Idx].Inactive = !Visible;
            Scene.Occupants[Idx].Moving = Visible;
        }
        this.Host.Distance?.InvalidateBounds(Bounds);
        this.SynchroniseGpuScene();
    }

    SynchroniseGpuScene()
    {
        const Host = this.Host;
        const Scene = Host.Scene;
        if (!Scene || !Host.Exchange?.Device)
            return;
        Scene.ConstructDrawRuns();
        Scene.ConstructDistanceList();
        Host.Distance?.UploadDistanceList(Scene.DistanceList);
        Host.Cards?.RebuildTileLists(Scene);
        if (Host.Emissive)
            Host.Emissive.StaticSignature = "";
        Host.Exchange.Device.queue.writeBuffer(Host.OccupantRecords, 0, Scene.EncodeOccupantRecords(Host.Volumes));
        Host.PriorValid = false;
    }

    SpawnBayParticles(SlotEntry, HexColour)
    {
        const Effects = this.Host?.Effects;
        if (!Effects || !Effects.BurstTyre)
            return;
        const { Placement } = SlotEntry.Bay;
        const Transform = ConstructRigidTransform([Placement.Position[0], Placement.Position[1], 0.55], [0, 0, 0, 1]);
        Effects.BurstTyre(Transform, [1.8, 0.9, 0.65], [0.0, 0.0, 0.9]);
        if (HexColour) {} // visual accent already applied via material upload
    }

    RecordEvent(Summary)
    {
        this.History.unshift(Summary);
        if (this.History.length > 8)
            this.History.pop();
        if (this.Host?.Shell?.Announce)
            this.Host.Shell.Announce(Summary);
        if (this.Host?.LogStream)
            this.Host.LogStream.RecordDiagnostic("Network", Summary, "LobbySession.js", "info", "display");
    }
}
