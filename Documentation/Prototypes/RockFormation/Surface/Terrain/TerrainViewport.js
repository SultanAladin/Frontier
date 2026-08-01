//========================================================================================================================
//                                               TerrainViewport.js                                                🧩
//========================================================================================================================
//
// 📝 The six viewport calls the verbatim EditorSurface.js makes, translated onto RockFormation's real host.
//
//    🔴 THIS IS NOT A PORT OF THE REFERENCE'S TerrainViewport.js, AND MUST NOT BECOME ONE. That file is a
//       self-contained mock: it owns a WebGPU device, a heightfield, a camera and a noise generator, and it
//       renders a procedural landscape from a per-card noise specification. RockFormation already has a real
//       renderer in Resolve/DeviceHost.js which draws a TRANSCRIBED VOXEL TREE — the two are not layered,
//       they are alternatives. Porting the reference's version would stand up a second WebGPU device and a
//       second canvas binding, and the visible symptom would be the rock vanishing behind a noise landscape
//       that ignores the graph entirely.
//
//    📝 So each of the six is one of three things:
//         inert    - the reference computed something RockFormation has no analogue for
//         forwards - maps cleanly onto host state the three-speed contract already carries
//         deferred - real, but owned by the host, so it is routed through the registered bindings
//
//    🔴 Nothing here calls WriteViewProfile or AssemblePipeline directly. Every change is announced through
//       the host's Notify so it lands on the right speed — a shading-mode flip that wrote the uniform itself
//       would repaint without restarting the sim, showing the new mode over the old rock.

//------------------------------------------------------------------------------------------------------------------------
//                                                   HOST BINDING
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The host registers these at boot. Keeping them in one record rather than importing DeviceHost here is
//    deliberate: this module then has no opinion about how the rock is drawn, and the host stays the only
//    place that knows the render loop's flags.

const Bindings =
{
    ViewProfile : null,                                             // the host's live view profile
    Notify      : null,                                             // (Reason, Detail) => void
    ApplyScale  : null                                              // (Divisor) => void
};

export function BindViewport(ViewProfile, Notify, ApplyScale)
{
    Bindings.ViewProfile = ViewProfile;
    Bindings.Notify      = Notify;
    Bindings.ApplyScale  = ApplyScale;
}

function Announce(Reason)
{
    if (Bindings.Notify) Bindings.Notify(Reason);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   INITIALIZE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 INERT. The reference provisioned its own device against the canvas it was handed. The host has already
//    provisioned the real one against #ResolveFrame by the time the editor boots, and a second
//    configure() on the same canvas invalidates the first context's swap chain — the rock would go black
//    with no error, because the failure is in a context the host still believes it owns.
export function InitializeTerrainViewport(TerrainCanvas)
{
    if (!TerrainCanvas) return;
    // 📝 Nothing to do. Left as a named no-op rather than deleted so the reference's call site stays intact.
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TERRAIN
//------------------------------------------------------------------------------------------------------------------------

// 🔴 INERT. The reference's Specification is a NOISE recipe — scale, octaves, persistence, lacunarity, seed
//    — read off whichever card is selected. RockFormation has no such thing: the surface comes from the
//    transcribed tree, so "what the viewport shows" is a property of the whole graph, not of the selected
//    card. Selecting a card here changes the PREVIEW THUMBNAIL, which the host already owns.
//
//    📝 The specification is retained for the readout only, so the progress pill can name what is selected.
export let DispatchedTerrain = null;

export function ReconfigureTerrain(Specification)
{
    DispatchedTerrain = Specification;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHADING
//------------------------------------------------------------------------------------------------------------------------

// 📝 FORWARDS. The reference's shading modes and RockFormation's resolve modes are the same idea — how to
//    colour the traced surface — and the shell's ShadingBar carries the original data-mode 0..3, so the
//    index passes straight through.
//
//    🔴 'view' speed, not 'dial'. A resolve-mode change alters how the existing rock is drawn and must NOT
//       restart the erosion — restarting would throw away the weathering the author is trying to inspect.
export function ReconfigureShading(ShadingMode)
{
    if (!Bindings.ViewProfile) return;

    const Mode = Number(ShadingMode);
    if (!Number.isFinite(Mode)) return;

    Bindings.ViewProfile.ResolveMode = Mode;
    Announce("view");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAMERA MODE
//------------------------------------------------------------------------------------------------------------------------

// 🔴 INERT. The reference switched between a 2D top-down heightfield view and a 3D orbit. RockFormation is
//    voxel-only — there is no 2D projection of a density grid that means anything, and the orbit camera is
//    the only camera. Flipping this silently would leave the author with a button that appears to do
//    nothing, so it reports instead.
export function ReconfigureCameraMode(CameraMode)
{
    if (CameraMode === '2D')
    {
        Announce("refused");
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 FORWARDS. The reference's Resolution is a pixel edge; the host's ApplyResolveScale takes a DIVISOR of
//    the frame extent, which is the same control expressed against the real canvas.
//
//    🔴 A divisor, not a size. Handing the host a pixel count would silently produce a 1/808 divisor — a
//       64x64 floor-clamped frame that looks like a catastrophic performance bug rather than a bad unit.
export function ReconfigureResolution(Resolution)
{
    if (!Bindings.ApplyScale) return;

    const Wanted = Number(Resolution);
    if (!Number.isFinite(Wanted) || Wanted <= 0) return;

    // 📝 The reference's dropdown offers 128/256/512; treat the largest as native and scale down from there.
    const Divisor = Math.max(1, Math.min(4, Math.round(512 / Wanted)));
    Bindings.ApplyScale(Divisor);
    Announce("view");
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENVIRONMENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 FORWARDS, selectively. The reference passed an arbitrary { DialNaming: Magnitude } patch. Only the
//    lighting terms have a counterpart on the view profile, and each is applied by name.
//
//    🔴 An unrecognised dial is REPORTED, not ignored. Silently dropping the patch is how a slider ends up
//       wired to nothing — it moves, the readout updates, and the render never changes.
const EnvironmentLanes =
{
    SolarAzimuth   : "SolarAzimuth",
    SolarElevation : "SolarElevation",
    AmbientWeight  : "AmbientWeight",
    ShadowWeight   : "ShadowWeight",
    CavityWeight   : "CavityWeight",
    SlopeWeight    : "SlopeWeight",
    Exposure       : "Exposure"
};

export function ReconfigureEnvironment(EnvironmentDials)
{
    if (!Bindings.ViewProfile || !EnvironmentDials) return;

    let Applied = 0;
    for (const [Dial, Magnitude] of Object.entries(EnvironmentDials))
    {
        const Lane = EnvironmentLanes[Dial];
        if (!Lane)
        {
            console.warn(`ReconfigureEnvironment: no view-profile lane for '${Dial}' — ignored`);
            continue;
        }
        Bindings.ViewProfile[Lane] = Number(Magnitude);
        Applied++;
    }

    if (Applied > 0) Announce("view");
}
