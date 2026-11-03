/*============================================================================================================================================
                                                        GLRESOURCES.JS
============================================================================================================================================*/
// 🧩 WebGL2 device helpers: program bundles, float render targets, grid / sphere geometry and readback

export function CreateWebGL2Context(Canvas)
{
    const Context = Canvas.getContext('webgl2', {
        alpha: false, antialias: true, depth: true, stencil: false,
        preserveDrawingBuffer: false, powerPreference: 'high-performance'
    });
    if (!Context) throw new Error('WebGL2 is unavailable in this browser; the world editor requires float render targets.');
    const FloatColourAttachment = Context.getExtension('EXT_color_buffer_float');
    const HalfFloatColourAttachment = Context.getExtension('EXT_color_buffer_half_float');
    Context.getExtension('OES_texture_float_linear');
    const Renderable = Boolean(FloatColourAttachment || HalfFloatColourAttachment);
    if (!Renderable) throw new Error('Float colour attachments are unavailable; the 2.5D solver cannot run on this GPU.');
    return { Context, FloatColourAttachment: Boolean(FloatColourAttachment), UseHalfFloat: !FloatColourAttachment };
}

export function CreateProgramBundle(Context, VertexSource, FragmentSource, Label)
{
    const Compile = (Kind, Source) =>
    {
        const Shader = Context.createShader(Kind);
        Context.shaderSource(Shader, Source);
        Context.compileShader(Shader);
        if (!Context.getShaderParameter(Shader, Context.COMPILE_STATUS))
            throw new Error(`${Label} shader compilation failed: ${Context.getShaderInfoLog(Shader)}`);
        return Shader;
    };
    const VertexShader = Compile(Context.VERTEX_SHADER, VertexSource);
    const FragmentShader = Compile(Context.FRAGMENT_SHADER, FragmentSource);
    const Program = Context.createProgram();
    Context.attachShader(Program, VertexShader);
    Context.attachShader(Program, FragmentShader);
    Context.linkProgram(Program);
    if (!Context.getProgramParameter(Program, Context.LINK_STATUS))
        throw new Error(`${Label} program link failed: ${Context.getProgramInfoLog(Program)}`);
    Context.deleteShader(VertexShader);
    Context.deleteShader(FragmentShader);
    const LocationCache = new Map();
    const Locate = Name =>
    {
        if (!LocationCache.has(Name)) LocationCache.set(Name, Context.getUniformLocation(Program, Name));
        return LocationCache.get(Name);
    };
    return {
        Program,
        Activate: () => Context.useProgram(Program),
        SetFloat: (Name, ScalarReading) => { const Location = Locate(Name); if (Location !== null) Context.uniform1f(Location, ScalarReading); },
        SetInteger: (Name, ScalarReading) => { const Location = Locate(Name); if (Location !== null) Context.uniform1i(Location, ScalarReading); },
        SetVec2: (Name, X, Y) => { const Location = Locate(Name); if (Location !== null) Context.uniform2f(Location, X, Y); },
        SetIVec2: (Name, X, Y) => { const Location = Locate(Name); if (Location !== null) Context.uniform2i(Location, X, Y); },
        SetVec3: (Name, X, Y, Z) => { const Location = Locate(Name); if (Location !== null) Context.uniform3f(Location, X, Y, Z); },
        SetVec4: (Name, X, Y, Z, W) => { const Location = Locate(Name); if (Location !== null) Context.uniform4f(Location, X, Y, Z, W); }
    };
}

export function CreateElevationTexture(Context, Resolution, Elevations)
{
    const Texture = Context.createTexture();
    Context.bindTexture(Context.TEXTURE_2D, Texture);
    Context.pixelStorei(Context.UNPACK_ALIGNMENT, 1);
    Context.texImage2D(Context.TEXTURE_2D, 0, Context.R32F, Resolution, Resolution, 0, Context.RED, Context.FLOAT, Elevations);
    Context.texParameteri(Context.TEXTURE_2D, Context.TEXTURE_MIN_FILTER, Context.NEAREST);
    Context.texParameteri(Context.TEXTURE_2D, Context.TEXTURE_MAG_FILTER, Context.NEAREST);
    Context.texParameteri(Context.TEXTURE_2D, Context.TEXTURE_WRAP_S, Context.CLAMP_TO_EDGE);
    Context.texParameteri(Context.TEXTURE_2D, Context.TEXTURE_WRAP_T, Context.CLAMP_TO_EDGE);
    return Texture;
}

