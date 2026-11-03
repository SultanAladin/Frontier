/*====================================================================================================
                                      OCEANRENDERER.JS
====================================================================================================*/
// 🧩 Ocean viewport rasterization: adaptive-tessellation water. The sea is drawn as
// concentric rings around the camera. The near ring runs the 2.5D simulation (real
// height-field waves, not a texture); mid and far rings are coarser tessellated mesh
// whose displacement tapers to flat at the horizon. Tessellation (and simulation cost)
// concentrates where the player is — "nanite for water".

const SimTextureSize = 256; // [idx] - Water simulation displacement texture (square).

// Geometry maxima (metres). Settings clamp the active annulus inside these bounds.
const RingNearMaxM = 1500.0;
const RingMidMaxM = 3500.0;
const RingFarMaxM = 6000.0;

const RingNear = { InnerM: 0.0, OuterM: RingNearMaxM, Grid: 129, Colour: [0.2, 0.9, 0.3] };
const RingMid = { InnerM: 0.0, OuterM: RingMidMaxM, Grid: 65, Colour: [0.9, 0.8, 0.2] };
const RingFar = { InnerM: 0.0, OuterM: RingFarMaxM, Grid: 33, Colour: [0.9, 0.3, 0.3] };

const WaterVertexShader = `#version 300 es
precision highp float;
in vec3 aPosition;   // local ring grid, y = 0
in vec2 aUv;         // 0..1 over the ring
uniform mat4 uViewProjection;
uniform mat4 uModel; // translation + uniform scale around the ring centre
uniform sampler2D uSimHeight;   // 2.5D simulation displacement (metres, R32F)
uniform vec2 uSimSize;          // cells
uniform float uCellM;           // [m] simulation cell spacing
uniform float uWorldHalfSideM;  // [m]
uniform float uSeaLevelM;
uniform float uDisplaceScale;
uniform float uTaperStartM;
uniform float uRingOuterM;
out vec3 vWorldPosition;
out vec2 vUv;
out float vSimHeight;
void main()
{
    vec3 p = (uModel * vec4(aPosition, 1.0)).xyz;
    vec2 uv = (p.xz + uWorldHalfSideM) / (2.0 * uWorldHalfSideM);
    float sim = texture(uSimHeight, uv).r;
    float dist = length(p.xz - (uModel * vec4(0.0, 0.0, 0.0, 1.0)).xyz);
    float taper = 1.0;
    if (uTaperStartM > 0.0)
    {
        taper = 1.0 - smoothstep(0.0, 1.0, (dist - uTaperStartM) / max(0.001, uRingOuterM - uTaperStartM));
    }
    p.y = uSeaLevelM + sim * uDisplaceScale * taper;
    vWorldPosition = p;
    vUv = uv;
    vSimHeight = sim;
    gl_Position = uViewProjection * vec4(p, 1.0);
}`;

