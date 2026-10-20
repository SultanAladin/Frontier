// Trace-selection validation: exercise explicit feature settings, not just URL parsing.
import assert from "node:assert/strict";

export default {
    BeforeFrame(Host) {
        const Selection = process.env.TRACE_SELECTION;
        if (Selection === "screen") {
            Host.Settings.ScreenTraversal = true;
            if ("ScreenIndirect" in Host.Settings) Host.Settings.ScreenIndirect = true;
            if ("ScreenReflection" in Host.Settings) Host.Settings.ScreenReflection = true;
            Host.Settings.TriangleReflection = true;
        }
        if (Selection === "rtx") Host.Settings.RayTracing = true;
        if (Selection === "gtx") Host.Settings.RayTracing = false;
    },
    Report(Host) {
        assert.ok(Host.Ready && Host.FrameIndex >= 12, "Expected twelve ready frames");
        const Selection = process.env.TRACE_SELECTION;
        if (Selection === "screen") {
            assert.equal(Host.Settings.ScreenTraversal, true);
            assert.equal(Host.Settings.TriangleReflection, true);
        }
        if (Selection === "rtx") assert.equal(Host.Settings.RayTracing, true);
        if (Selection === "gtx") assert.equal(Host.Settings.RayTracing, false);
        console.log("Verified trace selection:", Selection);
    }
};