export function RefreshElevationRegion(Context, Texture, Resolution, Region)
{
    Context.bindTexture(Context.TEXTURE_2D, Texture);
    Context.pixelStorei(Context.UNPACK_ALIGNMENT, 1);
    Context.texSubImage2D(Context.TEXTURE_2D, 0, Region.FirstColumn, Region.FirstRow, Region.Width, Region.Height,
        Context.RED, Context.FLOAT, Region.Samples);
}

export function CreateFloatTarget(Context, Width, Height, UseHalfFloat)
{
    const Texture = Context.createTexture();
    Context.bindTexture(Context.TEXTURE_2D, Texture);
    const InternalFormat = UseHalfFloat ? Context.RGBA16F : Context.RGBA32F;
    const Type = UseHalfFloat ? Context.HALF_FLOAT : Context.FLOAT;
    Context.texImage2D(Context.TEXTURE_2D, 0, InternalFormat, Width, Height, 0, Context.RGBA, Type, null);
    Context.texParameteri(Context.TEXTURE_2D, Context.TEXTURE_MIN_FILTER, Context.NEAREST);
    Context.texParameteri(Context.TEXTURE_2D, Context.TEXTURE_MAG_FILTER, Context.NEAREST);
    Context.texParameteri(Context.TEXTURE_2D, Context.TEXTURE_WRAP_S, Context.CLAMP_TO_EDGE);
    Context.texParameteri(Context.TEXTURE_2D, Context.TEXTURE_WRAP_T, Context.CLAMP_TO_EDGE);
    const Framebuffer = Context.createFramebuffer();
    Context.bindFramebuffer(Context.FRAMEBUFFER, Framebuffer);
    Context.framebufferTexture2D(Context.FRAMEBUFFER, Context.COLOR_ATTACHMENT0, Context.TEXTURE_2D, Texture, 0);
    const Status = Context.checkFramebufferStatus(Context.FRAMEBUFFER);
    Context.bindFramebuffer(Context.FRAMEBUFFER, null);
    if (Status !== Context.FRAMEBUFFER_COMPLETE) throw new Error(`Float render target incomplete (0x${Status.toString(16)})`);
    return { Texture, Framebuffer, Width, Height };
}

export function CreateGridGeometry(Context, Segments)
{
    const VertexCount = (Segments + 1) * (Segments + 1);
    const Positions = new Float32Array(VertexCount * 2);
    let Cursor = 0;
    for (let Row = 0; Row <= Segments; ++Row)
    {
        for (let Column = 0; Column <= Segments; ++Column)
        {
            Positions[Cursor++] = Column / Segments;
            Positions[Cursor++] = Row / Segments;
        }
    }
    const Indices = new Uint32Array(Segments * Segments * 6);
    Cursor = 0;
    for (let Row = 0; Row < Segments; ++Row)
    {
        for (let Column = 0; Column < Segments; ++Column)
        {
            const SouthWest = Row * (Segments + 1) + Column;
            const SouthEast = SouthWest + 1;
            const NorthWest = SouthWest + Segments + 1;
            const NorthEast = NorthWest + 1;
            Indices[Cursor++] = SouthWest; Indices[Cursor++] = NorthWest; Indices[Cursor++] = SouthEast;
            Indices[Cursor++] = SouthEast; Indices[Cursor++] = NorthWest; Indices[Cursor++] = NorthEast;
        }
    }
    const VertexArray = Context.createVertexArray();
    Context.bindVertexArray(VertexArray);
    const PositionBuffer = Context.createBuffer();
    Context.bindBuffer(Context.ARRAY_BUFFER, PositionBuffer);
    Context.bufferData(Context.ARRAY_BUFFER, Positions, Context.STATIC_DRAW);
    Context.enableVertexAttribArray(0);
    Context.vertexAttribPointer(0, 2, Context.FLOAT, false, 0, 0);
    const IndexBuffer = Context.createBuffer();
    Context.bindBuffer(Context.ELEMENT_ARRAY_BUFFER, IndexBuffer);
    Context.bufferData(Context.ELEMENT_ARRAY_BUFFER, Indices, Context.STATIC_DRAW);
    Context.bindVertexArray(null);
    return { VertexArray, IndexCount: Indices.length, VertexCount };
}

