/*====================================================================================================================================
                                                        SPRAY PARTICLES.JS
====================================================================================================================================*/
// 🧩 GPU-resident 3D spray: transform-feedback integration, soft rasterization, and field deposits

export const EmitterCapacity = 6;

export class SprayParticles
{
    constructor(Device, Programs, Descriptor = {})
    {
        this.Device = Device;
        this.Programs = Programs;
        this.Capacity = Descriptor.Capacity ?? 65536;
        this.ActiveRatio = 1.0;
        this.Current = 0;
        this.EmitterCount = 0;
        this.EmitterPosition = new Float32Array(EmitterCapacity * 4);
        this.EmitterDirection = new Float32Array(EmitterCapacity * 4);
        this.EmitterProfile = new Float32Array(EmitterCapacity * 4);

        const Gl = Device.Gl;
        const PositionArray = new Float32Array(this.Capacity * 4);
        const VelocityArray = new Float32Array(this.Capacity * 4);
        const SprayArray = new Float32Array(this.Capacity * 4);

        for (let Index = 0; Index < this.Capacity; Index++)
        {
            SprayArray[Index * 4 + 0] = Math.random();
            SprayArray[Index * 4 + 1] = Math.random() * 2.0;
            SprayArray[Index * 4 + 2] = 0.0;
            SprayArray[Index * 4 + 3] = 0.0;
        }

        const CreateState = () =>
        {
            const State = {};
            const Upload = (Array) =>
            {
                const Buffer = Gl.createBuffer();
                Gl.bindBuffer(Gl.ARRAY_BUFFER, Buffer);
                Gl.bufferData(Gl.ARRAY_BUFFER, Array, Gl.DYNAMIC_COPY);
                return Buffer;
            };

            State.Position = Upload(PositionArray);
            State.Velocity = Upload(VelocityArray);
            State.Spray = Upload(SprayArray);
            return State;
        };

        this.States = [CreateState(), CreateState()];
        this.AttributeArrayObject = Gl.createVertexArray();
        this.Feedback = Gl.createTransformFeedback();
        this.AttributeCount = 16;
    }

    get ActiveCount()
    {
        return Math.max(0, Math.floor(this.Capacity * this.ActiveRatio));
    }

    SetEmitter(Index, Descriptor)
    {
        if (Index >= EmitterCapacity)
        {
            return;
        }

        const PositionOffset = Index * 4;
        this.EmitterPosition[PositionOffset + 0] = Descriptor.Position[0];
        this.EmitterPosition[PositionOffset + 1] = Descriptor.Position[1];
        this.EmitterPosition[PositionOffset + 2] = Descriptor.Position[2];
        this.EmitterPosition[PositionOffset + 3] = 0.0;

        this.EmitterDirection[PositionOffset + 0] = Descriptor.Direction[0];
        this.EmitterDirection[PositionOffset + 1] = Descriptor.Direction[1];
        this.EmitterDirection[PositionOffset + 2] = Descriptor.Direction[2];
        this.EmitterDirection[PositionOffset + 3] = Descriptor.Spread;

        this.EmitterProfile[PositionOffset + 0] = Descriptor.Radius;
        this.EmitterProfile[PositionOffset + 1] = Descriptor.Speed;
        this.EmitterProfile[PositionOffset + 2] = Descriptor.Weight;
        this.EmitterProfile[PositionOffset + 3] = Descriptor.Category;

        this.EmitterCount = Math.max(this.EmitterCount, Index + 1);
    }

    ClearEmitters()
    {
        this.EmitterCount = 0;
    }

    BindAttributes(Program, State)
    {
        const Gl = this.Device.Gl;

        for (let Index = 0; Index < this.AttributeCount; Index++)
        {
            Gl.disableVertexAttribArray(Index);
        }

        Gl.bindVertexArray(this.AttributeArrayObject);

        const Layout =
        [
            ['a_Position', State.Position, 4],
            ['a_Velocity', State.Velocity, 4],
            ['a_Spray', State.Spray, 4]
        ];

        for (const [Name, Buffer, Size] of Layout)
        {
            const Location = Gl.getAttribLocation(Program.Program, Name);

            if (Location >= 0)
            {
                Gl.bindBuffer(Gl.ARRAY_BUFFER, Buffer);
                Gl.enableVertexAttribArray(Location);
                Gl.vertexAttribPointer(Location, Size, Gl.FLOAT, false, 0, 0);
            }
        }
    }

