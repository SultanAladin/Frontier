/*==========================================================================================================================================
                                                   VOLUMESCALARRASTERIZATION.TEST.JS
==========================================================================================================================================*/
// 🧩 Bounded CPU output, visible terrain, idle refinement, ocean-only cache reuse, suspension and document replacement.

import { test } from 'node:test';
import Assert from 'node:assert/strict';
import { CreateVolumeScalarRasterization, SoftwarePreviewExtent, MaximumSoftwareWidth } from './VolumeScalarRasterization.js';

const Camera = { Yaw: -.35, Pitch: .72, Orbit: 400, Target: [0, 0, 0] };
const Author = () => [{
    Name: 'CPU coast', Color: '#b4a17d', Visible: true, Depth: 100, Caves: false, Resolution: 256, Vertical: 64,
    Strokes: [{ Points: [[0, 0]], Radius: 100, Erase: false }],
    Regions: [{ X0: -120, Z0: -120, X1: 120, Z1: 120, Edge: 20 }], Landforms: []
}];

function Initialize()
{
    const Surface = { dataset: { previewWidth: '64' } }, Images = [];
    const Display = { Width: 100, Height: 80, Visible: true };
    const Rasterization = CreateVolumeScalarRasterization(Surface, {
        Display: () => Display,
        Present: Image => Images.push({ ...Image, Revision: Surface.dataset.completedRevision })
    });
    return { Surface, Images, Display, Rasterization };
}

async function AwaitImage(Images, Predicate)
{
    const Started = performance.now();
    while (performance.now() - Started < 5000)
    {
        const Image = Images.find(Predicate);
        if (Image) return Image;
        await new Promise(Resolve => setTimeout(Resolve, 10));
    }
    Assert.fail('A complete software SDF image did not arrive.');
}

function LandPixels(Pixels)
{
    let Count = 0;
    for (let Index = 0; Index < Pixels.length; Index += 4)
        if (Pixels[Index] > Pixels[Index + 1] + 3 && Pixels[Index + 1] > Pixels[Index + 2] + 6) Count++;
    return Count;
}

test('software preview extents enforce their width, height and pixel budgets in landscape and portrait views', () =>
{
    for (const [Width, Height] of [[1000, 750], [3840, 2160], [300, 10000], [10000, 300], [300, 500], [1, 10000]])
    for (const Requested of [32, 48, 64, 96, 160, 900, 999999])
    {
        const Extent = SoftwarePreviewExtent(Width, Height, Requested);
        Assert.ok(Extent[0] <= MaximumSoftwareWidth && Extent[1] <= 240);
        Assert.ok(Extent[0] * Extent[1] <= 25600);
        Assert.ok(Extent.every(Coordinate => Coordinate > 0));
    }
    for (const Width of [996, 997, 1279, 923])
    for (const Requested of [64, 96, 160]) Assert.equal(SoftwarePreviewExtent(Width, 823, Requested)[0], Requested);
    for (const Invalid of [0, -1, Infinity, NaN]) Assert.equal(SoftwarePreviewExtent(Invalid, 100), null);
});

test('a single submitted camera presents real terrain quickly and refines automatically after motion settles', async () =>
{
    const { Rasterization, Images, Surface } = Initialize();
    try
    {
        Rasterization.Upload(Author());
        Rasterization.Draw(Camera, 0, 1, false);
        const Coarse = await AwaitImage(Images, Image => Image.Width === 48);
        const Refined = await AwaitImage(Images, Image => Image.Width === 64);
        Assert.ok(LandPixels(Coarse.Pixels) > 50);
        Assert.ok(LandPixels(Refined.Pixels) > 100);
        Assert.equal(Refined.Pixels.length, Refined.Width * Refined.Height * 4);
        for (let Index = 3; Index < Refined.Pixels.length; Index += 4) Assert.equal(Refined.Pixels[Index], 255);
        Assert.equal(Surface.dataset.renderMode, 'software');
        Assert.equal(Surface.dataset.graphicsContexts, '0');
        Assert.equal(Surface.dataset.completedRevision, Surface.dataset.volumeRevision);
        Assert.equal(Rasterization.Inspect('pixels').Complete, true);
        const Depth = Rasterization.Inspect('terrain');
        Assert.ok(new Float32Array(Depth.Pixels.buffer).some(Distance => Distance > 0 && Distance < 1e7));
    }
    finally { Rasterization.Dispose(); }
});

