#version 300 es
precision highp float;

#include "Common.glsl"
#include "Noise.glsl"

in vec4 a_Position;   // world xyz, remaining life
in vec4 a_Velocity;   // velocity xyz, droplet size
in vec4 a_Spray;      // seed, age, category, impact

uniform float uDeltaSeconds;
uniform vec2  uWindVector;
uniform sampler2D uHeightAtlas;
uniform float uAtlasDomain;
uniform vec4  uEmitterPosition[6];
uniform vec4  uEmitterDirection[6];
uniform vec4  uEmitterProfile[6];    // radius, speed, weight, category
uniform int   uEmitterCount;

out vec4 vPosition;
out vec4 vVelocity;
out vec4 vSpray;

float AtlasHeight(vec2 WorldXz)
{
    return textureLod(uHeightAtlas, WorldXz / uAtlasDomain + 0.5, 0.0).r;
}

// 🧩 GPU-resident 3D spray: ballistic droplets respawn at weighted emitters and deposit on impact
void main()
{
    float Life = a_Position.w;
    vec3 Position = a_Position.xyz;
    vec3 Velocity = a_Velocity.xyz;
    float Size = a_Velocity.w;
    float Seed = a_Spray.x;
    float Age = a_Spray.y + uDeltaSeconds;
    float Category = a_Spray.z;
    float Impact = 0.0;

    if (Life <= 0.0)
    {
        int ChosenEmitter = 0;
        float BestWeight = -1.0;

        for (int Index = 0; Index < 6; Index++)
        {
            if (Index >= uEmitterCount)
            {
                break;
            }

            float Weight = uEmitterProfile[Index].z * HashScalar(vec2(Seed * 91.7, float(Index) * 13.3 + Age));
            if (Weight > BestWeight)
            {
                BestWeight = Weight;
                ChosenEmitter = Index;
            }
        }

        vec4 Emitter = uEmitterPosition[ChosenEmitter];
        vec4 Direction = uEmitterDirection[ChosenEmitter];
        vec4 Profile = uEmitterProfile[ChosenEmitter];

        vec2 Jitter = HashVector(vec2(Seed * 17.3 + 3.1, Age * 7.7)) * 2.0 - 1.0;
        vec2 Disc = HashVector(vec2(Seed * 31.7, 5.3)) * 2.0 - 1.0;

        Position = Emitter.xyz + vec3(Disc.x, 0.0, Disc.y) * Profile.x;
        Position.y = max(Position.y, AtlasHeight(Position.xz) + 0.03);

        vec3 Cone = normalize(Direction.xyz + vec3(Jitter.x * Direction.w, abs(Jitter.y) * Direction.w, Jitter.y * Direction.w));
        Velocity = Cone * Profile.y * (0.55 + 0.9 * HashScalar(vec2(Seed * 7.1, Age + 2.0)));
        Size = mix(0.06, 0.2, HashScalar(vec2(Seed * 3.7, 9.1)));
        Category = Profile.w;
        Life = mix(0.65, 2.6, HashScalar(vec2(Seed * 5.3, 1.7)));
        Age = 0.0;
    }

    float GravityScale = Category > 0.5 ? 0.5 : 1.0;
    Velocity.y -= Gravity * GravityScale * uDeltaSeconds;
    Velocity += vec3(uWindVector.x, 0.0, uWindVector.y) * uDeltaSeconds * 0.45;
    Velocity *= exp(-(Category > 0.5 ? 1.4 : 0.5) * uDeltaSeconds);
    Position += Velocity * uDeltaSeconds;

    float Surface = AtlasHeight(Position.xz);
    if (Position.y <= Surface)
    {
        Impact = clamp(length(Velocity) * 0.11, 0.0, 1.0) * (Category > 0.5 ? 0.2 : 1.0);
        Position.y = Surface + 0.015;
        Life = 0.0;
    }

    Life -= uDeltaSeconds;

    vPosition = vec4(Position, max(Life, 0.0));
    vVelocity = vec4(Velocity, Size);
    vSpray = vec4(Seed, Age, Category, Impact);
}