export function CreateFullscreenVertexArray(Context)
{
    return Context.createVertexArray();
}

export function CreatePolygonSurfaceGeometry(Context, Surface)
{
    const VertexArray = Context.createVertexArray();
    Context.bindVertexArray(VertexArray);
    const PositionBuffer = Context.createBuffer();
    Context.bindBuffer(Context.ARRAY_BUFFER, PositionBuffer);
    Context.bufferData(Context.ARRAY_BUFFER, Surface.VertexPositions, Context.STATIC_DRAW);
    Context.enableVertexAttribArray(0);
    Context.vertexAttribPointer(0, 3, Context.FLOAT, false, 0, 0);
    const NormalBuffer = Context.createBuffer();
    Context.bindBuffer(Context.ARRAY_BUFFER, NormalBuffer);
    Context.bufferData(Context.ARRAY_BUFFER, Surface.VertexNormals, Context.STATIC_DRAW);
    Context.enableVertexAttribArray(1);
    Context.vertexAttribPointer(1, 3, Context.FLOAT, false, 0, 0);
    const IndexBuffer = Context.createBuffer();
    Context.bindBuffer(Context.ELEMENT_ARRAY_BUFFER, IndexBuffer);
    Context.bufferData(Context.ELEMENT_ARRAY_BUFFER, Surface.TriangleIndices, Context.STATIC_DRAW);
    const LineIndices = new Uint32Array(Surface.TriangleIndices.length * 2);
    let Cursor = 0;
    for (let Offset = 0; Offset < Surface.TriangleIndices.length; Offset += 3)
    {
        const First = Surface.TriangleIndices[Offset];
        const Second = Surface.TriangleIndices[Offset + 1];
        const Third = Surface.TriangleIndices[Offset + 2];
        LineIndices[Cursor++] = First; LineIndices[Cursor++] = Second;
        LineIndices[Cursor++] = Second; LineIndices[Cursor++] = Third;
        LineIndices[Cursor++] = Third; LineIndices[Cursor++] = First;
    }
    const LineBuffer = Context.createBuffer();
    Context.bindBuffer(Context.ELEMENT_ARRAY_BUFFER, LineBuffer);
    Context.bufferData(Context.ELEMENT_ARRAY_BUFFER, LineIndices, Context.STATIC_DRAW);
    Context.bindVertexArray(null);
    return { VertexArray, IndexBuffer, LineBuffer, IndexCount: Surface.TriangleIndices.length, LineIndexCount: LineIndices.length };
}

export function BindLineIndices(Context, Geometry)
{
    Context.bindBuffer(Context.ELEMENT_ARRAY_BUFFER, Geometry.LineBuffer);
}

export function BindTriangleIndices(Context, Geometry)
{
    Context.bindBuffer(Context.ELEMENT_ARRAY_BUFFER, Geometry.IndexBuffer);
}

export function CreateOverlayGeometry(Context)
{
    const VertexArray = Context.createVertexArray();
    Context.bindVertexArray(VertexArray);
    const Buffer = Context.createBuffer();
    Context.bindBuffer(Context.ARRAY_BUFFER, Buffer);
    Context.bufferData(Context.ARRAY_BUFFER, 4096 * 12, Context.DYNAMIC_DRAW);
    Context.enableVertexAttribArray(0);
    Context.vertexAttribPointer(0, 3, Context.FLOAT, false, 0, 0);
    Context.bindVertexArray(null);
    return { VertexArray, Buffer };
}

export function UploadOverlayLines(Context, Geometry, LineArray)
{
    // 📝 ARRAY_BUFFER is global state; rebind explicitly because surface conversion may have replaced it.
    Context.bindBuffer(Context.ARRAY_BUFFER, Geometry.Buffer);
    Context.bufferSubData(Context.ARRAY_BUFFER, 0, LineArray);
}

export function ReadFloatRegion(Context, Framebuffer, X, Y, Width, Height, UseHalfFloat)
{
    const Samples = UseHalfFloat ? new Uint16Array(Width * Height * 4) : new Float32Array(Width * Height * 4);
    Context.bindFramebuffer(Context.FRAMEBUFFER, Framebuffer);
    Context.readPixels(X, Y, Width, Height, Context.RGBA, UseHalfFloat ? Context.HALF_FLOAT : Context.FLOAT, Samples);
    Context.bindFramebuffer(Context.FRAMEBUFFER, null);
    return Samples;
}
