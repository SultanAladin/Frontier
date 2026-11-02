// 🧩 Numeric constants, saturation helpers, and window-to-view depth conversion
const float Pi           = 3.14159265359;
const float TwoPi        = 6.28318530718;
const float SeaLevel     = 0.0;
const float Gravity      = 9.81;

float Saturate(float Scalar)
{
    return clamp(Scalar, 0.0, 1.0);
}

vec3 Saturate3(vec3 Vector)
{
    return clamp(Vector, vec3(0.0), vec3(1.0));
}

float Luminance(vec3 Color)
{
    return dot(Color, vec3(0.2126, 0.7152, 0.0722));
}

// 📝 Depth texture sampling returns window depth; this linearizes it to view distance
float LinearizeDepth(float WindowDepth, float NearPlane, float FarPlane)
{
    float Normalized = WindowDepth * 2.0 - 1.0;
    return (2.0 * NearPlane * FarPlane) / (FarPlane + NearPlane - Normalized * (FarPlane - NearPlane));
}

vec3 DecodeSlopeNormal(vec2 Slope)
{
    return normalize(vec3(-Slope.x, 1.0, -Slope.y));
}

vec2 EncodeSlopeNormal(vec3 Normal)
{
    return vec2(-Normal.x, -Normal.z) / max(Normal.y, 0.0001);
}

float LuminanceFromRadiance(vec3 Radiance)
{
    return dot(Radiance, vec3(0.2126, 0.7152, 0.0722));
}

// 📝 GLSL ES 3.00 has no bitfieldReverse (that arrived in 3.10), so the FFT index reversal is explicit
int ReverseBits(int Value, int Count)
{
    int Result = 0;
    int Cursor = Value;

    for (int Bit = 0; Bit < 16; Bit++)
    {
        if (Bit >= Count)
        {
            break;
        }

        Result = (Result << 1) | (Cursor & 1);
        Cursor = Cursor >> 1;
    }

    return Result;
}

// 💡 Complex values are packed as (real, imaginary) pairs inside vec2/vec4 lanes
vec2 ComplexMultiply(vec2 Left, vec2 Right)
{
    return vec2(Left.x * Right.x - Left.y * Right.y, Left.x * Right.y + Left.y * Right.x);
}
