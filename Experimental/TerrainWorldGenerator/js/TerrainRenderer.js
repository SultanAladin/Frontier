/*====================================================================================================
                                      TERRAINRENDERER.JS
====================================================================================================*/
// 🧩 Terrain viewport rasterization: a flat grid displaced by the SDF height map in the
// vertex shader, with normals and surface colour derived from the height map in the
// fragment shader. Linear depth is sufficient at kilometre scale with a 24-bit depth
// buffer; fog hides the far horizon.

const TerrainGridSize = 513; // [idx] - Vertices per side (512 x 512 quads).
const TerrainSpanM = 8000.0; // [m] - Full grid side length.

const TerrainVertexShader = `#version 300 es
precision highp float;
in vec3 aPosition;      // flat grid in the XZ plane, y = 0
in vec2 aUv;            // 0..1 over the grid
uniform mat4 uViewProjection;
uniform sampler2D uHeightMap;
uniform vec2 uHeightMapSize;
uniform float uHeightScale;
out vec3 vWorldPosition;
out vec2 vUv;
out float vHeight;
void main()
{
    float h = texture(uHeightMap, aUv).r * uHeightScale;
    vec3 p = vec3(aPosition.x, h, aPosition.z);
    vWorldPosition = p;
    vUv = aUv;
    vHeight = h;
    gl_Position = uViewProjection * vec4(p, 1.0);
}`;

const TerrainFragmentShader = `#version 300 es
precision highp float;
in vec3 vWorldPosition;
in vec2 vUv;
in float vHeight;
uniform sampler2D uHeightMap;
uniform vec2 uHeightMapSize;
uniform float uHeightScale;
uniform float uTexelM;
uniform float uSeaLevel;
uniform vec3 uLightDir;
uniform vec3 uLightColour;
uniform vec3 uFogColour;
uniform float uFogNear;
uniform float uFogFar;
uniform vec3 uCameraPosition;
out vec4 oColour;
void main()
{
    vec2 uv = vUv;
    float hc = texture(uHeightMap, uv).r;
    float hr = texture(uHeightMap, uv + vec2(1.0, 0.0) / uHeightMapSize).r;
    float hl = texture(uHeightMap, uv + vec2(-1.0, 0.0) / uHeightMapSize).r;
    float hb = texture(uHeightMap, uv + vec2(0.0, -1.0) / uHeightMapSize).r;
    float ht = texture(uHeightMap, uv + vec2(0.0, 1.0) / uHeightMapSize).r;
    vec3 normal = normalize(vec3(-(hr - hl) * uHeightScale * 0.5 / uTexelM, 1.0, -(ht - hb) * uHeightScale * 0.5 / uTexelM));

    vec3 base;
    if (vHeight < 0.0) base = vec3(0.08, 0.18, 0.30);            // submerged slope
    else if (vHeight < 45.0) base = vec3(0.82, 0.78, 0.60);      // sand
    else if (vHeight < 220.0) base = vec3(0.45, 0.60, 0.35);     // grass
    else if (vHeight < 520.0) base = vec3(0.55, 0.52, 0.48);     // rock
    else base = vec3(0.90, 0.92, 0.95);                          // snow

    vec3 lightDir = normalize(uLightDir);
    float diffuse = max(dot(normal, lightDir), 0.0);
    vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
    vec3 halfDir = normalize(lightDir + viewDir);
    float specular = pow(max(dot(normal, halfDir), 0.0), 48.0) * 0.55;

    float distance = length(vWorldPosition - uCameraPosition);
    float fog = clamp((uFogFar - distance) / (uFogFar - uFogNear), 0.0, 1.0);
    fog = fog * fog;

    vec3 colour = base * (0.25 + 0.75 * diffuse) + specular * uLightColour;
    colour = mix(colour, uFogColour, 1.0 - fog);
    oColour = vec4(colour, 1.0);
}`;

function CreateShader(gl, Type, Source)
{
    const Shader = gl.createShader(Type);
    gl.shaderSource(Shader, Source);
    gl.compileShader(Shader);
    if (!gl.getShaderParameter(Shader, gl.COMPILE_STATUS))
    {
        throw new Error('Shader compile failed: ' + gl.getShaderInfoLog(Shader));
    }
    return Shader;
}

function CreateProgram(gl, VertexSource, FragmentSource)
{
    const Program = gl.createProgram();
    gl.attachShader(Program, CreateShader(gl, gl.VERTEX_SHADER, VertexSource));
    gl.attachShader(Program, CreateShader(gl, gl.FRAGMENT_SHADER, FragmentSource));
    gl.linkProgram(Program);
    if (!gl.getProgramParameter(Program, gl.LINK_STATUS))
    {
        throw new Error('Program link failed: ' + gl.getProgramInfoLog(Program));
    }
    return Program;
}