const WaterFragmentShader = `#version 300 es
precision highp float;
in vec3 vWorldPosition;
in vec2 vUv;
in float vSimHeight;
uniform sampler2D uSimHeight;
uniform vec2 uSimSize;
uniform float uCellM;
uniform sampler2D uHeightMap;
uniform vec2 uHeightMapSize;
uniform float uHeightScale;
uniform float uSeaLevel;
uniform vec3 uLightDir;
uniform vec3 uLightColour;
uniform vec3 uFogColour;
uniform float uFogNear;
uniform float uFogFar;
uniform vec3 uCameraPosition;
uniform vec3 uRingCenter;
uniform float uRingInner;
uniform float uRingOuter;
uniform bool uLodColour;
uniform vec3 uLodColourValue;
out vec4 oColour;
void main()
{
    float dist = length(vWorldPosition.xz - uRingCenter.xz);
    if (dist < uRingInner || dist > uRingOuter) discard;

    vec2 uv = vUv;
    float h = texture(uSimHeight, uv).r;
    float hr = texture(uSimHeight, uv + vec2(1.0, 0.0) / uSimSize).r;
    float hl = texture(uSimHeight, uv + vec2(-1.0, 0.0) / uSimSize).r;
    float hb = texture(uSimHeight, uv + vec2(0.0, -1.0) / uSimSize).r;
    float ht = texture(uSimHeight, uv + vec2(0.0, 1.0) / uSimSize).r;
    vec3 normal = normalize(vec3(-(hr - hl) * 0.5 / uCellM, 1.0, -(ht - hb) * 0.5 / uCellM));

    // Depth shading from the terrain height map underneath: shallow water reveals the
    // submerged continent slope, deep water fades to the ocean abyss.
    float terrainH = texture(uHeightMap, uv).r * uHeightScale;
    float depth = uSeaLevel - terrainH;
    float t = clamp(depth / 45.0, 0.0, 1.0);
    vec3 shallow = vec3(0.50, 0.80, 0.70);
    vec3 deep = vec3(0.05, 0.14, 0.26);
    vec3 colour = mix(shallow, deep, t);

    // Foam where the simulated surface is steep (breaks against the shore).
    float gradient = length(vec2(hr - hl, ht - hb));
    float foam = smoothstep(0.10, 0.30, gradient);
    colour = mix(colour, vec3(0.90, 0.95, 1.0), foam);

    vec3 lightDir = normalize(uLightDir);
    vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
    vec3 halfDir = normalize(lightDir + viewDir);
    float diffuse = max(dot(normal, lightDir), 0.0);
    float specular = pow(max(dot(normal, halfDir), 0.0), 32.0) * 0.5;
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);

    colour = colour * (0.35 + 0.65 * diffuse) + specular * uLightColour + fresnel * 0.4;
    float distance = length(vWorldPosition - uCameraPosition);
    float fog = clamp((uFogFar - distance) / (uFogFar - uFogNear), 0.0, 1.0);
    colour = mix(colour, uFogColour, 1.0 - fog * fog);

    if (uLodColour) colour = mix(colour, uLodColourValue, 0.55);

    // Shallow water is transparent enough to see the terrain beneath.
    float alpha = 0.55 + 0.40 * t;
    alpha = mix(alpha, 0.97, foam);
    oColour = vec4(colour, alpha);
}`;

const MarkerVertexShader = `#version 300 es
precision highp float;
in vec3 aPosition;
in vec2 aUv;
in vec4 aColour;
uniform mat4 uViewProjection;
out vec4 vColour;
out vec2 vUv;
void main()
{
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
    vColour = aColour;
    vUv = aUv;
}`;

