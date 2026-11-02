/*====================================================================================================================================
                                                    POST PROCESS SEQUENCE.JS
====================================================================================================================================*/
// 🧩 Two-tier bloom, ACES tonemap, edge resolve, vignette, and the canvas composite

export class PostProcessSequence
{
    constructor(Device, Programs)
    {
        this.Device = Device;
        this.Programs = Programs;
        this.Width = 1;
        this.Height = 1;
        this.NearTarget = null;
        this.NearScratch = null;
        this.FarTarget = null;
        this.FarScratch = null;
    }

    Resize(Width, Height)
    {
        const NewWidth = Math.max(2, Width | 0);
        const NewHeight = Math.max(2, Height | 0);

        if (NewWidth === this.Width && NewHeight === this.Height && this.NearTarget)
        {
            return;
        }

        this.Width = NewWidth;
        this.Height = NewHeight;

        const NearWidth = Math.max(2, NewWidth >> 1);
        const NearHeight = Math.max(2, NewHeight >> 1);
        const FarWidth = Math.max(2, NewWidth >> 2);
        const FarHeight = Math.max(2, NewHeight >> 2);

        const Descriptor = (Label, TargetWidth, TargetHeight) => ({
            Label:  Label,
            Width:  TargetWidth,
            Height: TargetHeight,
            Format: 'rgba16f',
            Filter: 'linear'
        });

        if (!this.NearTarget)
        {
            this.NearTarget = this.Device.CreateTarget(Descriptor('BloomNear', NearWidth, NearHeight));
            this.NearScratch = this.Device.CreateTarget(Descriptor('BloomNearScratch', NearWidth, NearHeight));
            this.FarTarget = this.Device.CreateTarget(Descriptor('BloomFar', FarWidth, FarHeight));
            this.FarScratch = this.Device.CreateTarget(Descriptor('BloomFarScratch', FarWidth, FarHeight));
            return;
        }

        this.NearTarget.Resize(NearWidth, NearHeight);
        this.NearScratch.Resize(NearWidth, NearHeight);
        this.FarTarget.Resize(FarWidth, FarHeight);
        this.FarScratch.Resize(FarWidth, FarHeight);
    }

    Blur(Source, Destination, OffsetX, OffsetY)
    {
        const Program = this.Programs.get('BloomFilter').Use();
        Program.Uniforms.SetTexture('uSource', Source.ColorTexture, 15);
        Program.Uniforms.SetVec2('uDirection', OffsetX, OffsetY);
        Destination.Bind();
        this.Device.DrawFullscreen();
    }

    Resolve(SceneTarget, Settings)
    {
        const Device = this.Device;
        const Gl = Device.Gl;

        Gl.disable(Gl.DEPTH_TEST);
        Gl.disable(Gl.BLEND);

        if (Settings.BloomEnabled)
        {
            const Prefilter = this.Programs.get('BloomPrefilter').Use();
            Prefilter.Uniforms.SetTexture('uSource', SceneTarget.ColorTexture, 15);
            Prefilter.Uniforms.SetVec2('uTexelSize', 1.0 / SceneTarget.Width, 1.0 / SceneTarget.Height);
            Prefilter.Uniforms.SetFloat('uThreshold', 0.9);
            Prefilter.Uniforms.SetFloat('uKnee', 0.55);

            this.NearTarget.Bind();
            Device.DrawFullscreen();

            this.Blur(this.NearTarget, this.NearScratch, 1.15 / this.NearTarget.Width, 0.0);
            this.Blur(this.NearScratch, this.NearTarget, 0.0, 1.15 / this.NearTarget.Height);

            const Downsample = this.Programs.get('BloomPrefilter').Use();
            Downsample.Uniforms.SetTexture('uSource', this.NearTarget.ColorTexture, 15);
            Downsample.Uniforms.SetVec2('uTexelSize', 1.0 / this.NearTarget.Width, 1.0 / this.NearTarget.Height);
            Downsample.Uniforms.SetFloat('uThreshold', 0.0);
            Downsample.Uniforms.SetFloat('uKnee', 1.0);

            this.FarTarget.Bind();
            Device.DrawFullscreen();

            this.Blur(this.FarTarget, this.FarScratch, 2.1 / this.FarTarget.Width, 0.0);
            this.Blur(this.FarScratch, this.FarTarget, 0.0, 2.1 / this.FarTarget.Height);
        }

        const Composite = this.Programs.get('PostResolve').Use();
        const Uniforms = Composite.Uniforms;

        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        Gl.viewport(0, 0, this.Width, this.Height);

        Uniforms.SetTexture('uSceneColor', SceneTarget.ColorTexture, 10);
        Uniforms.SetTexture('uBloomNear', this.NearTarget.ColorTexture, 13);
        Uniforms.SetTexture('uBloomFar', this.FarTarget.ColorTexture, 14);
        Uniforms.SetVec2('uTexelSize', 1.0 / this.Width, 1.0 / this.Height);
        Uniforms.SetFloat('uExposure', Settings.Exposure);
        Uniforms.SetFloat('uBloomStrength', Settings.BloomEnabled ? 0.55 : 0.0);
        Uniforms.SetFloat('uAntialiasStrength', Settings.Antialias);
        Uniforms.SetFloat('uVignette', 0.55);
        Uniforms.SetInt('uBloomEnabled', Settings.BloomEnabled ? 1 : 0);
        Uniforms.SetInt('uRawView', Settings.FieldView > 0 ? 1 : 0);

        Device.DrawFullscreen();
    }
}
