/*====================================================================================================================================
                                                      TERRAINVIEWPORT.JS
====================================================================================================================================*/
// 🧩 Three.js preview of the chosen generator entry — displaced plane, sky, sun, and shading modes

import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { Sky } from 'three/addons/objects/Sky.js';
import { ConstructSimplexField } from './SimplexField.js';
import { TerrainTokens } from '../Graph/CatalogueSpecifications.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const PlaneExtent      = 100;    // [m]  - Side length of the displaced preview plane
const OrthographicZoom = 8;      // [-]  - Zoom applied in the 2D top projection

//------------------------------------------------------------------------------------------------------------------------
//                                                     INTERNAL STATE
//------------------------------------------------------------------------------------------------------------------------

let RenderTarget      = null;
let ViewScene         = null;
let SolidCamera       = null;
let PlanarCamera      = null;
let OrbitRegulation   = null;
let TerrainSurface    = null;
let SkyDome           = null;
let SunLight          = null;
let SkyFill           = null;
let GroundBounce      = null;
let SceneFog          = null;
let EnvironmentProbe  = null;
let ProbeGenerator    = null;
let FrameRequested    = false;

const ViewportProfile =
{
    ShadingMode:   'textured',      // 'textured' | 'lambert' | 'flat' | 'wireframe'
    CameraMode:    '3D',            // '3D' | '2D'
    SunElevation:  20,              // [°]
    SunAzimuth:    45,              // [°]
    Turbidity:     10,              // [-]
    FogDensity:    0.005,           // [1/m]
    Resolution:    128             // [idx] - Plane subdivisions per axis
};

