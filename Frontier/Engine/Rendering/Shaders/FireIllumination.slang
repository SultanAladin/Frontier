/*==========================================================================================================================================
                                                       FIREILLUMINATION.SLANG
                                     ------------------------------------------------------------------
                    A burning tyre as a point light on the scene: inverse-square, windowed to
                              its reach, shadowed through the scene distance field.
==========================================================================================================================================*/

// 🧩 Fire light: the Slang form of the demo's FireIllumination.wgsl.
//
// 📝 Read by the direct pass (screen) and the card direct pass (the surface cache), so a tyre fire
//    lights the course and the car directly and, through the cards the final gather reads, bounces
//    into the indirect term as well. The shadow march stops short of the light: the light sits inside
//    the burning wheel, which must not shadow itself.
//
//    The window (1 − r²)² is the same squared-smoothstep every compact light in the renderer uses: it
//    reaches exactly zero at the light's reach, so a fire can be culled by its reach with no seam and
//    no discontinuity in its derivative at the boundary.
//
// 📝 This module is a library: it declares no bindings of its own. It reads the scene distance field
//    through PlumeRecords' group 0, which every program that lights a scene already binds.

import PlumeRecords;

// 📝 One fire light: a world position and reach [m], and a linear radiance [W/sr].
public struct FireLightDescriptor
{
    public float4 Position;    // xyz [m] world centre, w [m] reach
    public float4 Radiance;    // rgb linear radiance, w unused
};

public struct FireArrival
{
    public float3 Direction;      // [-] unit, toward the fire
    public float3 Illuminance;    // [-] arriving at normal incidence, shadowed and windowed
};

// 📝 Half a distance-field cell: enough to lift a sample off the surface it belongs to, whatever the
//    field's resolution.
public float ResolveSurfaceBias()
{
    return max(0.5 * PlumeDistance.Minimum.w, 0.01);
}

// 📝 Sphere-traced visibility toward the fire. The march stops short of the light by Trim [m], so the
//    burning wheel the light sits inside does not shadow it.
public float ResolveFireVisibility(float3 Position, float3 Toward, float Reach, float Trim = 0.6)
{
    float Span = max(Reach - Trim, 0.0);
    if (Span <= 0.0)
        return 1.0;

    float Travel = 0.0;
    for (uint Step = 0u; Step < 48u; ++Step)
    {
        float Distance = SampleSceneDistance(Position + Toward * Travel).x;
        if (Distance <= 0.02)
            return 0.0;
        Travel += max(Distance, 0.02);
        if (Travel >= Span)
            break;
    }
    return 1.0;
}

// 📝 What arrives at Surface from Fire, on a surface whose normal is Normal. Illuminance is radiance
//    over the inverse square, windowed to the reach and shadowed through the distance field; it is
//    zero behind the surface and beyond the reach.
public FireArrival ResolveFireArrival(FireLightDescriptor Fire, float3 Surface, float3 Normal, bool Shadowed)
{
    FireArrival Arrival;
    Arrival.Direction = float3(0.0, 0.0, 1.0);
    Arrival.Illuminance = float3(0.0);

    const float Intensity = dot(Fire.Radiance.rgb, float3(1.0));
    if (Intensity <= 0.0)
        return Arrival;

    const float3 Toward = Fire.Position.xyz - Surface;
    const float Squared = dot(Toward, Toward);
    const float Reach = Fire.Position.w;
    if (Squared >= Reach * Reach)
        return Arrival;

    const float Range = sqrt(Squared);
    Arrival.Direction = Toward / max(Range, 1.0e-4);
    if (dot(Normal, Arrival.Direction) <= 0.0)
        return Arrival;

    const float Ratio = Squared / (Reach * Reach);
    const float Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);

    float Visibility = 1.0;
    if (Shadowed)
    {
        const float Bias = ResolveSurfaceBias();
        Visibility = ResolveFireVisibility(Surface + Normal * Bias, Arrival.Direction, Range, max(Bias, 0.6));
    }

    Arrival.Illuminance = Fire.Radiance.rgb * (Window * Visibility / max(Squared, 0.25));
    return Arrival;
}
