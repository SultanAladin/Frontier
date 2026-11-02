/*====================================================================================================================================
                                                    GRAPHICS DEVICE.JS
====================================================================================================================================*/
// 🧩 WebGL2 provisioning, include-resolved shader assembly, render targets, and transform-feedback programs

const TextureFormatTable =
{
    rgba16f: { Internal: 'RGBA16F', Type: 'HALF_FLOAT', Channels: 4 },
    rgba32f: { Internal: 'RGBA32F', Type: 'FLOAT',      Channels: 4 },
    rg16f:   { Internal: 'RG16F',   Type: 'HALF_FLOAT', Channels: 2 },
    r16f:    { Internal: 'R16F',    Type: 'HALF_FLOAT', Channels: 1 },
    rgba8:   { Internal: 'RGBA8',   Type: 'UNSIGNED_BYTE', Channels: 4 }
};

export class ShaderAssemblyError extends Error
{
    constructor(Label, Detail)
    {
        super(`${Label}: ${Detail}`);
        this.Label  = Label;
        this.Detail = Detail;
    }
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    UNIFORM SETTING
------------------------------------------------------------------------------------------------------------------------------------*/

export class ProgramUniforms
{
    constructor(Gl, Program, Label)
    {
        this.Gl        = Gl;
        this.Program   = Program;
        this.Label     = Label;
        this.Locations = new Map();
        this.TextureCursor = 0;
    }

    Locate(Name)
    {
        if (this.Locations.has(Name))
        {
            return this.Locations.get(Name);
        }

        const Location = this.Gl.getUniformLocation(this.Program, Name);
        this.Locations.set(Name, Location);
        return Location;
    }

    SetFloat(Name, Scalar)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniform1f(Location, Scalar);
        return this;
    }

    SetInt(Name, Integer)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniform1i(Location, Integer);
        return this;
    }

    SetVec2(Name, X, Y)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniform2f(Location, X, Y);
        return this;
    }

    SetVec3(Name, X, Y, Z)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniform3f(Location, X, Y, Z);
        return this;
    }

    SetVec4(Name, X, Y, Z, W)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniform4f(Location, X, Y, Z, W);
        return this;
    }

    SetArray4(Name, Values)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniform4fv(Location, Values);
        return this;
    }

    SetArray3(Name, Values)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniform3fv(Location, Values);
        return this;
    }

    SetArray1(Name, Values)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniform1fv(Location, Values);
        return this;
    }

    SetMatrix4(Name, Values)
    {
        const Location = this.Locate(Name);
        if (Location) this.Gl.uniformMatrix4fv(Location, false, Values);
        return this;
    }

    SetTexture(Name, Texture, Unit)
    {
        const Location = this.Locate(Name);
        if (!Location) return this;

        const Target = typeof Unit === 'number' ? Unit : this.TextureCursor++;
        this.Gl.activeTexture(this.Gl.TEXTURE0 + Target);
        this.Gl.bindTexture(this.Gl.TEXTURE_2D, Texture);
        this.Gl.uniform1i(Location, Target);
        return this;
    }

    ResetTextureCursor()
    {
        this.TextureCursor = 0;
        return this;
    }
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    COMPILED PROGRAM
------------------------------------------------------------------------------------------------------------------------------------*/

export class CompiledProgram
{
    constructor(Device, Descriptor)
    {
        this.Device   = Device;
        this.Gl       = Device.Gl;
        this.Label    = Descriptor.Label;
        this.VaryingNames = Descriptor.TransformVaryings ?? null;

        // 🔴 WebGL2 refuses to link a program that lacks either stage, so a missing fragment is rejected here
        if (!Descriptor.Fragment)
        {
            throw new ShaderAssemblyError(Descriptor.Label,
                'a fragment shader is required: WebGL2 cannot link a vertex-only program');
        }

        const VertexShader   = CompileShader(this.Gl, this.Gl.VERTEX_SHADER, Descriptor.Vertex, `${Descriptor.Label} vertex`);
        const FragmentShader = Descriptor.Fragment
            ? CompileShader(this.Gl, this.Gl.FRAGMENT_SHADER, Descriptor.Fragment, `${Descriptor.Label} fragment`)
            : null;

        const Program = this.Gl.createProgram();
        this.Gl.attachShader(Program, VertexShader);
        if (FragmentShader) this.Gl.attachShader(Program, FragmentShader);

        if (this.VaryingNames)
        {
            this.Gl.transformFeedbackVaryings(Program, this.VaryingNames, this.Gl.SEPARATE_ATTRIBS);
        }

        this.Gl.linkProgram(Program);

        if (!this.Gl.getProgramParameter(Program, this.Gl.LINK_STATUS))
        {
            const Log = this.Gl.getProgramInfoLog(Program) ?? 'unknown link failure';
            throw new ShaderAssemblyError(Descriptor.Label, `link failed\n${Log}`);
        }

        this.Gl.deleteShader(VertexShader);
        if (FragmentShader) this.Gl.deleteShader(FragmentShader);

        this.Program  = Program;
        this.Uniforms = new ProgramUniforms(this.Gl, Program, Descriptor.Label);
    }