let TerrainSpecification =
{
    GeneratorToken: 'default',
    NoiseScale:     0.1,
    Amplitude:      2.0,
    Octaves:        4,
    Persistence:    0.5,
    Lacunarity:     2.0,
    FieldSeed:      0
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

function ResolveSunDirection()
{
    const ElevationRadians = THREE.MathUtils.degToRad(ViewportProfile.SunElevation);
    const AzimuthRadians   = THREE.MathUtils.degToRad(ViewportProfile.SunAzimuth);
    return new THREE.Vector3(
        100 * Math.cos(ElevationRadians) * Math.cos(AzimuthRadians),
        100 * Math.sin(ElevationRadians),
        100 * Math.cos(ElevationRadians) * Math.sin(AzimuthRadians)
    );
}

// 📝 Sunlight reddens and weakens as it grazes the horizon, because the path through the atmosphere
//    lengthens and scatters out the short wavelengths. Approximated against elevation: a warm, dim
//    disc at 0° easing to a bright neutral one high up, so dragging the elevation dial reads as a
//    sunrise rather than a lamp on a hinge.
function ResolveSunRadiance()
{
    const Elevation = ViewportProfile.SunElevation;

    // Horizon proximity: 1 at the horizon, 0 once the sun is well clear of it.
    const HorizonProximity = Math.pow(1 - THREE.MathUtils.clamp(Elevation / 25, 0, 1), 1.6);

    // Warmth is folded in as a colour temperature ramp from ~2000 K to ~6500 K daylight.
    const SunColour = new THREE.Color().setRGB(
        1.0,
        THREE.MathUtils.lerp(1.0, 0.45, HorizonProximity),
        THREE.MathUtils.lerp(0.98, 0.18, HorizonProximity)
    );

    // Below the horizon the disc contributes nothing; the sky fill carries the remaining twilight.
    const AboveHorizon = THREE.MathUtils.clamp(Elevation / 8, 0, 1);
    const Turbidity    = ViewportProfile.Turbidity;

    // Haze attenuates the direct disc while it feeds the ambient fill.
    const HazeLoss = 1 - THREE.MathUtils.clamp((Turbidity - 2) / 28, 0, 1) * 0.45;

    return {
        Colour:    SunColour,
        Intensity: 3.4 * AboveHorizon * HazeLoss,
        Proximity: HorizonProximity
    };
}

// 📝 Ambient light is derived from the sky rather than fixed, so a low sun genuinely darkens the
//    scene instead of leaving a flat 0.6 floor that washed the terrain out at every hour.
function ResolveSkyRadiance(SunRadiance)
{
    const Elevation = ViewportProfile.SunElevation;
    const Daylight  = THREE.MathUtils.clamp((Elevation + 6) / 30, 0, 1);

    // Zenith drifts from deep dusk blue toward pale daylight blue.
    const ZenithColour = new THREE.Color().setRGB(
        THREE.MathUtils.lerp(0.06, 0.55, Daylight),
        THREE.MathUtils.lerp(0.08, 0.70, Daylight),
        THREE.MathUtils.lerp(0.16, 1.00, Daylight)
    );

    // The ground bounce takes on the warmth of the low sun.
    const GroundColour = new THREE.Color().setRGB(
        THREE.MathUtils.lerp(0.05, 0.42, Daylight),
        THREE.MathUtils.lerp(0.05, 0.38, Daylight),
        THREE.MathUtils.lerp(0.06, 0.30, Daylight)
    ).lerp(SunRadiance.Colour, SunRadiance.Proximity * 0.35);

    return {
        ZenithColour,
        GroundColour,
        Intensity: THREE.MathUtils.lerp(0.12, 0.85, Daylight)
    };
}

// 📝 Displace a fresh plane's Z against the noise field. The geometry is rebuilt rather than mutated so
//    a resolution change and a parameter change take the same path.
function ConstructTerrainGeometry()
{
    const Subdivisions = ViewportProfile.Resolution;
    const Geometry = new THREE.PlaneGeometry(PlaneExtent, PlaneExtent, Subdivisions, Subdivisions);
    const Positions = Geometry.attributes.position;

    const Specification = TerrainSpecification;
    const GeneratorToken = Specification.GeneratorToken;

    if (GeneratorToken === 'default')
    {
        Geometry.computeVertexNormals();
        return Geometry;
    }

    const EvaluateNoise = ConstructSimplexField(Math.round(Specification.FieldSeed * 1000));
    const EffectiveScale = Specification.NoiseScale * 0.1;
    const OctaveTally = Math.max(1, Math.min(8, Math.round(Specification.Octaves)));

    // 📝 White noise ignores the octave chain entirely — it is per-vertex uncorrelated by definition.
    const WhiteNoiseCondition = GeneratorToken === 'gen_white';
    const RidgedCondition     = GeneratorToken === 'gen_cellular';

    for (let VertexIndex = 0; VertexIndex < Positions.count; VertexIndex++)
    {
        const SampleX = Positions.getX(VertexIndex) + Specification.FieldSeed * 100;
        const SampleY = Positions.getY(VertexIndex) + Specification.FieldSeed * 100;

        let Displacement = 0.0;

        if (WhiteNoiseCondition)
        {
            Displacement = (EvaluateNoise(SampleX * 12.9898, SampleY * 78.233)) * Specification.Amplitude * 0.5;
        }
        else
        {
            let OctaveScale     = EffectiveScale;
            let OctaveAmplitude = Specification.Amplitude;
            for (let OctaveIndex = 0; OctaveIndex < OctaveTally; OctaveIndex++)
            {
                const Sample = EvaluateNoise(SampleX * OctaveScale, SampleY * OctaveScale);
                Displacement += (RidgedCondition ? Math.abs(Sample) * 1.5 : Sample) * OctaveAmplitude;
                OctaveScale     *= Specification.Lacunarity;
                OctaveAmplitude *= Specification.Persistence;
            }
        }

        Positions.setZ(VertexIndex, Displacement);
    }

    Geometry.computeVertexNormals();
    return Geometry;
}

function ConstructShadingMaterial()
{
    switch (ViewportProfile.ShadingMode)
    {
        case 'wireframe': return new THREE.MeshBasicMaterial({ wireframe: true, color: 0xb0fab0 });
        case 'flat':      return new THREE.MeshBasicMaterial({ color: 0x888888 });
        case 'lambert':   return new THREE.MeshLambertMaterial({ color: 0x8a9a8a });
        // 📝 A rough dielectric with a low env contribution: the sky probe tints the terrain without
        //    turning it into a mirror, so the shading tracks the time of day.
        default:          return new THREE.MeshStandardMaterial({
                              color: 0x9a9086, roughness: 0.92, metalness: 0.0,
                              envMapIntensity: 0.55, side: THREE.DoubleSide });
    }
}

function ResolveActiveCamera()
{
    return ViewportProfile.CameraMode === '2D' ? PlanarCamera : SolidCamera;
}

function InscribeFrame()
{
    FrameRequested = false;
    if (!RenderTarget) return;
    RenderTarget.render(ViewScene, ResolveActiveCamera());
}

function RequestFrame()
{
    if (FrameRequested) return;
    FrameRequested = true;
    requestAnimationFrame(InscribeFrame);
}

function ReconstructTerrainSurface()
{
    if (!ViewScene) return;

    if (TerrainSurface)
    {
        ViewScene.remove(TerrainSurface);
        TerrainSurface.geometry.dispose();
        TerrainSurface.material.dispose();
    }

    TerrainSurface = new THREE.Mesh(ConstructTerrainGeometry(), ConstructShadingMaterial());
    TerrainSurface.rotation.x = -Math.PI / 2;
    TerrainSurface.receiveShadow = true;
    TerrainSurface.castShadow = true;
    ViewScene.add(TerrainSurface);

    RequestFrame();
}

function AlignEnvironment()
{
    const SunDirection = ResolveSunDirection();
    const SunRadiance  = ResolveSunRadiance();
    const SkyRadiance  = ResolveSkyRadiance(SunRadiance);

    if (SunLight)
    {
        SunLight.position.copy(SunDirection);
        SunLight.color.copy(SunRadiance.Colour);
        SunLight.intensity = SunRadiance.Intensity;
        SunLight.castShadow = SunRadiance.Intensity > 0.01;
        SunLight.shadow.camera.updateProjectionMatrix();
    }

    if (SkyFill)
    {
        SkyFill.color.copy(SkyRadiance.ZenithColour);
        SkyFill.groundColor.copy(SkyRadiance.GroundColour);
        SkyFill.intensity = SkyRadiance.Intensity;
    }

    if (GroundBounce)
    {
        // A dim counter-light keeps slopes facing away from the sun from crushing to pure black.
        GroundBounce.position.set(-SunDirection.x, 40, -SunDirection.z);
        GroundBounce.color.copy(SkyRadiance.ZenithColour);
        GroundBounce.intensity = SkyRadiance.Intensity * 0.25;
    }

    if (SkyDome)
    {
        SkyDome.material.uniforms['sunPosition'].value.copy(SunDirection);
        SkyDome.material.uniforms['turbidity'].value = ViewportProfile.Turbidity;
        SkyDome.material.uniforms['rayleigh'].value  =
            THREE.MathUtils.lerp(1.2, 3.2, SunRadiance.Proximity);
    }

    if (ViewScene)
    {
        // 📝 Fog is expressed as a density dial mapped to a linear far plane of 1/density, but the
        //    colour is now sampled from the sky so distant terrain recedes into the horizon it is
        //    actually standing against instead of a fixed slate grey.
        if (ViewportProfile.FogDensity > 0)
        {
            const FogColour = SkyRadiance.ZenithColour.clone()
                .lerp(SunRadiance.Colour, SunRadiance.Proximity * 0.5)
                .multiplyScalar(0.55 + 0.45 * THREE.MathUtils.clamp(ViewportProfile.SunElevation / 30, 0, 1));

            if (!SceneFog) SceneFog = new THREE.Fog(FogColour.getHex(), 1, 200);
            SceneFog.color.copy(FogColour);
            SceneFog.far = 1 / ViewportProfile.FogDensity;
            SceneFog.near = Math.min(10, SceneFog.far * 0.05);
            ViewScene.fog = SceneFog;
        }
        else
        {
            ViewScene.fog = null;
        }
    }

    RefreshEnvironmentProbe();
    RequestFrame();
}

// 📝 Render the sky into a cubemap so the terrain material reflects the actual sky rather than a
//    flat colour. Rebuilt only when a dial moves, never per frame. The dome is lifted out of the
//    live scene and put back rather than cloned, because a Sky clone shares its material and the
//    probe would capture whatever uniforms the original happened to hold.
function RefreshEnvironmentProbe()
{
    if (!ProbeGenerator || !SkyDome || !ViewScene) return;

    if (EnvironmentProbe) EnvironmentProbe.dispose();

    ViewScene.remove(SkyDome);
    const ProbeScene = new THREE.Scene();
    ProbeScene.add(SkyDome);
    EnvironmentProbe = ProbeGenerator.fromScene(ProbeScene);
    ProbeScene.remove(SkyDome);
    ViewScene.add(SkyDome);

    ViewScene.environment = EnvironmentProbe.texture;
}

function AlignProjection()
{
    const Frame = RenderTarget.domElement.parentElement;
    if (!Frame) return;

    const FrameWidth  = Math.max(1, Frame.clientWidth);
    const FrameHeight = Math.max(1, Frame.clientHeight);
    const AspectRatio = FrameWidth / FrameHeight;

    RenderTarget.setSize(FrameWidth, FrameHeight, false);

    SolidCamera.aspect = AspectRatio;
    SolidCamera.updateProjectionMatrix();

    const PlanarHalfHeight = PlaneExtent / OrthographicZoom * 4;
    PlanarCamera.left   = -PlanarHalfHeight * AspectRatio;
    PlanarCamera.right  =  PlanarHalfHeight * AspectRatio;
    PlanarCamera.top    =  PlanarHalfHeight;
    PlanarCamera.bottom = -PlanarHalfHeight;
    PlanarCamera.updateProjectionMatrix();

    RequestFrame();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Initialize the preview against a canvas already present in the document.
export function InitializeTerrainViewport(TerrainCanvas)
{
    RenderTarget = new THREE.WebGLRenderer({ canvas: TerrainCanvas, antialias: true });
    RenderTarget.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    RenderTarget.setClearColor(0x0a0a0b, 1);

    // 📝 A physical sky emits well above 1.0, so without tonemapping the whole dome clips to white.
    RenderTarget.toneMapping         = THREE.ACESFilmicToneMapping;
    RenderTarget.toneMappingExposure = 0.62;
    RenderTarget.outputColorSpace    = THREE.SRGBColorSpace;

    ViewScene = new THREE.Scene();

    SolidCamera = new THREE.PerspectiveCamera(50, 1, 0.1, 2000);
    SolidCamera.position.set(0, 20, 20);

    PlanarCamera = new THREE.OrthographicCamera(-50, 50, 50, -50, 0.1, 1000);
    PlanarCamera.position.set(0, 100, 0);
    PlanarCamera.lookAt(0, 0, 0);

    SkyDome = new Sky();
    SkyDome.scale.setScalar(45000);
    SkyDome.material.uniforms['rayleigh'].value        = 2;
    SkyDome.material.uniforms['mieCoefficient'].value  = 0.005;
    SkyDome.material.uniforms['mieDirectionalG'].value = 0.8;
    ViewScene.add(SkyDome);

    // 📝 A hemisphere fill replaces the flat ambient term: sky above, warmed ground bounce below,
    //    both re-derived from the sun elevation whenever a dial moves.
    SkyFill = new THREE.HemisphereLight(0x88aaff, 0x4a4038, 0.6);
    ViewScene.add(SkyFill);

    SunLight = new THREE.DirectionalLight(0xffffff, 2.2);
    SunLight.castShadow = true;
    SunLight.shadow.mapSize.set(2048, 2048);
    SunLight.shadow.camera.near = 1;
    SunLight.shadow.camera.far  = 400;
    SunLight.shadow.camera.left   = -PlaneExtent * 0.75;
    SunLight.shadow.camera.right  =  PlaneExtent * 0.75;
    SunLight.shadow.camera.top    =  PlaneExtent * 0.75;
    SunLight.shadow.camera.bottom = -PlaneExtent * 0.75;
    SunLight.shadow.bias = -0.0005;
    ViewScene.add(SunLight);

    GroundBounce = new THREE.DirectionalLight(0x6688aa, 0.2);
    ViewScene.add(GroundBounce);

    RenderTarget.shadowMap.enabled = true;
    RenderTarget.shadowMap.type    = THREE.PCFSoftShadowMap;

    ProbeGenerator = new THREE.PMREMGenerator(RenderTarget);
    ProbeGenerator.compileEquirectangularShader();

    OrbitRegulation = new OrbitControls(SolidCamera, RenderTarget.domElement);
    OrbitRegulation.enableDamping = true;
    OrbitRegulation.dampingFactor = 0.08;
    OrbitRegulation.addEventListener('change', RequestFrame);

    // 📝 Damping needs a continuous tick to settle; the loop only re-renders while the user is orbiting.
    const AdvanceDamping = () =>
    {
        requestAnimationFrame(AdvanceDamping);
        if (OrbitRegulation.enabled) OrbitRegulation.update();
    };
    AdvanceDamping();

    AlignEnvironment();
    ReconstructTerrainSurface();
    AlignProjection();

    const FrameObserver = new ResizeObserver(AlignProjection);
    FrameObserver.observe(TerrainCanvas.parentElement);
}

// Reconfigure the terrain from the chosen entry's resolved generator parameters.
export function ReconfigureTerrain(Specification)
{
    TerrainSpecification = Specification;
    ReconstructTerrainSurface();
}

export function ReconfigureShading(ShadingMode)
{
    ViewportProfile.ShadingMode = ShadingMode;
    if (TerrainSurface)
    {
        TerrainSurface.material.dispose();
        TerrainSurface.material = ConstructShadingMaterial();
        RequestFrame();
    }
}

export function ReconfigureCameraMode(CameraMode)
{
    ViewportProfile.CameraMode = CameraMode;
    OrbitRegulation.enabled = CameraMode !== '2D';
    AlignProjection();
}

export function ReconfigureResolution(Resolution)
{
    ViewportProfile.Resolution = Resolution;
    ReconstructTerrainSurface();
}

export function ReconfigureEnvironment(EnvironmentDials)
{
    Object.assign(ViewportProfile, EnvironmentDials);
    AlignEnvironment();
}

export { TerrainTokens };
