import assert from 'node:assert/strict';
const Models = ['spring', 'xpbd', 'shell', 'solid'];
const Seen = new Set();
export default {
    BeforeFrame(Host, Frame) {
        Host.FixedStep = 1 / 60;
        Host.ScriptedInput = { Throttle: 0, Brake: 0, Steer: 0, Handbrake: false };
        const Model = Models[Math.min(3, Math.floor(Frame / 4))];
        Host.Settings.TyreModel = Model;
        Host.Settings.XpbdTyres = true;
        Host.Settings.RayTracing = Frame >= 8;
        if (Frame % 4 === 0) { Host.ApplySetting('TyreModel'); Host.ApplySetting('RayTracing'); }
        if (Frame % 4 === 3) {
            const Previous = Models[Math.floor(Frame / 4)];
            assert.equal(Host.Vehicle.TyreModel, Previous);
            const Code = Previous === 'spring' ? 0 : Previous === 'xpbd' ? 1 : Previous === 'shell' ? 2 : 3;
            const Encoded = Host.VehicleVisual.EncodeRecord({}, Host.Vehicle.Wheels, Host.Vehicle.Orientation);
            assert.equal(Encoded[21], Code);
            assert.ok(Host.Vehicle.Wheels.every(Wheel => Number.isFinite(Wheel.TyreForce)));
            Seen.add(Previous);
        }
    },
    Report(Host) {
        assert.equal(Seen.size, 4);
        assert.equal(Host.Exchange.ReportedErrors.length, 0);
        console.log('Four tyre models validated, including shell/solid FEM and shared raster/BVH deformation.');
    }
};