    Use()
    {
        this.Gl.useProgram(this.Program);
        this.Uniforms.ResetTextureCursor();
        return this;
    }
}

function CompileShader(Gl, Category, Source, Label)
{
    const Shader = Gl.createShader(Category);
    Gl.shaderSource(Shader, Source);
    Gl.compileShader(Shader);

    if (!Gl.getShaderParameter(Shader, Gl.COMPILE_STATUS))
    {
        const Log = Gl.getShaderInfoLog(Shader) ?? 'unknown compile failure';
        Gl.deleteShader(Shader);
        throw new ShaderAssemblyError(Label, `compile failed\n${AnnotateShaderLog(Source, Log)}`);
    }

    return Shader;
}

export function AnnotateShaderLog(Source, Log)
{
    const Lines   = Source.split('\n');
    const Header  = Log.split('\n').map((Entry) =>
    {
        const Match = /:(\d+):/.exec(Entry);
        if (!Match) return Entry;

        const Index = Number.parseInt(Match[1], 10) - 1;
        const Body  = Lines[Index] ? `    ${Index + 1} | ${Lines[Index].trim()}` : '';
        return Body ? `${Entry}\n${Body}` : Entry;
    });

    return Header.join('\n');
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    RENDER TARGET
------------------------------------------------------------------------------------------------------------------------------------*/

export class RenderTarget
{
    constructor(Device, Descriptor)
    {
        this.Device      = Device;
        this.Gl          = Device.Gl;
        this.Label       = Descriptor.Label ?? 'Target';
        this.Width       = Math.max(1, Descriptor.Width | 0);
        this.Height      = Math.max(1, Descriptor.Height | 0);
        this.Format      = Descriptor.Format ?? 'rgba16f';
        this.Filter      = Descriptor.Filter ?? 'linear';
        this.DepthMode   = Descriptor.Depth ?? null;
        this.WrapMode    = Descriptor.Wrap ?? 'clamp';
        this.MipMode     = Descriptor.Mip ?? false;

        this.ColorTexture = null;
        this.DepthTexture = null;
        this.DepthBuffer  = null;
        this.Framebuffer  = null;

        this.Allocate();
    }

    Allocate()
    {
        const Gl = this.Gl;
        const FormatEntry = TextureFormatTable[this.Format];

        if (!FormatEntry)
        {
            throw new ShaderAssemblyError(this.Label, `unsupported format ${this.Format}`);
        }

        this.ColorTexture = Gl.createTexture();
        Gl.bindTexture(Gl.TEXTURE_2D, this.ColorTexture);
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl[FormatEntry.Internal], this.Width, this.Height, 0,
                      Gl.RGBA, Gl[FormatEntry.Type], null);

        const FilterMode = this.Filter === 'nearest' ? Gl.NEAREST : Gl.LINEAR;
        const WrapMode   = this.WrapMode === 'repeat' ? Gl.REPEAT : Gl.CLAMP_TO_EDGE;

        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, this.MipMode ? Gl.LINEAR_MIPMAP_LINEAR : FilterMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, FilterMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, WrapMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, WrapMode);

        if (this.MipMode)
        {
            Gl.generateMipmap(Gl.TEXTURE_2D);
        }

        this.Framebuffer = Gl.createFramebuffer();
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, this.Framebuffer);
        Gl.framebufferTexture2D(Gl.FRAMEBUFFER, Gl.COLOR_ATTACHMENT0, Gl.TEXTURE_2D, this.ColorTexture, 0);

        if (this.DepthMode === 'texture')
        {
            this.DepthTexture = Gl.createTexture();
            Gl.bindTexture(Gl.TEXTURE_2D, this.DepthTexture);
            Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.DEPTH_COMPONENT24, this.Width, this.Height, 0,
                          Gl.DEPTH_COMPONENT, Gl.UNSIGNED_INT, null);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, Gl.NEAREST);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, Gl.NEAREST);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Gl.CLAMP_TO_EDGE);
            Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Gl.CLAMP_TO_EDGE);
            Gl.framebufferTexture2D(Gl.FRAMEBUFFER, Gl.DEPTH_ATTACHMENT, Gl.TEXTURE_2D, this.DepthTexture, 0);
        }
        else if (this.DepthMode === 'renderbuffer')
        {
            this.DepthBuffer = Gl.createRenderbuffer();
            Gl.bindRenderbuffer(Gl.RENDERBUFFER, this.DepthBuffer);
            Gl.renderbufferStorage(Gl.RENDERBUFFER, Gl.DEPTH_COMPONENT24, this.Width, this.Height);
            Gl.framebufferRenderbuffer(Gl.FRAMEBUFFER, Gl.DEPTH_ATTACHMENT, Gl.RENDERBUFFER, this.DepthBuffer);
        }

        const Status = Gl.checkFramebufferStatus(Gl.FRAMEBUFFER);
        if (Status !== Gl.FRAMEBUFFER_COMPLETE)
        {
            throw new ShaderAssemblyError(this.Label, `framebuffer incomplete (0x${Status.toString(16)})`);
        }

        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
        Gl.bindTexture(Gl.TEXTURE_2D, null);
    }

    Resize(Width, Height)
    {
        const NewWidth  = Math.max(1, Width | 0);
        const NewHeight = Math.max(1, Height | 0);

        if (NewWidth === this.Width && NewHeight === this.Height)
        {
            return false;
        }

        this.Dispose(false);
        this.Width  = NewWidth;
        this.Height = NewHeight;
        this.Allocate();
        return true;
    }

    Bind()
    {
        const Gl = this.Gl;
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, this.Framebuffer);
        Gl.viewport(0, 0, this.Width, this.Height);
        return this;
    }

    Dispose(ReleaseFramebuffer = true)
    {
        const Gl = this.Gl;
        if (this.ColorTexture) Gl.deleteTexture(this.ColorTexture);
        if (this.DepthTexture) Gl.deleteTexture(this.DepthTexture);
        if (this.DepthBuffer) Gl.deleteRenderbuffer(this.DepthBuffer);
        if (ReleaseFramebuffer && this.Framebuffer) Gl.deleteFramebuffer(this.Framebuffer);

        this.ColorTexture = null;
        this.DepthTexture = null;
        this.DepthBuffer  = null;
        if (ReleaseFramebuffer) this.Framebuffer = null;
    }
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    MULTI-TARGET PAIR
------------------------------------------------------------------------------------------------------------------------------------*/

