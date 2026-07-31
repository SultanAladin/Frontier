#version 450
// ============================================================================================================================================
//                                                          SOFTWARERESOLVE.FRAG
// ============================================================================================================================================
// 🧩 The resolve half of the software micro-raster (PLAN §6). A fullscreen triangle (VisibilityInscription.vert) runs this once per pixel to copy
//    the software raster's packed (DepthKey << 32) | Identity word out of the R64 target and INTO the exact same two attachments the hardware
//    raster fills — the R32_UINT visibility buffer (colour attachment 0) and the D32 depth target (gl_FragDepth) — so everything downstream (the
//    inscription debug composite, the HiZ pyramid that samples VisibilityDepth) is byte-for-byte agnostic to which raster produced the frame. That
//    is the whole point of the resolve being a GRAPHICS draw rather than a compute store: it writes through the ordinary colour/depth attachment
//    machinery, in the ordinary attachment formats, so no storage-image format support is assumed on Pascal.
//
//    UNPACK. DepthKey is the high 32 bits, built by the raster so NEARER depth → LARGER key; Identity is the low 32 bits, packed identically to
//    VisibilityRaster.frag. A zero high half is the empty sentinel (VisibilityPackedEmptySentinel): no surface covered this pixel, so we emit the
//    R32 empty sentinel (all-ones) and push depth to the far plane, and the pixel reads as background exactly as an uncovered hardware-raster pixel
//    would. Otherwise depth is recovered from the key by the inverse of the raster's map: ndcDepth = 1 - key / 0xFFFFFFFF.

layout(location = 0) in  vec2 FragTexCoord;   // unused; the resolve keys off gl_FragCoord
layout(location = 0) out uint OutIdentity;    // -> R32_UINT visibility buffer (colour attachment 0)

#extension GL_ARB_gpu_shader_int64 : require

// The packed-target routes, mirroring SoftwareRasterization.comp: exactly one is compiled in per pipeline variant the host loads.
#ifndef PACKED_ROUTE_IMAGE
#define PACKED_ROUTE_IMAGE 0
#endif
#ifndef PACKED_ROUTE_BUFFER
#define PACKED_ROUTE_BUFFER 0
#endif

// The image route reads the R64 storage image (u64image2D + imageLoad), which needs GL_EXT_shader_image_int64. The buffer route only reads a
// uint64_t SSBO element — no atomic — so GL_ARB_gpu_shader_int64 above already covers it.
#if PACKED_ROUTE_IMAGE
#extension GL_EXT_shader_image_int64 : require
#endif

// set 0, binding 0 — the packed R64 target the raster atomicMaxed. Image route: read via imageLoad. Buffer route: read the linear word.
#if PACKED_ROUTE_IMAGE
layout(set = 0, binding = 0, r64ui) uniform readonly u64image2D PackedImage;
#endif
#if PACKED_ROUTE_BUFFER
layout(std430, set = 0, binding = 0) readonly buffer PackedBufferBlock
{
    uint64_t PackedWords[];
};
#endif

layout(push_constant) uniform SoftwareResolveConstants
{
    uvec2 TargetExtent;   // [px] - target width/height, for the buffer route's linear index
    uint  Pad0;
    uint  Pad1;
} Constants;

// The empty packed word (matches VisibilityPackedEmptySentinel): a zero high half means no surface. The R32 empty sentinel (matches
// VisibilityEmptySentinel) is emitted at those pixels so the inscription treats them as background.
const uint VisibilityEmptySentinel = 0xFFFFFFFFu;

void main()
{
    ivec2 Pixel = ivec2(gl_FragCoord.xy);

#if PACKED_ROUTE_IMAGE
    uint64_t Word = imageLoad(PackedImage, Pixel).x;
#elif PACKED_ROUTE_BUFFER
    uint Linear   = uint(Pixel.y) * Constants.TargetExtent.x + uint(Pixel.x);
    uint64_t Word = PackedWords[Linear];
#else
    uint64_t Word = 0ul;
#endif

    uint DepthKey = uint(Word >> 32);
    uint Identity = uint(Word & 0xFFFFFFFFul);

    if (DepthKey == 0u)
    {
        // No surface — background. Emit the R32 sentinel and push depth to the far plane so the depth target matches an uncovered hardware pixel.
        OutIdentity   = VisibilityEmptySentinel;
        gl_FragDepth  = 1.0;
        return;
    }

    OutIdentity  = Identity;
    // Inverse of the raster's DepthToKey: ndcDepth = 1 - key / 0xFFFFFFFF. Restores the same D32 value the hardware raster's depth test wrote.
    gl_FragDepth = 1.0 - float(DepthKey) / 4294967295.0;
}
