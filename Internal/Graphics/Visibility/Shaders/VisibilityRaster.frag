// ============================================================================================================================================
//                                                          VISIBILITYRASTER.FRAG
// ============================================================================================================================================
// 🧩 Fragment stage of the hardware visibility raster. Writes ONE packed surface identity per covered pixel into the R32_UINT visibility buffer:
//    the flat partition ordinal in the high 12 bits, gl_PrimitiveID (the triangle ordinal within the draw) in the low 20 bits. A later resolve
//    unpacks the pair to reconstruct shading inputs. No colour, no lighting — depth-test + this identity write are the whole job. Hardware depth
//    testing against the paired D32 target resolves occlusion, so exactly the nearest surface's identity survives at each pixel.
#version 450

layout(location = 0) flat in uint FragPartitionId;

layout(location = 0) out uint OutIdentity;

// 20 bits of primitive ordinal (up to 1,048,575 triangles per mesh — comfortably over the subdivided Suzanne's 15,744) in the low bits; the
// partition ordinal takes the high 12 bits (up to 4,095 placed heads — over the pyramid-stress worst case). A resolve masks the low 20 for the
// primitive and shifts down 20 for the partition. This pack layout MUST match SoftwareRasterization.comp (the A/B raster) and every consumer of the
// visibility buffer — VisibilityInscription.frag and SurfaceShade.comp.
const uint PrimitiveBits = 20u;
const uint PrimitiveMask = (1u << PrimitiveBits) - 1u;

void main()
{
    uint Primitive = uint(gl_PrimitiveID) & PrimitiveMask;
    OutIdentity = (FragPartitionId << PrimitiveBits) | Primitive;
}