// 💡 The FFT butterfly writes three complex fields, so both attachments share one draw
export class RenderPair
{
    constructor(Device, Descriptor)
    {
        this.Device = Device;
        this.Gl     = Device.Gl;
        this.Label  = Descriptor.Label ?? 'Pair';
        this.Width  = Math.max(1, Descriptor.Width | 0);
        this.Height = Math.max(1, Descriptor.Height | 0);
        this.Filter = Descriptor.Filter ?? 'nearest';
        this.WrapMode = Descriptor.Wrap ?? 'clamp';
        this.Framebuffer = this.Gl.createFramebuffer();

        this.Textures = [this.CreateAttachment(), this.CreateAttachment()];
        this.Allocate();
    }

    CreateAttachment()
    {
        const Gl = this.Gl;
        const Texture = Gl.createTexture();
        const FilterMode = this.Filter === 'nearest' ? Gl.NEAREST : Gl.LINEAR;

        Gl.bindTexture(Gl.TEXTURE_2D, Texture);
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl.RGBA32F, this.Width, this.Height, 0, Gl.RGBA, Gl.FLOAT, null);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, FilterMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, FilterMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, this.WrapMode === 'repeat' ? Gl.REPEAT : Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, this.WrapMode === 'repeat' ? Gl.REPEAT : Gl.CLAMP_TO_EDGE);
        Gl.bindTexture(Gl.TEXTURE_2D, null);

        return Texture;
    }

    Allocate()
    {
        const Gl = this.Gl;

        Gl.bindFramebuffer(Gl.FRAMEBUFFER, this.Framebuffer);
        Gl.framebufferTexture2D(Gl.FRAMEBUFFER, Gl.COLOR_ATTACHMENT0, Gl.TEXTURE_2D, this.Textures[0], 0);
        Gl.framebufferTexture2D(Gl.FRAMEBUFFER, Gl.COLOR_ATTACHMENT1, Gl.TEXTURE_2D, this.Textures[1], 0);
        Gl.drawBuffers([Gl.COLOR_ATTACHMENT0, Gl.COLOR_ATTACHMENT1]);

        const Status = Gl.checkFramebufferStatus(Gl.FRAMEBUFFER);
        if (Status !== Gl.FRAMEBUFFER_COMPLETE)
        {
            throw new ShaderAssemblyError(this.Label, `multi-target incomplete (0x${Status.toString(16)})`);
        }

        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    }

    Bind()
    {
        const Gl = this.Gl;
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, this.Framebuffer);
        Gl.drawBuffers([Gl.COLOR_ATTACHMENT0, Gl.COLOR_ATTACHMENT1]);
        Gl.viewport(0, 0, this.Width, this.Height);
        return this;
    }
}

