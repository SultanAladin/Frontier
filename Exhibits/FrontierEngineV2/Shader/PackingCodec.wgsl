//============================================================================================================================================
//                                                            PACKINGCODEC.WGSL
//============================================================================================================================================
// 🧩 NaN-safe octahedral unit-vector encoding and IEEE-754 half-precision packing for reservoirs and surface attributes.

// 📐 Octahedral projection (Cigolle et al. 2014): the unit sphere folded onto the [−1, 1]² square.
fn EncodeOctahedral(Normal: vec3f) -> vec2f
{
    let L1 = abs(Normal.x) + abs(Normal.y) + abs(Normal.z);
    if (!(L1 > 1.0e-8))
    {
        return vec2f(0.0, 0.0);
    }
    let Projected = Normal.xy / L1;
    if (Normal.z >= 0.0)
    {
        return clamp(Projected, vec2f(-1.0), vec2f(1.0));
    }
    let Folded = (1.0 - abs(Projected.yx)) * select(vec2f(-1.0), vec2f(1.0), Projected >= vec2f(0.0));
    return clamp(Folded, vec2f(-1.0), vec2f(1.0));
}

fn DecodeOctahedral(Encoded: vec2f) -> vec3f
{
    let SafeEnc = select(vec2f(0.0), clamp(Encoded, vec2f(-1.0), vec2f(1.0)), Encoded == Encoded);
    var Normal  = vec3f(SafeEnc.x, SafeEnc.y, 1.0 - abs(SafeEnc.x) - abs(SafeEnc.y));
    let Fold    = max(-Normal.z, 0.0);
    Normal.x   += select(Fold, -Fold, Normal.x >= 0.0);
    Normal.y   += select(Fold, -Fold, Normal.y >= 0.0);
    let LenSq   = dot(Normal, Normal);
    return select(vec3f(0.0, 0.0, 1.0), Normal * inverseSqrt(LenSq), LenSq > 1.0e-12);
}

fn PackOctahedral(Normal: vec3f) -> u32
{
    return pack2x16snorm(EncodeOctahedral(Normal));
}

fn UnpackOctahedral(Packed: u32) -> vec3f
{
    return DecodeOctahedral(unpack2x16snorm(Packed));
}

fn SanitizeHalfFloat(Value: f32, MaxVal: f32) -> f32
{
    return select(0.0, clamp(Value, 0.0, MaxVal), Value == Value);
}

fn PackHalfTriple(Triple: vec3f, Fourth: f32) -> vec2u
{
    let SafeX = SanitizeHalfFloat(Triple.x, 32000.0);
    let SafeY = SanitizeHalfFloat(Triple.y, 32000.0);
    let SafeZ = SanitizeHalfFloat(Triple.z, 32000.0);
    let SafeW = SanitizeHalfFloat(Fourth, 60000.0);
    return vec2u(pack2x16float(vec2f(SafeX, SafeY)), pack2x16float(vec2f(SafeZ, SafeW)));
}

fn UnpackHalfTriple(Packed: vec2u) -> vec4f
{
    let Raw = vec4f(unpack2x16float(Packed.x), unpack2x16float(Packed.y));
    return select(vec4f(0.0), Raw, Raw == Raw);
}