    Update(DeltaSeconds, AtlasTexture, AtlasDomain, WindVector)
    {
        if (this.EmitterCount === 0)
        {
            return;
        }

        const Gl = this.Device.Gl;
        const Source = this.States[this.Current];
        const Destination = this.States[1 - this.Current];
        const Program = this.Programs.get('SprayUpdate').Use();
        const Uniforms = Program.Uniforms;

        Uniforms.SetFloat('uDeltaSeconds', Math.min(DeltaSeconds, 1.0 / 30.0));
        Uniforms.SetVec2('uWindVector', WindVector[0], WindVector[1]);
        Uniforms.SetTexture('uHeightAtlas', AtlasTexture, 12);
        Uniforms.SetFloat('uAtlasDomain', AtlasDomain);
        Uniforms.SetArray4('uEmitterPosition', this.EmitterPosition);
        Uniforms.SetArray4('uEmitterDirection', this.EmitterDirection);
        Uniforms.SetArray4('uEmitterProfile', this.EmitterProfile);
        Uniforms.SetInt('uEmitterCount', this.EmitterCount);

        this.BindAttributes(Program, Source);

        Gl.bindTransformFeedback(Gl.TRANSFORM_FEEDBACK, this.Feedback);
        Gl.bindBufferBase(Gl.TRANSFORM_FEEDBACK_BUFFER, 0, Destination.Position);
        Gl.bindBufferBase(Gl.TRANSFORM_FEEDBACK_BUFFER, 1, Destination.Velocity);
        Gl.bindBufferBase(Gl.TRANSFORM_FEEDBACK_BUFFER, 2, Destination.Spray);

        Gl.enable(Gl.RASTERIZER_DISCARD);
        Gl.beginTransformFeedback(Gl.POINTS);
        Gl.drawArrays(Gl.POINTS, 0, this.ActiveCount);
        Gl.endTransformFeedback();
        Gl.disable(Gl.RASTERIZER_DISCARD);

        Gl.bindBufferBase(Gl.TRANSFORM_FEEDBACK_BUFFER, 0, null);
        Gl.bindBufferBase(Gl.TRANSFORM_FEEDBACK_BUFFER, 1, null);
        Gl.bindBufferBase(Gl.TRANSFORM_FEEDBACK_BUFFER, 2, null);
        Gl.bindTransformFeedback(Gl.TRANSFORM_FEEDBACK, null);
        Gl.bindVertexArray(null);

        this.Current = 1 - this.Current;
    }

    // 💡 Impact splats write straight into the global grids, so spray feeds ripples and foam
    Deposit(Target, Domain, FoamWeight, VelocityWeight)
    {
        const Gl = this.Device.Gl;
        const Program = this.Programs.get('SprayDeposit').Use();
        const Uniforms = Program.Uniforms;

        Uniforms.SetVec2('uDomain', Domain, Domain);
        Uniforms.SetFloat('uPointSize', 7.0);
        Uniforms.SetFloat('uFoamWeight', FoamWeight);
        Uniforms.SetFloat('uVelocityWeight', VelocityWeight);

        this.BindAttributes(Program, this.States[this.Current]);

        Target.Bind();
        Gl.enable(Gl.BLEND);
        Gl.blendFunc(Gl.ONE, Gl.ONE);
        Gl.drawArrays(Gl.POINTS, 0, this.ActiveCount);
        Gl.disable(Gl.BLEND);
        Gl.bindVertexArray(null);
        Gl.bindFramebuffer(Gl.FRAMEBUFFER, null);
    }

    Rasterize(ViewProjection, PixelScale, DepthTexture, Viewport, NearPlane, FarPlane, SprayTint)
    {
        const Gl = this.Device.Gl;
        const Program = this.Programs.get('SprayRasterization').Use();
        const Uniforms = Program.Uniforms;

        Uniforms.SetMatrix4('uViewProjection', ViewProjection);
        Uniforms.SetFloat('uPixelScale', PixelScale);
        Uniforms.SetTexture('uSceneDepth', DepthTexture, 11);
        Uniforms.SetVec2('uViewport', Viewport[0], Viewport[1]);
        Uniforms.SetFloat('uNearPlane', NearPlane);
        Uniforms.SetFloat('uFarPlane', FarPlane);
        Uniforms.SetVec3('uSprayTint', SprayTint[0], SprayTint[1], SprayTint[2]);

        this.BindAttributes(Program, this.States[this.Current]);

        Gl.enable(Gl.BLEND);
        Gl.blendFunc(Gl.SRC_ALPHA, Gl.ONE_MINUS_SRC_ALPHA);
        Gl.drawArrays(Gl.POINTS, 0, this.ActiveCount);
        Gl.disable(Gl.BLEND);
        Gl.bindVertexArray(null);
    }
}
