// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
//                                                    TRANSMITTANCE LOOKUP 🧩
// ════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════════
// Samples the baked transmittance LUT with the SAME (altitude, μ) → uv mapping the Transmittance.comp bake used. Included by every shader that
// needs T(point→space): the multi-scatter bake, the sky-view bake, and (indirectly) the sky dome. Requires AtmosphereCommon.glsl before it.

layout(binding = 1) uniform sampler2D TransmittanceLUT;   // 256×64 RGBA16F, X = μ (mapped -1..1), Y = altitude 0..top

vec3 SampleTransmittanceToSpace(float RadiusFromCentre, float ViewZenithCosine)
{
    float Altitude = RadiusFromCentre - Atmosphere.BottomRadius;
    float U = 0.5 * (ViewZenithCosine + 1.0);
    float V = clamp(Altitude / max(Atmosphere.TopRadius - Atmosphere.BottomRadius, 1e-4), 0.0, 1.0);
    return texture(TransmittanceLUT, vec2(U, V)).rgb;
}
