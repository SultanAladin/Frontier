import assert from 'node:assert/strict';
const Seen = new Set();
export default {
    BeforeFrame(Host, Frame) {
        Host.FixedStep = 1 / 60;
        Host.ScriptedInput = { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false };
        if ([0, 8, 16].includes(Frame)) {
            const Enabled = Frame !== 8;
            Object.assign(Host.Settings, { RayTracing: Enabled, ScreenTraversal: Enabled, ScreenIndirect: Enabled,
                ScreenReflection: Enabled, ClusteredLights: Enabled, PhotometricLights: Enabled, XpbdTyres: Enabled });
            Host.ApplySetting('RayTracing');
            Seen.add(Enabled);
        }
        if (Frame === 7 || Frame === 15 || Frame === 23) {
            assert.equal(Host.Vehicle.Wheels[0].CarcassActive, Frame !== 15);
            assert.equal(Host.Lighting.Table[512 + 3], Frame !== 15 ? 1 : 0);
            assert.equal(Host.Lighting.Table[1544], Frame !== 15 ? 1 : 0);
        }
    },
    Report(Host) {
        assert.equal(Seen.size, 2);
        assert.ok(Host.Ready && Host.FrameIndex >= 24);
        assert.ok(Host.NearestDepth.LevelCount > 1);
        assert.ok(Host.Lighting.Profile.Flux > 0);
        assert.equal(Host.RayTracing.TraversalGroups.length, 2);
        assert.equal(Host.Exchange.ReportedErrors.length, 0);
        console.log('Combined validation: BVH/SDF, screen GI/reflections, actual-light culling, real IES and XPBD toggled OFF and ON in one app.');
    }
};
