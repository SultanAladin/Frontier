/*====================================================================================================================================
                                                        FOAM FIELD.JS
====================================================================================================================================*/
// 🧩 Global foam grid: advects with the surface flow, breaks on crest curvature, and collects particle deposits

export class FoamField
{
    constructor(Device, Programs, Descriptor)
    {
        this.Device = Device;
        this.Programs = Programs;
        this.Domain = Descriptor.Domain ?? 400;
        this.Resolution = Descriptor.Resolution ?? 1024;
        this.MaxSplats = Descriptor.MaxSplats ?? 1024;
        this.Current = 0;
        this.SplatCount = 0;

        const TargetDescriptor =
        {
            Width:  this.Resolution,
            Height: this.Resolution,
            Format: 'rgba16f',
            Filter: 'linear'
        };

        this.Targets =
        [
            Device.CreateTarget({ ...TargetDescriptor, Label: 'FoamTargetA' }),
            Device.CreateTarget({ ...TargetDescriptor, Label: 'FoamTargetB' })
        ];

        const Gl = Device.Gl;
        this.SplatArray = new Float32Array(this.MaxSplats * 4);
        this.SplatBuffer = Gl.createBuffer();
        Gl.bindBuffer(Gl.ARRAY_BUFFER, this.SplatBuffer);
        Gl.bufferData(Gl.ARRAY_BUFFER, this.SplatArray.byteLength, Gl.DYNAMIC_DRAW);

        this.SplatArrayObject = Gl.createVertexArray();
        Gl.bindVertexArray(this.SplatArrayObject);
        Gl.bindBuffer(Gl.ARRAY_BUFFER, this.SplatBuffer);
        const Location = Gl.getAttribLocation(Programs.get('FoamSplat').Program, 'a_Splat');
        Gl.enableVertexAttribArray(Location);
        Gl.vertexAttribPointer(Location, 4, Gl.FLOAT, false, 0, 0);
        Gl.bindVertexArray(null);

        for (const Target of this.Targets)
        {
            Target.Bind();
            Gl.clearColor(0, 0, 0, 1);
            Gl.clear(Gl.COLOR_BUFFER_BIT);
        }

        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    }

    get Texture()
    {
        return this.Targets[this.Current].ColorTexture;
    }

    QueueSplat(WorldX, WorldZ, Strength, PointSize)
    {
        if (this.SplatCount >= this.MaxSplats)
        {
            return;
        }

        const Offset = this.SplatCount * 4;
        this.SplatArray[Offset + 0] = WorldX;
        this.SplatArray[Offset + 1] = WorldZ;
        this.SplatArray[Offset + 2] = Strength;
        this.SplatArray[Offset + 3] = PointSize;
        this.SplatCount++;
    }

    Step(DeltaSeconds, HeightTexture, Settings, Terrain, WindDirection)
    {
        const Gl = this.Device.Gl;
        const Device = this.Device;
        const Source = this.Targets[this.Current];
        const Destination = this.Targets[1 - this.Current];

        const Program = this.Programs.get('FoamStep').Use();
        const Uniforms = Program.Uniforms;

        Uniforms.SetTexture('uFoamField', Source.ColorTexture, 0);
        Uniforms.SetTexture('uHeightSource', HeightTexture, 1);
        Uniforms.SetVec2('uDomain', this.Domain, this.Domain);
        Uniforms.SetFloat('uResolution', this.Resolution);
        Uniforms.SetFloat('uTexelWorld', this.Domain / this.Resolution);
        Uniforms.SetFloat('uDeltaSeconds', DeltaSeconds);
        Uniforms.SetFloat('uDecaySeconds', 5.5);
        Uniforms.SetFloat('uBreakSensitivity', 0.35 + Settings.WaveHeight * 0.25);
        Uniforms.SetFloat('uShoreFoam', 0.55);
        Uniforms.SetFloat('uShoreBand', 3.2);
        Uniforms.SetFloat('uAdvectionScale', 2.2);
        Uniforms.SetVec2('uWindDirection', WindDirection[0], WindDirection[1]);
        Uniforms.SetFloat('uWindSpeed', Settings.WindSpeed);
        Terrain.Bind(Uniforms);

        Gl.disable(Gl.BLEND);
        Destination.Bind();
        Device.DrawFullscreen();

        if (this.SplatCount > 0)
        {
            const Splat = this.Programs.get('FoamSplat').Use();
            Splat.Uniforms.SetVec2('uDomain', this.Domain, this.Domain);

            Gl.bindBuffer(Gl.ARRAY_BUFFER, this.SplatBuffer);
            Gl.bufferSubData(Gl.ARRAY_BUFFER, 0, this.SplatArray, 0, this.SplatCount * 4);

            Gl.enable(Gl.BLEND);
            Gl.blendFunc(Gl.ONE, Gl.ONE);
            Gl.bindVertexArray(this.SplatArrayObject);
            Gl.drawArrays(Gl.POINTS, 0, this.SplatCount);
            Gl.bindVertexArray(null);
            Gl.disable(Gl.BLEND);

            this.SplatCount = 0;
        }

        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        this.Current = 1 - this.Current;
    }
}
