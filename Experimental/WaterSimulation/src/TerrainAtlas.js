/*====================================================================================================================================
                                                        TERRAIN ATLAS.JS
====================================================================================================================================*/
// 🧩 Bakes the analytic shoreline once into (height, normal xyz) so every runtime stage samples instead of re-evaluating

export class TerrainAtlas
{
    constructor(Device, Programs, Descriptor = {})
    {
        this.Device = Device;
        this.Programs = Programs;
        this.Domain = Descriptor.Domain ?? 900;
        this.Resolution = Descriptor.Resolution ?? 1024;
        this.Target = Device.CreateTarget({
            Label:  'TerrainAtlas',
            Width:  this.Resolution,
            Height: this.Resolution,
            Format: 'rgba16f',
            Filter: 'linear'
        });

        this.Bake();
    }

    Bake()
    {
        const Program = this.Programs.get('TerrainBake').Use();
        const Uniforms = Program.Uniforms;

        Uniforms.SetFloat('uTerrainDomain', this.Domain);
        Uniforms.SetFloat('uBakeResolution', this.Resolution);

        this.Target.Bind();
        this.Device.DrawFullscreen();
    }

    Bind(Uniforms, Unit = 12)
    {
        Uniforms.SetTexture('uTerrainAtlas', this.Target.ColorTexture, Unit);
        Uniforms.SetFloat('uTerrainDomain', this.Domain);
        return this;
    }
}
