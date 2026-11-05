/*==========================================================================================================================================
                                                         CABINVOLUME.SLANG
                                     ------------------------------------------------------------------
                   How far a car-body-frame point lies inside the ControlVehicle's cabin:
                              1 deep inside, 0 outside, soft over four centimetres.
==========================================================================================================================================*/

// 🧩 The cabin's interior: the Slang form of the demo's CabinVolume.wgsl, 1:1.
//
// 📝 The car's shell is one facet thick — thinner than any distance-field voxel — and the cabin lies
//    inside the body's own distance volume, so the traced indirect light there is the sunlit exterior's
//    cards seen from within, and the cabin glowed brighter than the street. Lumen asks for thick walls
//    and open interiors; here the cabin is known, so the light that cannot be traced is attenuated
//    instead: two boxes, the greenhouse up to the headliner within the glass line and the lower tub
//    forward to the footwells and back to the rear bench. The sun through the glazing is direct light
//    (the shadow raster lets it in) and is never touched.

// 📝 Share of a point inside an axis-aligned box: 1 once the point is Soft [m] inside every face, 0 at
//    or outside any face.
public float ResolveBoxShare(float3 Point, float3 Minimum, float3 Maximum, float Soft)
{
    const float3 Inside = min(Point - Minimum, Maximum - Point);
    return clamp(min(min(Inside.x, Inside.y), Inside.z) / Soft, 0.0, 1.0);
}

// 📝 The greenhouse cabin: X[-0.95, 1.33] Y[-1.04, 1.04] Z[-0.12, 1.19] in body-local metres.
public float ResolveGreenhouseShare(float3 BodyLocal)
{
    return ResolveBoxShare(BodyLocal, float3(-0.95, -1.04, -0.12), float3(1.33, 1.04, 1.19), 0.04);
}

// 📝 The lower tub: X[-1.85, 1.38] Y[-1.02, 1.02] Z[-0.12, 0.60] in body-local metres.
public float ResolveTubShare(float3 BodyLocal)
{
    return ResolveBoxShare(BodyLocal, float3(-1.85, -1.02, -0.12), float3(1.38, 1.02, 0.60), 0.04);
}

// 📝 How far a body-local point lies inside the cabin: the further in of the two boxes, so a point in
//    the footwell and a point under the headliner are both deep inside.
public float ResolveCabinShare(float3 BodyLocal)
{
    return max(ResolveGreenhouseShare(BodyLocal), ResolveTubShare(BodyLocal));
}
