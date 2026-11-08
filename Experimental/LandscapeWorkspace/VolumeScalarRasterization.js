/*==========================================================================================================================================
                                                       VOLUMESCALARRASTERIZATION.JS
==========================================================================================================================================*/
// 🧩 Yielding software ray intersections of the authored 3D field, cached terrain shading and animated procedural ocean.

import { CompileVolumeRayField, EmptyRayDistance, IntersectVolumeRay, NormalizeDirection, ProjectVolumeCamera, ProjectVolumeRay } from './VolumeRayField.js';
import { EvaluateOcean } from './OceanSurface.js';
import { LatticeNoise } from './TerrainNoise.js';

export const MaximumSoftwareWidth = 160;
const Sun = NormalizeDirection([-.52, .68, -.44]);
const Clamp = (Number, Minimum = 0, Maximum = 1) => Math.max(Minimum, Math.min(Maximum, Number));
const Mix = (First, Second, Fraction) => First.map((Coordinate, Axis) => Coordinate + (Second[Axis] - Coordinate) * Fraction);
const Dot = (First, Second) => First.reduce((Sum, Coordinate, Axis) => Sum + Coordinate * Second[Axis], 0);
const Smooth = (First, Second, Coordinate) => { const Fraction = Clamp((Coordinate - First) / (Second - First)); return Fraction * Fraction * (3 - 2 * Fraction); };

//------------------------------------------------------------------------------------------------------------------------
//                                                    BOUNDED EXTENTS AND SHADING
//------------------------------------------------------------------------------------------------------------------------

export function SoftwarePreviewExtent(Width, Height, Requested = 160)
{
    if (![Width, Height, Requested].every(Number.isFinite) || Math.min(Width, Height, Requested) <= 0) return null;
    const Ratio = Math.min(1, Clamp(Requested, 32, MaximumSoftwareWidth) / Width, 240 / Height, Math.sqrt(25600 / (Width * Height)));
    return [Math.max(1, Math.floor(Width * Ratio + 1e-9)), Math.max(1, Math.floor(Height * Ratio + 1e-9))];
}

function Sky(Ray)
{
    const Elevation = Math.pow(Math.max(Ray[1], 0), .48);
    const Colour = Mix([.69, .76, .79], [.24, .39, .51], Elevation);
    const Glare = Math.pow(Math.max(Dot(Ray, Sun), 0), 650) * 2.4;
    return Colour.map((Coordinate, Axis) => Coordinate + [1, .86, .65][Axis] * Glare);
}

function TerrainColour(Field, Hit, Ray)
{
    if (!Hit.Found) return Sky(Ray);
    const Layer = Field.Layers[Hit.LayerIndex];
    let Albedo = [1, 3, 5].map(Offset => parseInt(Layer.Color.slice(Offset, Offset + 2), 16) / 255);
    const Sand = Mix([.55, .49, .36], Albedo, .28);
    Albedo = Mix(Sand, Albedo, Smooth(.3, 8, Hit.Position[1]));
    const Slope = .68 + .32 * Smooth(.3, .78, Hit.Normal[1]);
    const Diffuse = Math.max(Dot(Hit.Normal, Sun), 0);
    const Grain = .97 + .03 * LatticeNoise(...Hit.Position.map(Coordinate => Coordinate * .12), 381);
    const Colour = Albedo.map(Coordinate => Coordinate * Slope * (.36 + .64 * Diffuse) * Grain);
    return Mix(Colour, Sky(Ray), 1 - Math.exp(-Hit.Distance * .00007));
}