class TerrainRenderer
{
    constructor(gl)
    {
        this.gl = gl;
        this.Program = CreateProgram(gl, TerrainVertexShader, TerrainFragmentShader);
        this.Locations = {
            ViewProjection: gl.getUniformLocation(this.Program, 'uViewProjection'),
            HeightMap: gl.getUniformLocation(this.Program, 'uHeightMap'),
            HeightMapSize: gl.getUniformLocation(this.Program, 'uHeightMapSize'),
            HeightScale: gl.getUniformLocation(this.Program, 'uHeightScale'),
            TexelM: gl.getUniformLocation(this.Program, 'uTexelM'),
            SeaLevel: gl.getUniformLocation(this.Program, 'uSeaLevel'),
            LightDir: gl.getUniformLocation(this.Program, 'uLightDir'),
            LightColour: gl.getUniformLocation(this.Program, 'uLightColour'),
            FogColour: gl.getUniformLocation(this.Program, 'uFogColour'),
            FogNear: gl.getUniformLocation(this.Program, 'uFogNear'),
            FogFar: gl.getUniformLocation(this.Program, 'uFogFar'),
            CameraPosition: gl.getUniformLocation(this.Program, 'uCameraPosition'),
            Position: gl.getAttribLocation(this.Program, 'aPosition'),
            Uv: gl.getAttribLocation(this.Program, 'aUv')
        };

        const Half = TerrainSpanM / 2.0;
        const Positions = new Float32Array(TerrainGridSize * TerrainGridSize * 3);
        const Uvs = new Float32Array(TerrainGridSize * TerrainGridSize * 2);
        const Indices = [];
        let Vertex = 0;
        for (let Ty = 0; Ty < TerrainGridSize; Ty++)
        {
            const Z = -Half + (Ty / (TerrainGridSize - 1)) * TerrainSpanM;
            for (let Tx = 0; Tx < TerrainGridSize; Tx++)
            {
                const X = -Half + (Tx / (TerrainGridSize - 1)) * TerrainSpanM;
                Positions[Vertex * 3] = X;
                Positions[Vertex * 3 + 1] = 0.0;
                Positions[Vertex * 3 + 2] = Z;
                Uvs[Vertex * 2] = Tx / (TerrainGridSize - 1);
                Uvs[Vertex * 2 + 1] = Ty / (TerrainGridSize - 1);
                Vertex++;
            }
        }
        for (let Ty = 0; Ty < TerrainGridSize - 1; Ty++)
        {
            for (let Tx = 0; Tx < TerrainGridSize - 1; Tx++)
            {
                const A = Ty * TerrainGridSize + Tx;
                const B = A + 1;
                const C = A + TerrainGridSize;
                const D = C + 1;
                Indices.push(A, B, C, B, D, C);
            }
        }
        this.IndexCount = Indices.length;
        this.IndexBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.IndexBuffer);
        gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint32Array(Indices), gl.STATIC_DRAW);

        this.VertexBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.VertexBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, Positions, gl.STATIC_DRAW);

        this.UvBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.UvBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, Uvs, gl.STATIC_DRAW);

        // [R32F] - One red channel holds elevation in metres; nearest filtering keeps
        // crisp cliffs without interpolation artefacts.
        this.HeightMapTexture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.HeightMapTexture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, 1, 1, 0, gl.RED, gl.FLOAT, null);
        gl.bindTexture(gl.TEXTURE_2D, null);
    }

    // Upload the edited height map (Float32Array, length Size²) as the displacement texture.
    SetHeightMap(Data, Width)
    {
        const gl = this.gl;
        gl.bindTexture(gl.TEXTURE_2D, this.HeightMapTexture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, Width, Width, 0, gl.RED, gl.FLOAT, Data);
        gl.bindTexture(gl.TEXTURE_2D, null);
    }

    GetHeightMapTexture()
    {
        return this.HeightMapTexture;
    }

    Draw(ViewProjection, CameraPosition, LightDir, LightColour, FogColour, FogNear, FogFar, SeaLevel, HeightMapSizeM, TexelM, HeightScale)
    {
        const gl = this.gl;
        gl.useProgram(this.Program);
        gl.uniformMatrix4fv(this.Locations.ViewProjection, false, ViewProjection);
        gl.uniform3fv(this.Locations.CameraPosition, CameraPosition);
        gl.uniform3fv(this.Locations.LightDir, LightDir);
        gl.uniform3fv(this.Locations.LightColour, LightColour);
        gl.uniform3fv(this.Locations.FogColour, FogColour);
        gl.uniform1f(this.Locations.FogNear, FogNear);
        gl.uniform1f(this.Locations.FogFar, FogFar);
        gl.uniform1f(this.Locations.SeaLevel, SeaLevel);
        gl.uniform1f(this.Locations.HeightMapSize, HeightMapSizeM);
        gl.uniform1f(this.Locations.TexelM, TexelM);
        gl.uniform1f(this.Locations.HeightScale, HeightScale);

        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.HeightMapTexture);
        gl.uniform1i(this.Locations.HeightMap, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.VertexBuffer);
        gl.enableVertexAttribArray(this.Locations.Position);
        gl.vertexAttribPointer(this.Locations.Position, 3, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ARRAY_BUFFER, this.UvBuffer);
        gl.enableVertexAttribArray(this.Locations.Uv);
        gl.vertexAttribPointer(this.Locations.Uv, 2, gl.FLOAT, false, 0, 0);

        gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, this.IndexBuffer);
        gl.drawElements(gl.TRIANGLES, this.IndexCount, gl.UNSIGNED_INT, 0);
    }
}

export { TerrainRenderer, TerrainGridSize, TerrainSpanM };
