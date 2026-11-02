/*====================================================================================================================================
                                                        SHADER LIBRARY.JS
====================================================================================================================================*/
// 🧩 Fetches the GLSL chunk/program files, registers includes, and compiles every program once

const ChunkFiles =
[
    'Common.glsl',
    'Noise.glsl',
    'TerrainShape.glsl',
    'TerrainSampling.glsl',
    'SkyRadiance.glsl',
    'WaveSampling.glsl',
    'WaterShading.glsl'
];

const ProgramFiles =
[
    { Label: 'FullscreenVertex', Vertex: 'Fullscreen.vert' },
    { Label: 'SpectrumInitialize', Vertex: 'Fullscreen.vert', Fragment: 'SpectrumInitialize.frag' },
    { Label: 'SpectrumEvolution', Vertex: 'Fullscreen.vert', Fragment: 'SpectrumEvolution.frag' },
    { Label: 'FftButterfly', Vertex: 'Fullscreen.vert', Fragment: 'FftButterfly.frag' },
    { Label: 'SpectralResolve', Vertex: 'Fullscreen.vert', Fragment: 'SpectralResolve.frag' },
    { Label: 'SpectralSlope', Vertex: 'Fullscreen.vert', Fragment: 'SpectralSlope.frag' },
    { Label: 'TerrainBake', Vertex: 'Fullscreen.vert', Fragment: 'TerrainBake.frag' },
    { Label: 'WaveFieldStep', Vertex: 'Fullscreen.vert', Fragment: 'WaveFieldStep.frag' },
    { Label: 'HeightAtlas', Vertex: 'Fullscreen.vert', Fragment: 'HeightAtlas.frag' },
    { Label: 'WaveFieldImpulse', Vertex: 'Impulse.vert', Fragment: 'Impulse.frag' },
    { Label: 'FoamStep', Vertex: 'Fullscreen.vert', Fragment: 'FoamStep.frag' },
    { Label: 'FoamSplat', Vertex: 'Impulse.vert', Fragment: 'FoamSplat.frag' },
    { Label: 'SprayUpdate', Vertex: 'SprayUpdate.vert', TransformVaryings: ['vPosition', 'vVelocity', 'vSpray'] },
    { Label: 'SprayRasterization', Vertex: 'SprayRasterization.vert', Fragment: 'SprayRasterization.frag' },
    { Label: 'SprayDeposit', Vertex: 'SprayDeposit.vert', Fragment: 'SprayDeposit.frag' },
    { Label: 'OceanSurface', Vertex: 'OceanSurface.vert', Fragment: 'OceanSurface.frag' },
    { Label: 'ShoreTerrain', Vertex: 'ShoreTerrain.vert', Fragment: 'ShoreTerrain.frag' },
    { Label: 'VesselHull', Vertex: 'VesselHull.vert', Fragment: 'VesselHull.frag' },
    { Label: 'SkyRadiance', Vertex: 'Fullscreen.vert', Fragment: 'Sky.frag' },
    { Label: 'SimulationGridOverlay', Vertex: 'GridOverlay.vert', Fragment: 'GridOverlay.frag' },
    { Label: 'BloomPrefilter', Vertex: 'Fullscreen.vert', Fragment: 'BloomPrefilter.frag' },
    { Label: 'BloomFilter', Vertex: 'Fullscreen.vert', Fragment: 'BloomFilter.frag' },
    { Label: 'PostResolve', Vertex: 'Fullscreen.vert', Fragment: 'PostResolve.frag' }
];

const BaseUrl = new URL('./shaders/', import.meta.url);

async function FetchText(RelativePath, OnProgress)
{
    const Response = await fetch(new URL(RelativePath, BaseUrl));
    if (!Response.ok)
    {
        throw new Error(`shader fetch failed: ${RelativePath} (${Response.status})`);
    }

    const Text = await Response.text();
    if (OnProgress) OnProgress(RelativePath);
    return Text;
}

export async function LoadShaderLibrary(Device, OnProgress)
{
    const ChunkEntries = await Promise.all(ChunkFiles.map(async (FileName) =>
    {
        const Source = await FetchText(`chunks/${FileName}`, OnProgress);
        return [FileName, Source];
    }));

    for (const [Name, Source] of ChunkEntries)
    {
        Device.RegisterChunk(Name, Source);
    }

    const Cache = new Map();
    const ReadProgram = async (FileName) =>
    {
        if (!Cache.has(FileName))
        {
            Cache.set(FileName, await FetchText(`programs/${FileName}`, OnProgress));
        }

        return Cache.get(FileName);
    };

    const Programs = new Map();

    for (const Descriptor of ProgramFiles)
    {
        const Vertex = await ReadProgram(Descriptor.Vertex);
        const Fragment = Descriptor.Fragment ? await ReadProgram(Descriptor.Fragment) : null;

        Programs.set(Descriptor.Label, Device.Compile({
            Label:             Descriptor.Label,
            Vertex:            Vertex,
            Fragment:          Fragment,
            TransformVaryings: Descriptor.TransformVaryings ?? null
        }));
    }

    return Programs;
}