function ShadeOcean(
    Field,
    Eye,
    Ray,
    Terrain,
    Distance,
    Height,
    Wave,
    Ocean,
    Seconds)
{
    let Water = EmptyRayDistance;
    if (Ocean && Ray[1] < -.0001)
    {
        Water = (-.09 - Eye[1]) / Ray[1];
        for (let Refinement = 0; Refinement < 3 && Water >= 0; Refinement++)
        {
            const X = Eye[0] + Ray[0] * Water, Z = Eye[2] + Ray[2] * Water;
            const Damping = Smooth(-6, 28, Field.Shore(X, Z));
            const Surface = -.09 + EvaluateOcean(X, Z, Seconds, Wave, Math.max(.05, Water / Height * .9)).Height * Damping;
            Water = (Surface - Eye[1]) / Ray[1];
        }
        if (Water < 0) Water = EmptyRayDistance;
    }
    if (Distance < Water) return Terrain;
    if (Water >= 1e7) return Sky(Ray);
    const Position = Eye.map((Coordinate, Axis) => Coordinate + Ray[Axis] * Water);
    const Footprint = Math.max(.05, Water / Height * .9);
    const Shore = Field.Shore(Position[0], Position[2]);
    const Damping = Smooth(-6, 28, Shore);
    const Evaluation = EvaluateOcean(Position[0], Position[2], Seconds, Wave, Footprint);
    const Normal = NormalizeDirection([-Evaluation.SlopeX * Damping, 1, -Evaluation.SlopeZ * Damping]);
    const Facing = Math.max(-Dot(Ray, Normal), 0);
    const Fresnel = .0204 + .9796 * Math.pow(1 - Facing, 5);
    const Reflection = Sky(Ray.map((Coordinate, Axis) => Coordinate - 2 * Dot(Ray, Normal) * Normal[Axis]));
    const Transmission = Mix([.075, .205, .225], [.025, .105, .14], Smooth(0, 38, Shore));
    let Colour = Mix(Transmission, Reflection, Fresnel);
    const HalfDirection = NormalizeDirection(Sun.map((Coordinate, Axis) => Coordinate - Ray[Axis]));
    const Roughness = .17 + Math.min(Footprint * .006, .12);
    const Alignment = Math.max(Dot(Normal, HalfDirection), 0), Squared = Roughness * Roughness;
    const Denominator = Alignment * Alignment * (Squared - 1) + 1;
    const Distribution = Squared / (Math.PI * Denominator * Denominator);
    const Specular = Math.min(Distribution * .009, .65) * Math.max(Dot(Normal, Sun), 0);
    Colour = Colour.map((Coordinate, Axis) => Coordinate + [1, .88, .69][Axis] * Specular);
    const Turbulence = LatticeNoise(Position[0] * .15, Position[2] * .15, Seconds * .23, 733);
    const Foam = (1 - Smooth(.5, 3, Math.abs(Shore))) * Smooth(.36, .72, Turbulence);
    Colour = Mix(Colour, [.78, .85, .81], Foam * .68);
    return Mix(Colour, Sky(Ray), 1 - Math.exp(-Water * .000045));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     YIELDING IMAGE REFINEMENT
//------------------------------------------------------------------------------------------------------------------------

export function CreateVolumeScalarRasterization(Surface, Options = {})
{
    let Field = CompileVolumeRayField([]), Revision = 0, Cache = null, Sequence = null, Latest = null, Image = null;
    let Timer = null, Disposed = false, Suspended = false, FirstPresentation = false, LastPose = '', LastMotion = 0, LastPresentation = -Infinity;
    const Display = () => Options.Display?.() || { Width: Surface.clientWidth, Height: Surface.clientHeight, Visible: true };
    Surface.dataset.renderMode = 'software';
    Surface.dataset.renderer = 'CPU software · volumetric SDF ray intersections';
    Surface.dataset.graphicsContexts ||= '0';
    Surface.dataset.initializationEpoch = String(Number(Surface.dataset.initializationEpoch || 0) + 1);
    Surface.dataset.softwareReason = Options.Reason || 'WebGL2 is unavailable';
    for (const Name of ['completedRevision', 'completedComposition', 'submittedComposition']) delete Surface.dataset[Name];

    function Signal(Category, Message = '')
    {
        Surface.dataset.renderStatus = Category;
        Surface.dataset.renderMessage = Message;
        Options.Status?.(Category, Message);
    }

    function Schedule(Delay = 0)
    {
        if (!Timer && !Disposed && !Suspended) Timer = setTimeout(Refine, Delay);
    }

    function Upload(Layers)
    {
        Field = CompileVolumeRayField(Layers);
        Revision++;
        Cache = Sequence = null;
        FirstPresentation = false;
        for (const Name of ['completedRevision', 'completedComposition', 'submittedComposition']) delete Surface.dataset[Name];
        Surface.dataset.volumeRevision = String(Revision);
        Surface.dataset.softwareUploads = String(Number(Surface.dataset.softwareUploads || 0) + 1);
        Schedule();
    }

    function Submit(Camera, Seconds, Wave, Ocean, OceanTime = Seconds, Interacting = false)
    {
        const Pose = JSON.stringify(Camera);
        if (Pose !== LastPose) { LastPose = Pose; LastMotion = performance.now(); }
        Latest = { Camera: structuredClone(Camera), Seconds, Wave, Ocean, OceanTime, Interacting, Pose };
        if (Sequence && Sequence.Width > 48 && Sequence.Pose !== Pose) Sequence = null;
        Schedule();
    }

    function InitializeSequence()
    {
        if (!Latest) return false;
        const Extent = Display();
        if (!Extent.Visible) return false;
        const Moving = Latest.Interacting || performance.now() - LastMotion < 180;
        const Requested = Clamp(Number(Surface.dataset.previewWidth) || 160, 32, MaximumSoftwareWidth);
        const Size = SoftwarePreviewExtent(Extent.Width, Extent.Height, FirstPresentation && !Moving ? Requested : 48);
        if (!Size) return false;
        const [Width, Height] = Size;
        const Signature = JSON.stringify([Width, Height, Revision, Latest.Camera]);
        const Composition = JSON.stringify([Signature, Latest.Wave, Latest.Ocean, Latest.Ocean ? Latest.OceanTime : 0]);
        if (Image?.Composition === Composition)
        {
            if (Moving && !Latest.Interacting) Schedule(180);
            return false;
        }
        const Terrain = Cache?.Signature !== Signature;
        if (!Terrain && performance.now() - LastPresentation < 180) { Schedule(40); return false; }
        const Count = Width * Height;
        Sequence = {
            ...Latest, Width, Height, Signature, Composition, Terrain, Cursor: 0, Revision,
            Projection: ProjectVolumeCamera(Latest.Camera), Pixels: new Uint8ClampedArray(Count * 4),
            Distances: Terrain ? new Float32Array(Count) : Cache.Distances,
            Colours: Terrain ? new Float32Array(Count * 3) : Cache.Colours,
            Rays: Terrain ? new Float32Array(Count * 3) : Cache.Rays
        };
        Surface.dataset.submittedComposition = Composition;
        Signal(FirstPresentation ? 'refining' : 'rendering', 'Rendering the same 3D SDF in software; the editor remains usable.');
        return true;
    }

    function Refine()
    {
        Timer = null;
        if (Disposed || Suspended || !Latest) return;
        if (!Display().Visible) return;
        try
        {
            if (!Sequence && !InitializeSequence()) return;
            const Started = performance.now(), Current = Sequence, Count = Current.Width * Current.Height;
            do
            {
                const Index = Current.Cursor, X = Index % Current.Width, Y = Math.floor(Index / Current.Width);
                let Ray;
                if (Current.Terrain)
                {
                    Ray = ProjectVolumeRay(Current.Projection, X, Y, Current.Width, Current.Height);
                    const Hit = IntersectVolumeRay(Field, Current.Projection.Eye, Ray, Current.Height);
                    Current.Distances[Index] = Hit.Distance;
                    Current.Colours.set(TerrainColour(Field, Hit, Ray), Index * 3);
                    Current.Rays.set(Ray, Index * 3);
                    Surface.dataset.softwareRays = String(Number(Surface.dataset.softwareRays || 0) + 1);
                }
                else Ray = Current.Rays.subarray(Index * 3, Index * 3 + 3);
                const Colour = ShadeOcean(Field, Current.Projection.Eye, Ray, Current.Colours.subarray(Index * 3, Index * 3 + 3),
                    Current.Distances[Index], Current.Height, Current.Wave, Current.Ocean, Current.OceanTime);
                for (let Axis = 0; Axis < 3; Axis++) Current.Pixels[Index * 4 + Axis] = Math.pow(Math.max(Colour[Axis], 0), .88) * 255;
                Current.Pixels[Index * 4 + 3] = 255;
                Current.Cursor++;
            } while (Current.Cursor < Count && performance.now() - Started < 8);
            Surface.dataset.softwareMaximumSliceMilliseconds = String(Math.max(Number(Surface.dataset.softwareMaximumSliceMilliseconds || 0), performance.now() - Started));
            Surface.dataset.previewProgress = String(Current.Cursor / Count);
            Options.Progress?.(Current.Cursor / Count);
            if (Current.Cursor < Count) { Schedule(); return; }
            if (Current.Revision !== Revision) { Sequence = null; Schedule(); return; }
            if (Current.Terrain)
            {
                Cache = Current;
                Surface.dataset.terrainRenders = String(Number(Surface.dataset.terrainRenders || 0) + 1);
            }
            Image = Current;
            Sequence = null;
            FirstPresentation = true;
            LastPresentation = performance.now();
            Surface.dataset.completedOrbit = String(Current.Camera.Orbit);
            Surface.dataset.completedTarget = JSON.stringify(Current.Camera.Target);
            Surface.dataset.completedRevision = String(Revision);
            Surface.dataset.completedComposition = Current.Composition;
            Surface.dataset.bufferWidth = String(Current.Width);
            Surface.dataset.bufferHeight = String(Current.Height);
            Surface.dataset.previewProgress = '1';
            Surface.dataset.softwareImages = String(Number(Surface.dataset.softwareImages || 0) + 1);
            Options.Present?.({ Width: Current.Width, Height: Current.Height, Pixels: Current.Pixels.slice() });
            Signal('ready');
            Schedule();
        }
        catch (Failure) { Failed(Failure); }
    }

    function Suspend()
    {
        Suspended = true;
        clearTimeout(Timer);
        Timer = null;
    }

    function Resume()
    {
        Suspended = false;
        Schedule();
    }

    function Failed(Failure)
    {
        Suspend();
        Signal('failed', 'Software SDF preview could not finish: ' + Failure.message);
    }

    function Inspect(Category = 'status')
    {
        if (Category === 'status') return { ...Surface.dataset, Suspended, ContextLost: false };
        if (Category === 'lose' || Category === 'restore') return { Ignored: 'Software rendering has no WebGL context.' };
        if (!Image || !['pixels', 'terrain'].includes(Category)) throw Error('The software SDF image is still rendering.');
        if (Category === 'terrain' && !Cache) throw Error('The software SDF terrain is still rendering.');
        const Pixels = Category === 'terrain' ? new Uint8Array(Cache.Distances.buffer.slice(0)) : Image.Pixels.slice();
        return { Width: Image.Width, Height: Image.Height, Pixels, Complete: true, Error: 0, PixelOrder: 'top-down RGBA8', ...Surface.dataset };
    }

    function Dispose()
    {
        Suspend();
        Disposed = true;
        Cache = Sequence = Image = Latest = null;
    }

    Signal('rendering', 'Preparing a software-rendered 3D SDF preview.');
    return { Upload, Draw: Submit, Suspend, Resume, Failed, Inspect, Dispose, Ready: Promise.resolve() };
}
