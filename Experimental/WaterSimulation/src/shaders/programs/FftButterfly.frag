#version 300 es
precision highp float;

#include "Common.glsl"

uniform sampler2D uSourceA;
uniform sampler2D uSourceB;
uniform int   uStage;
uniform int   uHorizontal;
uniform float uSign;
uniform int   uSize;

layout(location = 0) out vec4 FragColorA;
layout(location = 1) out vec4 FragColorB;

void main()
{
    ivec2 Texel = ivec2(gl_FragCoord.xy);
    int Index = uHorizontal == 1 ? Texel.x : Texel.y;
    int Half = 1 << uStage;
    int Length = Half << 1;
    int Local = Index - (Index / Length) * Length;
    int Offset = Local >= Half ? Local - Half : Local;
    int Base = Index - Local;
    int EvenIndex = Base + Offset;
    int OddIndex = EvenIndex + Half;

    ivec2 EvenTexel = uHorizontal == 1 ? ivec2(EvenIndex, Texel.y) : ivec2(Texel.x, EvenIndex);
    ivec2 OddTexel = uHorizontal == 1 ? ivec2(OddIndex, Texel.y) : ivec2(Texel.x, OddIndex);
    vec2 EvenCoordinate = (vec2(EvenTexel) + 0.5) / float(uSize);
    vec2 OddCoordinate = (vec2(OddTexel) + 0.5) / float(uSize);

    float Angle = uSign * TwoPi * float(Offset) / float(Length);
    vec2 Twiddle = vec2(cos(Angle), sin(Angle));

    vec4 EvenA = textureLod(uSourceA, EvenCoordinate, 0.0);
    vec4 OddA = textureLod(uSourceA, OddCoordinate, 0.0);
    vec4 EvenB = textureLod(uSourceB, EvenCoordinate, 0.0);
    vec4 OddB = textureLod(uSourceB, OddCoordinate, 0.0);

    float SlotSign = Local >= Half ? -1.0 : 1.0;
    vec2 RotatedA0 = ComplexMultiply(OddA.xy, Twiddle) * SlotSign;
    vec2 RotatedA1 = ComplexMultiply(OddA.zw, Twiddle) * SlotSign;
    vec2 RotatedB0 = ComplexMultiply(OddB.xy, Twiddle) * SlotSign;
    vec2 RotatedB1 = ComplexMultiply(OddB.zw, Twiddle) * SlotSign;

    FragColorA = vec4(EvenA.xy + RotatedA0, EvenA.zw + RotatedA1);
    FragColorB = vec4(EvenB.xy + RotatedB0, EvenB.zw + RotatedB1);
}
