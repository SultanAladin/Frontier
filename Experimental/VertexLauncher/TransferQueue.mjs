/*====================================================================================================================================
                                                            TRANSFERQUEUE.MJS
====================================================================================================================================*/
// 🧩 Deterministic local transfer queue projection for install and update cards

export function ConstructTransfer(GameEntry, Category = "install", InitialFraction = 0)
{
    const TotalMegabytes = Category === "update" ? Math.round(GameEntry.Megabytes * 0.08) : GameEntry.Megabytes;
    return {
        Identifier: GameEntry.Identifier,
        Title: GameEntry.Title,
        Cover: GameEntry.Cover,
        Category,
        TotalMegabytes,
        TransferredMegabytes: Math.round(TotalMegabytes * Math.min(1, Math.max(0, InitialFraction))),
        Condition: InitialFraction > 0 ? "active" : "queued",
        SpeedMegabytes: 24.6
    };
}

export function AdvanceTransferQueue(TransferQueue, ElapsedSeconds, SpeedLimit = 0)
{
    const NextQueue = TransferQueue.map(Transfer => ({ ...Transfer }));
    let ActiveTransfer = NextQueue.find(Transfer => Transfer.Condition === "active");
    const PausedTransferExists = NextQueue.some(Transfer => Transfer.Condition === "paused");
    if (!ActiveTransfer && !PausedTransferExists)
    {
        const QueuedTransfer = NextQueue.find(Transfer => Transfer.Condition === "queued");
        if (QueuedTransfer)
        {
            QueuedTransfer.Condition = "active";
            ActiveTransfer = QueuedTransfer;
        }
    }
    if (!ActiveTransfer) return NextQueue;

    const SpeedWave = Math.sin(ActiveTransfer.TransferredMegabytes / 900) * 4.2
        + Math.sin(ActiveTransfer.TransferredMegabytes / 233) * 1.8;
    ActiveTransfer.SpeedMegabytes = Math.min(34, Math.max(12, 24.6 + SpeedWave));
    const EffectiveSpeed = SpeedLimit > 0 ? Math.min(ActiveTransfer.SpeedMegabytes, SpeedLimit) : ActiveTransfer.SpeedMegabytes;
    ActiveTransfer.TransferredMegabytes = Math.min(
        ActiveTransfer.TotalMegabytes,
        ActiveTransfer.TransferredMegabytes + EffectiveSpeed * Math.max(0, ElapsedSeconds)
    );
    if (ActiveTransfer.TransferredMegabytes >= ActiveTransfer.TotalMegabytes)
    {
        ActiveTransfer.Condition = "completed";
        const QueuedTransfer = NextQueue.find(Transfer => Transfer.Condition === "queued");
        if (QueuedTransfer) QueuedTransfer.Condition = "active";
    }
    return NextQueue;
}

export function PauseTransfer(TransferQueue, Identifier)
{
    return TransferQueue.map(Transfer => Transfer.Identifier === Identifier && Transfer.Condition === "active"
        ? { ...Transfer, Condition: "paused" }
        : { ...Transfer });
}

export function ResumeTransfer(TransferQueue, Identifier)
{
    const PausedQueue = TransferQueue.map(Transfer => Transfer.Identifier === Identifier && Transfer.Condition === "paused"
        ? { ...Transfer, Condition: "active" }
        : { ...Transfer });
    const ActiveCount = PausedQueue.filter(Transfer => Transfer.Condition === "active").length;
    if (ActiveCount <= 1) return PausedQueue;
    let EncounteredResumed = false;
    return PausedQueue.map(Transfer =>
    {
        if (Transfer.Identifier !== Identifier || Transfer.Condition !== "active") return Transfer;
        if (EncounteredResumed) return { ...Transfer, Condition: "queued" };
        EncounteredResumed = true;
        return Transfer;
    });
}

export function CancelTransfer(TransferQueue, Identifier)
{
    return TransferQueue.filter(Transfer => Transfer.Identifier !== Identifier || Transfer.Condition === "completed");
}

export function QueryTransferFraction(Transfer)
{
    return Transfer.TotalMegabytes === 0 ? 1 : Transfer.TransferredMegabytes / Transfer.TotalMegabytes;
}

export function FormatMegabytes(Megabytes)
{
    if (Megabytes >= 1024) return `${(Megabytes / 1024).toFixed(1)} GB`;
    return `${Math.round(Megabytes)} MB`;
}

export function FormatRemaining(Transfer, SpeedLimit = 0)
{
    if (Transfer.Condition === "completed") return "Complete";
    if (Transfer.Condition === "paused") return "Paused";
    if (Transfer.Condition === "queued") return "Queued";
    const EffectiveSpeed = SpeedLimit > 0 ? Math.min(Transfer.SpeedMegabytes, SpeedLimit) : Transfer.SpeedMegabytes;
    const RemainingSeconds = Math.ceil((Transfer.TotalMegabytes - Transfer.TransferredMegabytes) / EffectiveSpeed);
    if (RemainingSeconds < 60) return `${RemainingSeconds}s left`;
    return `${Math.ceil(RemainingSeconds / 60)} min left`;
}
