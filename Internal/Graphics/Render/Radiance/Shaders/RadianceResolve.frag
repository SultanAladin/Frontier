#version 450

// 🧩 Fragment stage of the radiance resolve: sample one pixel of the LINEAR HDR radiance target, apply exposure, tone map ONCE, and write the
//    result to the _SRGB swapchain. This is the single place in the renderer where linear scene radiance becomes a display colour.
//
// 📝 Three defects are closed by this shader existing at all, none of which a different tone-map operator could have fixed:
//      • DOUBLE TONE MAP — the sky and the surface shade each used to map independently and then composite, so two different roll-offs met in
//        one image and the horizon could not match. Both now write linear radiance and only this pass maps.
//      • BLENDING IN DISPLAY SPACE — alpha-over is only correct on linear light. Glass used to blend already-encoded output over an
//        already-encoded sky. The blend now happens in the linear target, upstream of here.
//      • NO EXPOSURE CONTROL — the surface path had none, so the only brightness knob was the light intensity, which silently re-tuned the
//        tone map's operating point. Exposure is now an explicit scalar applied before the curve.
//
// ⚠️ NO sRGB ENCODE HERE. The swapchain is _SRGB, so the presentation hardware applies the exact piecewise OETF on write. Adding a pow()
//    would double-encode. The previous hand-rolled pow(x, 1/2.2) also missed the real curve's linear toe by up to ~6% in the low end.
// ⚠️ The display-referred overlays (grid, selection outline, component handles, clipmap lattice, id-hash resolve) deliberately do NOT pass
//    through here — they draw onto the swapchain AFTER this resolve so their authored colours reach the screen unaltered.

layout(location = 0) in  vec2 FragTexCoord;
layout(location = 0) out vec4 OutColour;

layout(set = 0, binding = 0) uniform sampler2D RadianceBuffer;

layout(push_constant) uniform ResolveConstants
{
    float Exposure;       // [-] - linear multiplier applied BEFORE the curve; 1.0 leaves radiance untouched
    uint  OperatorIndex;  // [-] - 0 = Khronos PBR Neutral, 1 = bypass (linear clamp, for A/B inspection)
    uint  Pad0;
    uint  Pad1;
} Constants;

//------------------------------------------------------------------------------------------------------------------------
//                                                          TONE MAPPING
//------------------------------------------------------------------------------------------------------------------------

// 📝 Khronos PBR Neutral — the operator this renderer standardizes on, moved here verbatim from SurfaceShade.frag so exactly one copy exists.
//    It is the only widely-used operator that is simultaneously ALU-only (no LUT), provably hue-preserving, and analytically invertible, and
//    Khronos designed it for the product/CAD case where an authored colour must survive to the screen. Do NOT substitute ACES or AgX: both
//    deliberately rotate hue, which is wrong when a user picked a specific material colour.
vec3 TonemapPbrNeutral(vec3 Colour)
{
    const float StartCompression = 0.8 - 0.04;
    const float Desaturation     = 0.15;

    float MinChannel = min(Colour.r, min(Colour.g, Colour.b));
    float Offset     = MinChannel < 0.08 ? MinChannel - 6.25 * MinChannel * MinChannel : 0.04;
    Colour -= Offset;

    float Peak = max(Colour.r, max(Colour.g, Colour.b));
    if (Peak < StartCompression)
        return Colour;

    float D = 1.0 - StartCompression;
    float NewPeak = 1.0 - D * D / (Peak + D - StartCompression);
    Colour *= NewPeak / Peak;

    float G = 1.0 - 1.0 / (Desaturation * (Peak - NewPeak) + 1.0);
    return mix(Colour, vec3(NewPeak), G);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            RESOLVE
//------------------------------------------------------------------------------------------------------------------------

void main()
{
    // 📝 texelFetch, not texture(): this is a 1:1 fullscreen blit of a same-size target, so there is nothing to filter and a nearest fetch
    //    avoids any half-texel drift between the radiance target and the swapchain.
    vec3 Radiance = texelFetch(RadianceBuffer, ivec2(gl_FragCoord.xy), 0).rgb;

    Radiance *= max(Constants.Exposure, 0.0);

    // ⚠️ Clamp the floor at zero before the curve. A half-float target can carry a small negative from an aggressive blend, and the operator's
    //    MinChannel branch is not defined for negative input.
    Radiance = max(Radiance, vec3(0.0));

    vec3 Mapped = (Constants.OperatorIndex == 1u)
                ? clamp(Radiance, vec3(0.0), vec3(1.0))   // bypass — linear clamp, for A/B against the curve
                : TonemapPbrNeutral(Radiance);

    // Alpha 1.0: the radiance target's own alpha carried the glass compositing, which already resolved upstream in linear. The swapchain is
    // opaque, so a transparent value here would only confuse the presentation engine.
    OutColour = vec4(Mapped, 1.0);
}