const MarkerFragmentShader = `#version 300 es
precision highp float;
in vec4 vColour;
in vec2 vUv;
out vec4 oColour;
void main()
{
    float d = length(vUv * 2.0 - 1.0);
    if (d > 1.0) discard;
    float edge = smoothstep(0.85, 1.0, d);
    oColour = vColour * (1.0 - edge * 0.6);
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

class OceanRenderer
{
    constructor(gl)
    {
        this.gl = gl;
        this.WaterProgram = CreateProgram(gl, WaterVertexShader, WaterFragmentShader);
        this.MarkerProgram = CreateProgram(gl, MarkerVertexShader, MarkerFragmentShader);
        this.WaterLocations = {
            ViewProjection: gl.getUniformLocation(this.WaterProgram, 'uViewProjection'),
            Model: gl.getUniformLocation(this.WaterProgram, 'uModel'),
            SimHeight: gl.getUniformLocation(this.WaterProgram, 'uSimHeight'),
            SimSize: gl.getUniformLocation(this.WaterProgram, 'uSimSize'),
            CellM: gl.getUniformLocation(this.WaterProgram, 'uCellM'),
            WorldHalfSideM: gl.getUniformLocation(this.WaterProgram, 'uWorldHalfSideM'),
            SeaLevelM: gl.getUniformLocation(this.WaterProgram, 'uSeaLevelM'),
            DisplaceScale: gl.getUniformLocation(this.WaterProgram, 'uDisplaceScale'),
            TaperStartM: gl.getUniformLocation(this.WaterProgram, 'uTaperStartM'),
            RingOuterM: gl.getUniformLocation(this.WaterProgram, 'uRingOuterM'),
            HeightMap: gl.getUniformLocation(this.WaterProgram, 'uHeightMap'),
            HeightMapSize: gl.getUniformLocation(this.WaterProgram, 'uHeightMapSize'),
            HeightScale: gl.getUniformLocation(this.WaterProgram, 'uHeightScale'),
            LightDir: gl.getUniformLocation(this.WaterProgram, 'uLightDir'),
            LightColour: gl.getUniformLocation(this.WaterProgram, 'uLightColour'),
            FogColour: gl.getUniformLocation(this.WaterProgram, 'uFogColour'),
            FogNear: gl.getUniformLocation(this.WaterProgram, 'uFogNear'),
            FogFar: gl.getUniformLocation(this.WaterProgram, 'uFogFar'),
            CameraPosition: gl.getUniformLocation(this.WaterProgram, 'uCameraPosition'),
            RingCenter: gl.getUniformLocation(this.WaterProgram, 'uRingCenter'),
            RingInner: gl.getUniformLocation(this.WaterProgram, 'uRingInner'),
            RingOuter: gl.getUniformLocation(this.WaterProgram, 'uRingOuter'),
            LodColour: gl.getUniformLocation(this.WaterProgram, 'uLodColour'),
            LodColourValue: gl.getUniformLocation(this.WaterProgram, 'uLodColourValue'),
            Position: gl.getAttribLocation(this.WaterProgram, 'aPosition'),
            Uv: gl.getAttribLocation(this.WaterProgram, 'aUv')
        };
        this.MarkerLocations = {
            ViewProjection: gl.getUniformLocation(this.MarkerProgram, 'uViewProjection'),
            Position: gl.getAttribLocation(this.MarkerProgram, 'aPosition'),
            Uv: gl.getAttribLocation(this.MarkerProgram, 'aUv'),
            Colour: gl.getAttribLocation(this.MarkerProgram, 'aColour')
        };

        // [R32F] - Simulation displacement uploaded from CPU each frame.
        this.SimTexture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.SimTexture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.R32F, SimTextureSize, SimTextureSize, 0, gl.RED, gl.FLOAT, null);
        gl.bindTexture(gl.TEXTURE_2D, null);

        this.RingBuffers = [RingNear, RingMid, RingFar].map(Ring => this.BuildRingBuffer(Ring.Grid, Ring.OuterM));

        // Marker quad: disc in local [-25, 25] m, colour baked per vertex.
        const MarkerRadiusM = 22.0;
        this.MarkerVertexBuffer = gl.createBuffer();
        this.MarkerVertexCount = 0;
    }

    BuildRingBuffer(GridSize, OuterM)
    {
        const gl = this.gl;
        const Positions = new Float32Array(GridSize * GridSize * 3);
        const Uvs = new Float32Array(GridSize * GridSize * 2);
        let V = 0;
        for (let Ty = 0; Ty < GridSize; Ty++)
        {
            const Z = -OuterM + (Ty / (GridSize - 1)) * (2.0 * OuterM);
            for (let Tx = 0; Tx < GridSize; Tx++)
            {
                const X = -OuterM + (Tx / (GridSize - 1)) * (2.0 * OuterM);
                Positions[V * 3] = X;
                Positions[V * 3 + 1] = 0.0;
                Positions[V * 3 + 2] = Z;
                Uvs[V * 2] = Tx / (GridSize - 1);
                Uvs[V * 2 + 1] = Ty / (GridSize - 1);
                V++;
            }
        }
        const PositionBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, PositionBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, Positions, gl.STATIC_DRAW);
        const UvBuffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, UvBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, Uvs, gl.STATIC_DRAW);
        return { PositionBuffer, UvBuffer, VertexCount: GridSize * GridSize };
    }

    // [Float32Array] - Upload the current simulation height field as the displacement texture.
    UpdateSimTexture(Data)
    {
        const gl = this.gl;
        gl.bindTexture(gl.TEXTURE_2D, this.SimTexture);
        gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, SimTextureSize, SimTextureSize, gl.RED, gl.FLOAT, Data);
        gl.bindTexture(gl.TEXTURE_2D, null);
    }

    // [m] - Re-centre the rings on the camera's water-plane position (exact float translation).
    SetCenter(WorldX, WorldZ)
    {
        this.CenterX = WorldX;
        this.CenterZ = WorldZ;
    }

    // Settings: { NearRadiusM, FarDistanceM } clamp the active annuli inside the
    // pre-built geometry. Rings are drawn far -> near for correct transparency blending.
    DrawRings(ViewProjection, CameraPosition, LightDir, LightColour, FogColour, FogNear, FogFar, SeaLevel, HeightMapTexture, HeightMapSize, HeightScale, TexelM, Settings, WireframeEnabled, LodColourEnabled)
    {
        const gl = this.gl;
        gl.useProgram(this.WaterProgram);
        gl.uniformMatrix4fv(this.WaterLocations.ViewProjection, false, ViewProjection);
        gl.uniform3fv(this.WaterLocations.CameraPosition, CameraPosition);
        gl.uniform3fv(this.WaterLocations.LightDir, LightDir);
        gl.uniform3fv(this.WaterLocations.LightColour, LightColour);
        gl.uniform3fv(this.WaterLocations.FogColour, FogColour);
        gl.uniform1f(this.WaterLocations.FogNear, FogNear);
        gl.uniform1f(this.WaterLocations.FogFar, FogFar);
        gl.uniform1f(this.WaterLocations.SeaLevel, SeaLevel);
        gl.uniform1f(this.WaterLocations.HeightMapSize, HeightMapSize);
        gl.uniform1f(this.WaterLocations.HeightScale, HeightScale);
        gl.uniform1f(this.WaterLocations.CellM, TexelM === undefined ? 32.0 : TexelM);
        gl.uniform2f(this.WaterLocations.SimSize, SimTextureSize, SimTextureSize);
        gl.uniform1f(this.WaterLocations.WorldHalfSideM, 4000.0);
        gl.uniform1i(this.WaterLocations.LodColour, LodColourEnabled ? 1 : 0);

        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.SimTexture);
        gl.uniform1i(this.WaterLocations.SimHeight, 0);
        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, HeightMapTexture);
        gl.uniform1i(this.WaterLocations.HeightMap, 1);

        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);

        const NearRadiusM = Settings.NearRadiusM;
        const FarDistanceM = Settings.FarDistanceM;
        const Specs = [
            { Ring: RingNear, Buffer: this.RingBuffers[0], InnerM: 0.0, OuterM: NearRadiusM, ScaleM: NearRadiusM / RingNearMaxM, DisplaceScale: 1.0, TaperStartM: -1.0 },
            { Ring: RingMid, Buffer: this.RingBuffers[1], InnerM: NearRadiusM, OuterM: FarDistanceM, ScaleM: FarDistanceM / RingMidMaxM, DisplaceScale: 1.0, TaperStartM: FarDistanceM * 0.72 },
            { Ring: RingFar, Buffer: this.RingBuffers[2], InnerM: FarDistanceM, OuterM: RingFarMaxM, ScaleM: 1.0, DisplaceScale: 0.0, TaperStartM: -1.0 }
        ];

        for (const Spec of Specs)
        {
            gl.uniform1f(this.WaterLocations.RingInner, Spec.InnerM);
            gl.uniform1f(this.WaterLocations.RingOuter, Spec.OuterM);
            gl.uniform1f(this.WaterLocations.DisplaceScale, Spec.DisplaceScale);
            gl.uniform1f(this.WaterLocations.TaperStartM, Spec.TaperStartM);
            gl.uniform1f(this.WaterLocations.RingOuterM, Spec.OuterM);
            gl.uniform3fv(this.WaterLocations.LodColourValue, Spec.Ring.Colour);
            gl.uniform3fv(this.WaterLocations.RingCenter, [this.CenterX, 0.0, this.CenterZ]);

            // Model matrix: uniform scale then translation to the ring centre.
            const S = Spec.ScaleM;
            const Model = new Float32Array([
                S, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, S, 0,
                this.CenterX, 0.0, this.CenterZ, 1
            ]);
            gl.uniformMatrix4fv(this.WaterLocations.Model, false, Model);

            const Buffer = Spec.Buffer;
            gl.bindBuffer(gl.ARRAY_BUFFER, Buffer.PositionBuffer);
            gl.enableVertexAttribArray(this.WaterLocations.Position);
            gl.vertexAttribPointer(this.WaterLocations.Position, 3, gl.FLOAT, false, 0, 0);

            gl.bindBuffer(gl.ARRAY_BUFFER, Buffer.UvBuffer);
            gl.enableVertexAttribArray(this.WaterLocations.Uv);
            gl.vertexAttribPointer(this.WaterLocations.Uv, 2, gl.FLOAT, false, 0, 0);

            gl.drawArrays(WireframeEnabled ? gl.LINE_STRIP : gl.TRIANGLE_STRIP, 0, Buffer.VertexCount);
        }
        gl.disable(gl.BLEND);
    }

    // Instances: [{Position: [x,y,z], Colour: [r,g,b,a]}, ...] — one disc marker per base.
    DrawMarkers(ViewProjection, Instances)
    {
        if (Instances.length === 0) return;
        const gl = this.gl;
        gl.useProgram(this.MarkerProgram);
        gl.uniformMatrix4fv(this.MarkerLocations.ViewProjection, false, ViewProjection);

        const MarkerRadiusM = 22.0;
        const Vertices = new Float32Array(Instances.length * 4 * 9);
        let V = 0;
        for (const Instance of Instances)
        {
            const X = Instance.Position[0];
            const Y = Instance.Position[1];
            const Z = Instance.Position[2];
            const R = Instance.Colour[0];
            const G = Instance.Colour[1];
            const B = Instance.Colour[2];
            const A = Instance.Colour[3];
            const Corners = [
                [-MarkerRadiusM, -MarkerRadiusM, 0.0, 0.0],
                [ MarkerRadiusM, -MarkerRadiusM, 1.0, 0.0],
                [-MarkerRadiusM,  MarkerRadiusM, 0.0, 1.0],
                [ MarkerRadiusM,  MarkerRadiusM, 1.0, 1.0]
            ];
            for (const Corner of Corners)
            {
                Vertices[V++] = X + Corner[0];
                Vertices[V++] = Y;
                Vertices[V++] = Z + Corner[1];
                Vertices[V++] = Corner[2];
                Vertices[V++] = Corner[3];
                Vertices[V++] = R;
                Vertices[V++] = G;
                Vertices[V++] = B;
                Vertices[V++] = A;
            }
        }
        gl.bindBuffer(gl.ARRAY_BUFFER, this.MarkerVertexBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, Vertices, gl.DYNAMIC_DRAW);

        gl.enableVertexAttribArray(this.MarkerLocations.Position);
        gl.vertexAttribPointer(this.MarkerLocations.Position, 3, gl.FLOAT, false, 36, 0);
        gl.enableVertexAttribArray(this.MarkerLocations.Uv);
        gl.vertexAttribPointer(this.MarkerLocations.Uv, 2, gl.FLOAT, false, 36, 12);
        gl.enableVertexAttribArray(this.MarkerLocations.Colour);
        gl.vertexAttribPointer(this.MarkerLocations.Colour, 4, gl.FLOAT, false, 36, 20);

        for (let I = 0; I < Instances.length; I++)
        {
            gl.drawArrays(gl.TRIANGLE_STRIP, I * 4, 4);
        }
    }
}

export { OceanRenderer, SimTextureSize, RingNearMaxM, RingMidMaxM, RingFarMaxM };
