/*====================================================================================================================================
                                                    WATER HEIGHT ATLAS.JS
====================================================================================================================================*/
// 🧩 World-space water surface atlas: feeds spray collision, vessel buoyancy readback, and grid inspection

export class WaterHeightAtlas
{
    constructor(Device, Programs, Descriptor = {})
    {
        this.Device = Device;
        this.Programs = Programs;
        this.Domain = Descriptor.Domain ?? 400;
        this.Resolution = Descriptor.Resolution ?? 256;
        this.WindowSize = Descriptor.WindowSize ?? 28;
        this.WindowArray = new Float32Array(this.WindowSize * this.WindowSize * 4);
        this.WindowMinimum = [0, 0];
        this.WindowValid = false;

        this.Target = Device.CreateTarget({
            Label:  'WaterHeightAtlas',
            Width:  this.Resolution,
            Height: this.Resolution,
            Format: 'rgba32f',
            Filter: Device.FloatFilterable ? 'linear' : 'nearest'
        });
    }

    get Texture()
    {
        return this.Target.ColorTexture;
    }

    Update(Binder, Settings, SimulationTime, Terrain)
    {
        const Program = this.Programs.get('HeightAtlas').Use();
        const Uniforms = Program.Uniforms;

        Binder.Apply(Uniforms, Settings, SimulationTime);
        Terrain.Bind(Uniforms);
        Uniforms.SetFloat('uAtlasDomain', this.Domain);
        Uniforms.SetFloat('uAtlasResolution', this.Resolution);

        this.Device.Gl.disable(this.Device.Gl.BLEND);
        this.Target.Bind();
        this.Device.DrawFullscreen();
        this.Device.Gl.bindFramebuffer(this.Device.Gl.FRAMEBUFFER, null);
    }

    // 📝 A small window readback keeps buoyancy physical without stalling the pipeline on a full atlas read
    RefreshWindow(CenterX, CenterZ)
    {
        const Gl = this.Device.Gl;
        const Size = this.Resolution;
        const Window = this.WindowSize;
        const TexelX = Math.round((CenterX / this.Domain + 0.5) * Size) - Math.floor(Window * 0.5);
        const TexelZ = Math.round((CenterZ / this.Domain + 0.5) * Size) - Math.floor(Window * 0.5);
        const ClampedX = Math.min(Math.max(TexelX, 0), Size - Window);
        const ClampedZ = Math.min(Math.max(TexelZ, 0), Size - Window);

        this.WindowMinimum = [ClampedX, ClampedZ];
        this.Target.Bind();
        Gl.readPixels(ClampedX, ClampedZ, Window, Window, Gl.RGBA, Gl.FLOAT, this.WindowArray);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        this.WindowValid = true;
    }

    SampleHeight(WorldX, WorldZ)
    {
        if (!this.WindowValid)
        {
            return 0.0;
        }

        const TexelX = (WorldX / this.Domain + 0.5) * this.Resolution - 0.5;
        const TexelZ = (WorldZ / this.Domain + 0.5) * this.Resolution - 0.5;
        const LocalX = TexelX - this.WindowMinimum[0];
        const LocalZ = TexelZ - this.WindowMinimum[1];
        const Window = this.WindowSize;

        if (LocalX < 0.0 || LocalZ < 0.0 || LocalX >= Window - 1 || LocalZ >= Window - 1)
        {
            return 0.0;
        }

        const LowerX = Math.floor(LocalX);
        const LowerZ = Math.floor(LocalZ);
        const FractionX = LocalX - LowerX;
        const FractionZ = LocalZ - LowerZ;
        const Read = (X, Z) => this.WindowArray[(Z * Window + X) * 4];

        const Corner00 = Read(LowerX, LowerZ);
        const Corner10 = Read(LowerX + 1, LowerZ);
        const Corner01 = Read(LowerX, LowerZ + 1);
        const Corner11 = Read(LowerX + 1, LowerZ + 1);

        return (Corner00 * (1.0 - FractionX) + Corner10 * FractionX) * (1.0 - FractionZ)
             + (Corner01 * (1.0 - FractionX) + Corner11 * FractionX) * FractionZ;
    }
}
