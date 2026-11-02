/*====================================================================================================================================
                                                         TRANSFERQUEUE.TEST.MJS
====================================================================================================================================*/
// 🧩 Focused validation for queue ordering, pause/resume, cancellation and completion

import test from "node:test";
import assert from "node:assert/strict";
import { Catalogue, ResolveCatalogueEntry, FormatPrice } from "../Catalogue.mjs";
import {
    ConstructTransfer, AdvanceTransferQueue, PauseTransfer, ResumeTransfer, CancelTransfer,
    QueryTransferFraction, FormatMegabytes, FormatRemaining
} from "../TransferQueue.mjs";

const AlanWake = ResolveCatalogueEntry("alan-wake");
const DeathStranding = ResolveCatalogueEntry("death-stranding");

test("catalogue resolves stable identifiers and formats prices", () =>
{
    assert.equal(Catalogue.length, 6);
    assert.equal(AlanWake.Title, "Alan Wake 2");
    assert.equal(FormatPrice(0), "Free");
    assert.equal(FormatPrice(39.99), "$39.99");
});

test("new install starts queued and update uses a smaller payload", () =>
{
    const Install = ConstructTransfer(AlanWake);
    const Update = ConstructTransfer(DeathStranding, "update");
    assert.equal(Install.Condition, "queued");
    assert.equal(Install.TotalMegabytes, AlanWake.Megabytes);
    assert.ok(Update.TotalMegabytes < DeathStranding.Megabytes);
});

test("advance promotes the first queued transfer and respects speed limit", () =>
{
    const Queue = [ConstructTransfer(AlanWake), ConstructTransfer(DeathStranding, "update")];
    const Advanced = AdvanceTransferQueue(Queue, 2, 10);
    assert.equal(Advanced[0].Condition, "active");
    assert.equal(Advanced[0].TransferredMegabytes, 20);
    assert.equal(Advanced[1].Condition, "queued");
});

test("pause freezes progress and resume restores one active transfer", () =>
{
    let Queue = [ConstructTransfer(AlanWake, "install", 0.5)];
    Queue = PauseTransfer(Queue, "alan-wake");
    const Frozen = Queue[0].TransferredMegabytes;
    Queue = AdvanceTransferQueue(Queue, 10);
    assert.equal(Queue[0].TransferredMegabytes, Frozen);
    Queue = ResumeTransfer(Queue, "alan-wake");
    assert.equal(Queue[0].Condition, "active");
});

test("completion promotes the next queued transfer and clamps at total", () =>
{
    const First = ConstructTransfer(AlanWake, "install", 0.999);
    const Second = ConstructTransfer(DeathStranding, "update");
    const Queue = AdvanceTransferQueue([First, Second], 10);
    assert.equal(Queue[0].Condition, "completed");
    assert.equal(Queue[0].TransferredMegabytes, Queue[0].TotalMegabytes);
    assert.equal(Queue[1].Condition, "active");
    assert.equal(QueryTransferFraction(Queue[0]), 1);
});

test("cancel removes an unfinished transfer but keeps completed history", () =>
{
    const Active = ConstructTransfer(AlanWake, "install", 0.4);
    const Completed = { ...ConstructTransfer(DeathStranding, "update"), Condition: "completed" };
    const Queue = CancelTransfer([Active, Completed], "alan-wake");
    assert.equal(Queue.length, 1);
    assert.equal(CancelTransfer(Queue, "death-stranding").length, 1);
});

test("remaining-time formatting respects the download limit", () =>
{
    const Transfer = ConstructTransfer(ResolveCatalogueEntry("alan-wake"), "install", 0.5);
    Transfer.SpeedMegabytes = 30;
    const Limited = FormatRemaining(Transfer, 10);
    const Unlimited = FormatRemaining(Transfer);
    assert.notEqual(Limited, Unlimited);
    assert.match(Limited, /min left/);
});

test("format helpers expose readable card metrics", () =>
{
    const Transfer = ConstructTransfer(AlanWake, "install", 0.5);
    assert.equal(FormatMegabytes(2048), "2.0 GB");
    assert.equal(FormatMegabytes(512), "512 MB");
    assert.match(FormatRemaining(Transfer), /min left/);
    assert.equal(FormatRemaining({ ...Transfer, Condition: "paused" }), "Paused");
});
