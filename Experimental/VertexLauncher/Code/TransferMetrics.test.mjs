/*====================================================================================================================================
                                                         TRANSFERMETRICS.TEST.MJS
====================================================================================================================================*/
// 🧩 Focused validation for speed trace encoding, remaining time and storage status

import test from "node:test";
import assert from "node:assert/strict";
import { ResolveCatalogueEntry } from "../Catalogue.mjs";
import { ConstructTransfer } from "../TransferQueue.mjs";
import {
    AppendSpeedSample, EncodeSpeedTrace, ResolveEffectiveSpeed, ResolveRemainingSeconds,
    FormatDuration, ResolveStorageProjection, DiskTotalMegabytes, SystemReservedMegabytes
} from "../TransferMetrics.mjs";

test("speed trace keeps a bounded recent window and encodes SVG points", () =>
{
    let Trace = [];
    for (let Index = 0; Index < 60; Index += 1) Trace = AppendSpeedSample(Trace, Index, 48);
    assert.equal(Trace.length, 48);
    const Encoded = EncodeSpeedTrace(Trace);
    assert.ok(Encoded.Line.split(" ").length === 48);
    assert.equal(Encoded.Peak, 59);
});

test("effective speed respects pause and limit", () =>
{
    const Transfer = ConstructTransfer(ResolveCatalogueEntry("alan-wake"), "install", 0.5);
    assert.equal(ResolveEffectiveSpeed(Transfer, 10), 10);
    assert.equal(ResolveEffectiveSpeed({ ...Transfer, Condition: "paused" }, 10), 0);
});

test("remaining time aggregates pending transfers and formats duration", () =>
{
    const Queue = [ConstructTransfer(ResolveCatalogueEntry("alan-wake"), "install", 0.5)];
    const Seconds = ResolveRemainingSeconds(Queue, 10);
    assert.ok(Seconds > 0);
    assert.equal(FormatDuration(null), "Paused");
    assert.equal(FormatDuration(0), "Done");
    assert.match(FormatDuration(3660), /1h 1m/);
});

test("storage projection turns red when pending downloads exceed free space", () =>
{
    const Queue = [ConstructTransfer(ResolveCatalogueEntry("alan-wake"), "install", 0)];
    const Tight = ResolveStorageProjection(Queue, DiskTotalMegabytes - 1024);
    assert.equal(Tight.Sufficient, false);
    const Roomy = ResolveStorageProjection(Queue, 0);
    assert.equal(Roomy.Sufficient, true);
    assert.equal(Roomy.SystemMegabytes, SystemReservedMegabytes);
    assert.ok(Roomy.GamesMegabytes >= 0);
    assert.ok(Roomy.AvailableMegabytes <= DiskTotalMegabytes - SystemReservedMegabytes);
});