/*------------------------------------------------------------------------------------------------------------------------------------
                                                    GRAPHICS DEVICE
------------------------------------------------------------------------------------------------------------------------------------*/

export class GraphicsDevice
{
    constructor(CanvasElement)
    {
        this.Canvas = CanvasElement;

        const Attributes =
        {
            Alpha:                false,
            Antialias:            false,
            Depth:                true,
            Stencil:              false,
            PremultipliedAlpha:   false,
            PreserveDrawingBuffer: true,
            PowerPreference:      'high-performance'
        };

        const Gl = CanvasElement.getContext('webgl2', {
            alpha:                 Attributes.Alpha,
            antialias:             Attributes.Antialias,
            depth:                 Attributes.Depth,
            stencil:               Attributes.Stencil,
            premultipliedAlpha:    Attributes.PremultipliedAlpha,
            preserveDrawingBuffer: Attributes.PreserveDrawingBuffer,
            powerPreference:       Attributes.PowerPreference
        });

        if (!Gl)
        {
            throw new ShaderAssemblyError('GraphicsDevice', 'WebGL2 is unavailable in this browser');
        }

        this.Gl              = Gl;
        this.Chunks          = new Map();
        this.ProgramCache    = new Map();
        this.EmptyVertexArray = Gl.createVertexArray();
        this.FloatRenderable = !!Gl.getExtension('EXT_color_buffer_float');
        this.FloatFilterable = !!Gl.getExtension('OES_texture_float_linear');
        this.Anisotropy      = Gl.getExtension('EXT_texture_filter_anisotropic');
        this.MaxAnisotropy   = this.Anisotropy
            ? Gl.getParameter(this.Anisotropy.MAX_TEXTURE_MAX_ANISOTROPY_EXT)
            : 1;

        if (!this.FloatRenderable)
        {
            throw new ShaderAssemblyError('GraphicsDevice', 'EXT_color_buffer_float is required for the simulation fields');
        }

        Gl.disable(Gl.CULL_FACE);
        Gl.disable(Gl.BLEND);
        Gl.clearColor(0, 0, 0, 1);
    }

    RegisterChunk(Name, Source)
    {
        this.Chunks.set(Name, Source);
    }

    ResolveIncludes(Source, Label, DepthTier = 0, Included = new Set())
    {
        if (DepthTier > 12)
        {
            throw new ShaderAssemblyError(Label, 'include nesting exceeds 12 tiers');
        }

        return Source.replace(/^[ \t]*#include[ \t]+"([^"]+)"[ \t]*$/gm, (Match, ChunkName) =>
        {
            const ChunkSource = this.Chunks.get(ChunkName);
            if (ChunkSource === undefined)
            {
                throw new ShaderAssemblyError(Label, `missing shader chunk "${ChunkName}"`);
            }

            // 📝 A chunk pulled in twice would redeclare uniforms and functions, so repeats collapse to nothing
            if (Included.has(ChunkName))
            {
                return '';
            }

            Included.add(ChunkName);
            return this.ResolveIncludes(ChunkSource, Label, DepthTier + 1, Included);
        });
    }

    Compile(Descriptor)
    {
        const Assembled =
        {
            Label:             Descriptor.Label,
            Vertex:            this.ResolveIncludes(Descriptor.Vertex, `${Descriptor.Label} vertex`),
            Fragment:          Descriptor.Fragment
                ? this.ResolveIncludes(Descriptor.Fragment, `${Descriptor.Label} fragment`)
                : null,
            TransformVaryings: Descriptor.TransformVaryings ?? null
        };

        const Program = new CompiledProgram(this, Assembled);
        this.ProgramCache.set(Descriptor.Label, Program);
        return Program;
    }

