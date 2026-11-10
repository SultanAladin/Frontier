import assert from "node:assert/strict";

let CreatedId = null;

export default {
    BeforeFrame(Host, Frame)
    {
        if (Frame === 0)
        {
            // 1. Select every camera, post-process, studio light, bay, and vehicle entity to exercise all Inspector pages
            for (const Id of [
                "editor-camera",
                "vehicle-camera",
                "cine-camera",
                "post-process",
                "studio-light-0",
                "studio-light-1",
                "studio-light-2",
                "lobby-bay-0",
                "vehicle"
            ])
            {
                Host.Shell.SelectEntity(Id);
            }
            Host.Shell.SelectFolder("Lights");

            // 2. Construct an editor Cube primitive and enable Array Modifier across modes
            const Created = Host.ConstructPrimitive("Cube");
            assert.ok(Created && Created.Id.startsWith("primitive-"), "ConstructPrimitive returns primitive-* entry");
            CreatedId = Created.Id;
            Host.Shell.SelectEntity(CreatedId);

            const Spec = Host.EnsureArrayModifier(CreatedId);
            Spec.Enabled = true;
            Spec.Mode = "circular";
            Spec.Count = 6;
            Spec.Radius = 3.5;
            Host.SyncArrayModifiers();
            Host.Shell.SelectEntity(CreatedId);

            // 3. Enter EDIT mode, snap VehicleCamera to RoofMount socket, verify socket debugs, and transform entities
            Host.AssignRunMode("EDIT");
            assert.equal(Host.SnapVehicleCameraToSocket("RoofMount"), true);
            const EditSockets = Host.QuerySocketLocations();
            assert.ok(EditSockets.length >= 10, "Socket visual debugs active in EDIT mode");
            assert.equal(Host.QueryVisualizableCameras().length, 2, "Vehicle & Lobby Cine camera frustums active in EDIT mode");

            Host.ApplyEntityTransformPose("vehicle-camera", { Position: [-1.8, 0.2, 1.5], Rotation: [0, 5, -2], Scale: [1, 1, 1] });
            Host.ApplyEntityTransformPose("cine-camera", { Position: [6.0, 4.0, 2.1], Rotation: [0, 8, -145], Scale: [1, 1, 1] });
            Host.ApplyEntityTransformPose("studio-light-1", { Position: [11.5, 4.5, 4.2], Rotation: [0, -18, -155], Scale: [1, 1, 1] });
            Host.ApplyEntityTransformPose(CreatedId, { Position: [2.5, -2.0, 0.8], Rotation: [0, 0, 25], Scale: [1.1, 1.1, 1.1] });
        }
        else if (Frame === 1)
        {
            // Verify 3D camera debug meshes are visible in EDIT mode when viewing through Editor Orbit camera
            assert.equal(Host.Scene.Occupants[Host.Scene.CameraOccupants.Vehicle].Inactive, false, "Vehicle camera 3D mesh visible in EDIT");
            assert.equal(Host.Scene.Occupants[Host.Scene.CameraOccupants.Cine].Inactive, false, "Lobby Cine camera 3D mesh visible in EDIT");

            // 4. Switch to VehicleCamera and toggle a Studio Light off
            Host.AssignCameraMode("VehicleCamera");
            assert.equal(Host.Settings.CameraMode, "VehicleCamera");
            if (Host.Lighting?.StudioLights?.[2])
            {
                Host.Lighting.StudioLights[2].Enabled = false;
                Host.SyncStudioLightMaterial();
            }
        }
        else if (Frame === 2)
        {
            // 5. Switch to Cockpit camera to exercise ExtendedCockpitExposure (bit 64) in DisplayExposure.wgsl
            if (Host.Lighting?.StudioLights?.[2])
            {
                Host.Lighting.StudioLights[2].Enabled = true;
                Host.SyncStudioLightMaterial();
            }
            Host.AssignCameraMode("Cockpit");
            assert.equal(Host.Settings.CameraMode, "Cockpit");
            assert.equal(Host.QueryInteriorCamera(), true);
        }
        else if (Frame === 3)
        {
            // 6. Switch to PLAY mode: ALL camera debug meshes, frustums, and socket debugs MUST hide!
            Host.AssignRunMode("PLAY");
            assert.equal(Host.QuerySocketLocations().length, 0, "Socket debugs hidden in PLAY mode");
            assert.equal(Host.QueryVisualizableCameras().length, 0, "Camera frustums hidden in PLAY mode");
            if (CreatedId)
            {
                const Spec = Host.EnsureArrayModifier(CreatedId);
                Spec.Mode = "grid";
                Spec.Columns = 3;
                Spec.Rows = 2;
                Spec.Layers = 1;
                Host.SyncArrayModifiers();
            }
        }
    },
    Report(Host)
    {
        // After Frame 3 ran in PLAY mode, both 3D camera debug meshes must be Inactive (hidden)!
        assert.equal(Host.Scene.Occupants[Host.Scene.CameraOccupants.Vehicle].Inactive, true, "Vehicle camera 3D mesh hidden in PLAY");
        assert.equal(Host.Scene.Occupants[Host.Scene.CameraOccupants.Cine].Inactive, true, "Cine camera 3D mesh hidden in PLAY");
    }
};
