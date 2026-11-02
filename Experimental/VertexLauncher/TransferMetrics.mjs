/*====================================================================================================================================
                                                           TRANSFERMETRICS.MJS
====================================================================================================================================*/
// 🧩 Speed trace, remaining-time and storage projections for download insight cards

import { QueryTransferFraction } from "./TransferQueue.mjs?v=20261102-3";

export const DiskTotalMegabytes = 512 * 1024;
export const SystemReservedMegabytes = Math.round(84.2 * 1024);

export function ResolveEffectiveSpeed(Transfer, SpeedLimit = 0)
{
    if (!Transfer || Transfer.Condition !== "active") return 0;
    return SpeedLimit > 0 ? Math.min(Transfer.SpeedMegabytes, SpeedLimit) : Transfer.SpeedMegabytes;
}

export function AppendSpeedSample(SpeedTrace, Sample, Capacity = 48)
{
    const NextTrace = [...SpeedTrace, Math.max(0, Sample)];
    return NextTrace.slice(Math.max(0, NextTrace.length - Capacity));
}

export function EncodeSpeedTrace(SpeedTrace, Width = 320, Height = 72)
{
    if (SpeedTrace.length === 0) return { Line: "", Area: "", Peak: 0 };
    const Peak = Math.max(1, ...SpeedTrace);
    const Step = SpeedTrace.length > 1 ? Width / (SpeedTrace.length - 1) : Width;
    const Points = SpeedTrace.length === 1
        ? [0, Width].map(X => `${X.toFixed(1)},${(Height - (SpeedTrace[0] / Peak) * (Height - 8) - 4).toFixed(1)}`)
        : SpeedTrace.map((Sample, Index) =>
        {
            const X = Index * Step;
            const Y = Height - (Sample / Peak) * (Height - 8) - 4;
            return `${X.toFixed(1)},${Y.toFixed(1)}`;
        });
    const Line = Points.join(" ");
    const Area = `0,${Height} ${Line} ${Width},${Height}`;
    return { Line, Area, Peak };
}

export function ResolvePendingMegabytes(TransferQueue)
{
    return TransferQueue
        .filter(Transfer => Transfer.Condition !== "completed")
        .reduce((Total, Transfer) => Total + Transfer.TotalMegabytes * (1 - QueryTransferFraction(Transfer)), 0);
}

export function ResolveRemainingSeconds(TransferQueue, SpeedLimit = 0)
{
    const ActiveTransfer = TransferQueue.find(Transfer => Transfer.Condition === "active");
    const Speed = ResolveEffectiveSpeed(ActiveTransfer, SpeedLimit);
    if (Speed <= 0) return null;
    return Math.ceil(ResolvePendingMegabytes(TransferQueue) / Speed);
}

export function FormatDuration(Seconds)
{
    if (Seconds === null) return "Paused";
    if (Seconds <= 0) return "Done";
    if (Seconds < 60) return `${Seconds}s`;
    const Minutes = Math.floor(Seconds / 60);
    if (Minutes < 60) return `${Minutes}m ${Seconds % 60}s`;
    const Hours = Math.floor(Minutes / 60);
    return `${Hours}h ${Minutes % 60}m`;
}

export function ResolveStorageProjection(TransferQueue, InstalledMegabytes)
{
    const TransferredMegabytes = TransferQueue.reduce((Total, Transfer) => Total + Transfer.TransferredMegabytes, 0);
    const GamesMegabytes = InstalledMegabytes + TransferredMegabytes;
    const UsedMegabytes = Math.min(DiskTotalMegabytes, GamesMegabytes + SystemReservedMegabytes);
    const AvailableMegabytes = Math.max(0, DiskTotalMegabytes - UsedMegabytes);
    const RequiredMegabytes = ResolvePendingMegabytes(TransferQueue);
    return {
        TotalMegabytes: DiskTotalMegabytes,
        GamesMegabytes: Math.min(GamesMegabytes, DiskTotalMegabytes),
        SystemMegabytes: SystemReservedMegabytes,
        UsedMegabytes,
        AvailableMegabytes,
        RequiredMegabytes,
        Sufficient: AvailableMegabytes >= RequiredMegabytes
    };
}