    Acquire(Label)
    {
        const Program = this.ProgramCache.get(Label);
        if (!Program)
        {
            throw new ShaderAssemblyError(Label, 'program was never compiled');
        }

        return Program;
    }

    CreateTarget(Descriptor)
    {
        return new RenderTarget(this, Descriptor);
    }

    CreateTexture(Width, Height, Format = 'rgba16f', Filter = 'linear', Wrap = 'clamp')
    {
        const Gl = this.Gl;
        const FormatEntry = TextureFormatTable[Format];
        const Texture = Gl.createTexture();

        Gl.bindTexture(Gl.TEXTURE_2D, Texture);
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl[FormatEntry.Internal], Width, Height, 0, Gl.RGBA, Gl[FormatEntry.Type], null);

        const FilterMode = Filter === 'nearest' ? Gl.NEAREST : Gl.LINEAR;
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, FilterMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, FilterMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Wrap === 'repeat' ? Gl.REPEAT : Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Wrap === 'repeat' ? Gl.REPEAT : Gl.CLAMP_TO_EDGE);
        Gl.bindTexture(Gl.TEXTURE_2D, null);

        return Texture;
    }

    CreateTextureFromData(Width, Height, PixelArray, Format = 'rgba32f', Filter = 'linear', Wrap = 'clamp')
    {
        const Gl = this.Gl;
        const FormatEntry = TextureFormatTable[Format];
        const Texture = Gl.createTexture();

        Gl.bindTexture(Gl.TEXTURE_2D, Texture);
        Gl.texImage2D(Gl.TEXTURE_2D, 0, Gl[FormatEntry.Internal], Width, Height, 0, Gl.RGBA, Gl[FormatEntry.Type], PixelArray);

        const FilterMode = Filter === 'nearest' ? Gl.NEAREST : Gl.LINEAR;
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MIN_FILTER, FilterMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_MAG_FILTER, FilterMode);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_S, Wrap === 'repeat' ? Gl.REPEAT : Gl.CLAMP_TO_EDGE);
        Gl.texParameteri(Gl.TEXTURE_2D, Gl.TEXTURE_WRAP_T, Wrap === 'repeat' ? Gl.REPEAT : Gl.CLAMP_TO_EDGE);
        Gl.bindTexture(Gl.TEXTURE_2D, null);

        return Texture;
    }

    DrawFullscreen()
    {
        const Gl = this.Gl;
        Gl.bindVertexArray(this.EmptyVertexArray);
        Gl.drawArrays(Gl.TRIANGLES, 0, 3);
    }

    BlitColor(Source, Destination)
    {
        const Gl = this.Gl;

        Gl.bindFramebuffer(Gl.READ_FRAMEBUFFER, Source.Framebuffer);
        Gl.bindFramebuffer(Gl.DRAW_FRAMEBUFFER, Destination.Framebuffer);
        Gl.blitFramebuffer(0, 0, Source.Width, Source.Height,
                           0, 0, Destination.Width, Destination.Height,
                           Gl.COLOR_BUFFER_BIT, Gl.NEAREST);
        Gl.bindFramebuffer(Gl.READ_FRAMEBUFFER, null);
        Gl.bindFramebuffer(Gl.DRAW_FRAMEBUFFER, null);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    }

    BlitDepth(Source, Destination)
    {
        const Gl = this.Gl;

        Gl.bindFramebuffer(Gl.READ_FRAMEBUFFER, Source.Framebuffer);
        Gl.bindFramebuffer(Gl.DRAW_FRAMEBUFFER, Destination.Framebuffer);
        Gl.blitFramebuffer(0, 0, Source.Width, Source.Height,
                           0, 0, Destination.Width, Destination.Height,
                           Gl.DEPTH_BUFFER_BIT, Gl.NEAREST);
        Gl.bindFramebuffer(Gl.READ_FRAMEBUFFER, null);
        Gl.bindFramebuffer(Gl.DRAW_FRAMEBUFFER, null);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    }

    ReadFloatPixel(Target, X, Y)
    {
        const Gl = this.Gl;
        const Buffer = new Float32Array(4);

        Target.Bind();
        Gl.readPixels(X, Y, 1, 1, Gl.RGBA, Gl.FLOAT, Buffer);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);

        return Buffer;
    }

    ReadFloatRegion(Target, X, Y, Width, Height, Destination)
    {
        const Gl = this.Gl;

        Target.Bind();
        Gl.readPixels(X, Y, Width, Height, Gl.RGBA, Gl.FLOAT, Destination);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);

        return Destination;
    }
}
