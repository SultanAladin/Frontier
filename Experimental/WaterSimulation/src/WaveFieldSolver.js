/*====================================================================================================================================
                                                        WAVE FIELD SOLVER.JS
====================================================================================================================================*/
// 🧩 Global height/velocity wave-equation solve with additive impulse splats; used for the sea grid and the ripple grid

export class WaveFieldSolver
{
    constructor(Device, Programs, Descriptor)
    {
        this.Device = Device;
        this.Programs = Programs;
        this.Label = Descriptor.Label;
        this.Domain = Descriptor.Domain ?? 400;
        this.Resolution = Descriptor.Resolution ?? 1024;
        this.CelerityScale = Descriptor.CelerityScale ?? 1.0;
        this.Damping = Descriptor.Damping ?? 0.985;
        this.WindForcing = Descriptor.WindForcing ?? 0.0;
        this.MaxHeightRatio = Descriptor.MaxHeightRatio ?? 0.55;
        this.MaxSplats = Descriptor.MaxSplats ?? 512;
        this.Current = 0;
        this.SplatCount = 0;
        this.Initialized = false;

        const TargetDescriptor =
        {
            Label:  `${this.Label}Target`,
            Width:  this.Resolution,
            Height: this.Resolution,
            Format: 'rgba16f',
            Filter: 'linear'
        };

        this.Targets =
        [
            Device.CreateTarget(TargetDescriptor),
            Device.CreateTarget({ ...TargetDescriptor, Label: `${this.Label}TargetB` })
        ];

        const Gl = Device.Gl;
        this.SplatArray = new Float32Array(this.MaxSplats * 4);
        this.SplatBuffer = Gl.createBuffer();
        Gl.bindBuffer(Gl.ARRAY_BUFFER, this.SplatBuffer);
        Gl.bufferData(Gl.ARRAY_BUFFER, this.SplatArray.byteLength, Gl.DYNAMIC_DRAW);

        this.SplatArrayObject = Gl.createVertexArray();
        Gl.bindVertexArray(this.SplatArrayObject);
        Gl.bindBuffer(Gl.ARRAY_BUFFER, this.SplatBuffer);
        const Location = Gl.getAttribLocation(Programs.get('WaveFieldImpulse').Program, 'a_Splat');
        Gl.enableVertexAttribArray(Location);
        Gl.vertexAttribPointer(Location, 4, Gl.FLOAT, false, 0, 0);
        Gl.bindVertexArray(null);

        this.Clear();
    }

    Clear()
    {
        const Gl = this.Device.Gl;

        for (const Target of this.Targets)
        {
            Target.Bind();
            Gl.clearColor(0, 0, 0, 1);
            Gl.clear(Gl.COLOR_BUFFER_BIT);
        }

        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        this.Current = 0;
        this.Initialized = true;
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

    Step(DeltaSeconds, SimulationTime, WindDirection, Settings, Terrain)
    {
        const Gl = this.Device.Gl;
        const Device = this.Device;
        const Source = this.Targets[this.Current];
        const Destination = this.Targets[1 - this.Current];

        const Program = this.Programs.get('WaveFieldStep').Use();
        const Uniforms = Program.Uniforms;

        Uniforms.SetTexture('uField', Source.ColorTexture, 0);
        Uniforms.SetVec2('uDomain', this.Domain, this.Domain);
        Uniforms.SetFloat('uResolution', this.Resolution);
        Uniforms.SetFloat('uTexelWorld', this.Domain / this.Resolution);
        Uniforms.SetFloat('uDeltaSeconds', DeltaSeconds);
        Uniforms.SetFloat('uCelerityScale', this.CelerityScale);
        Uniforms.SetFloat('uDamping', this.Damping);
        Uniforms.SetFloat('uWindForcing', this.WindForcing * Settings.WindSpeed * 0.05);
        Uniforms.SetFloat('uMaxHeightRatio', this.MaxHeightRatio);
        Uniforms.SetFloat('uSimulationTime', SimulationTime);
        Terrain.Bind(Uniforms);

        Gl.disable(Gl.BLEND);
        Destination.Bind();
        Device.DrawFullscreen();

        if (this.SplatCount > 0)
        {
            const Impulse = this.Programs.get('WaveFieldImpulse').Use();
            Impulse.Uniforms.SetVec2('uDomain', this.Domain, this.Domain);

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