test('animated ocean recomposes changing pixels without recasting the cached 3D terrain rays', async () =>
{
    const { Rasterization, Images, Surface } = Initialize();
    try
    {
        Rasterization.Upload(Author());
        Rasterization.Draw(Camera, 0, 1, true, 0);
        const First = await AwaitImage(Images, Image => Image.Width === 64);
        const Terrain = Surface.dataset.terrainRenders, Rays = Surface.dataset.softwareRays;
        const Offset = Images.length;
        Rasterization.Draw(Camera, 3, 1, true, 3);
        const Next = await AwaitImage(Images, Image => Images.indexOf(Image) >= Offset && Image.Width === 64);
        Assert.notDeepEqual(Next.Pixels, First.Pixels);
        Assert.equal(Surface.dataset.terrainRenders, Terrain);
        Assert.equal(Surface.dataset.softwareRays, Rays);
    }
    finally { Rasterization.Dispose(); }
});

test('suspension cancels CPU work, resume keeps the document, and disposal stops future images', async () =>
{
    const { Rasterization, Images, Surface } = Initialize();
    try
    {
        Rasterization.Upload(Author());
        Rasterization.Draw(Camera, 0, 1, false);
        Rasterization.Suspend();
        await new Promise(Resolve => setTimeout(Resolve, 30));
        Assert.equal(Images.length, 0);
        Assert.equal(Rasterization.Inspect().Suspended, true);
        Rasterization.Resume();
        await AwaitImage(Images, Image => Image.Width === 64);
        Rasterization.Suspend();
        const Rays = Surface.dataset.softwareRays, Count = Images.length;
        await new Promise(Resolve => setTimeout(Resolve, 40));
        Assert.equal(Surface.dataset.softwareRays, Rays);
        Assert.equal(Images.length, Count);
        Rasterization.Dispose();
        Rasterization.Draw({ ...Camera, Orbit: 200 }, 1, 1, false);
        await new Promise(Resolve => setTimeout(Resolve, 20));
        Assert.equal(Images.length, Count);
    }
    finally { Rasterization.Dispose(); }
});

test('document replacement invalidates old completion, keeps uploaded snapshots immutable and excludes hidden terrain', async () =>
{
    const { Rasterization, Images, Surface } = Initialize();
    try
    {
        const Layers = Author();
        Rasterization.Upload(Layers);
        Layers[0].Visible = false;
        Rasterization.Draw(Camera, 0, 1, false);
        const Before = await AwaitImage(Images, Image => Image.Width === 64);
        Assert.ok(LandPixels(Before.Pixels) > 100, 'edits after Upload must not alter an in-progress snapshot');
        Rasterization.Upload(Layers);
        Assert.equal(Surface.dataset.completedRevision, undefined);
        const After = await AwaitImage(Images, Image => Image.Width === 64 && Image.Revision === '2');
        Assert.equal(LandPixels(After.Pixels), 0);
        Assert.equal(Surface.dataset.completedRevision, '2');
        Assert.equal(Surface.dataset.softwareUploads, '2');
        Assert.notDeepEqual(After.Pixels, Before.Pixels);
    }
    finally { Rasterization.Dispose(); }
});

test('resizing changes the ray grid without exceeding bounded CPU image storage', async () =>
{
    const { Rasterization, Images, Display } = Initialize();
    try
    {
        Rasterization.Upload(Author());
        Rasterization.Draw(Camera, 0, 1, false);
        await AwaitImage(Images, Image => Image.Width === 64);
        Display.Width = 80;
        Display.Height = 120;
        Rasterization.Draw(Camera, 0, 1, false);
        const Portrait = await AwaitImage(Images, Image => Image.Width === 64 && Image.Height === 96);
        Assert.equal(Portrait.Pixels.length, 64 * 96 * 4);
        Assert.deepEqual(Rasterization.Inspect('lose'), { Ignored: 'Software rendering has no WebGL context.' });
        Assert.deepEqual(Rasterization.Inspect('restore'), { Ignored: 'Software rendering has no WebGL context.' });
    }
    finally { Rasterization.Dispose(); }
});
