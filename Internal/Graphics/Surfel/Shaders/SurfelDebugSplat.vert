/*==============================================================================================================================================
                                                            SURFELDEBUGSPLAT.VERT
==============================================================================================================================================*/
// 🧩 The vertex half of the surfel DEBUG splat — one point per pool slot, projected to a screen-space disc so the surfel field is eyeballable.
//    The draw is vkCmdDraw(SurfelMaxCount) with no vertex buffer: this stage reads Surfels[gl_VertexIndex] straight from the pool's surfel SSBO,
//    culls the dead / never-spawned slots off screen, projects the live ones through the frame's ViewProjection, and sizes gl_PointSize from the
//    surfel's world radius mapped to pixels so a near surfel reads as a large disc and a far one shrinks with perspective. Everything the fragment
//    stage needs to colour the disc (age, cascade, cell occupancy) is forwarded flat so no interpolation smears it across the point.
//
//    🔴 THIS IS A DEBUG-ONLY VIEW. It reads the pool but writes nothing back; it composites after shade inside the radiance scope (like GroundGridPass)
//       and is entirely gated by DebugMode — mode 0 draws nothing. It is the visual half of the Phase-1 DoD, NOT a rendering path Phase 2 depends on.
//
//    🔴 DEAD-SLOT CULL MIRRORS THE LIFECYCLE EXACTLY. A slot is live only when its age is a real in-[0,TTL) age; a recycled slot carries the
//       SURFEL_LIFE_RECYCLED sentinel (0x8000000+1, a large POSITIVE int, NOT the sign bit — see SurfelPool.h) and a never-spawned slot is left at that
//       same seed by Prepare. Both are >= SURFEL_TTL, so a single `Age >= SURFEL_TTL` test culls both, matching SurfelAge.comp's own liveness gate.

#version 450

#include "SurfelRecord.glsl"
#include "SurfelGrid.glsl"

layout(std430, set = 0, binding = 0) readonly buffer SurfelBuffer  { SurfelRecord Surfels[]; };
layout(std430, set = 0, binding = 1) readonly buffer OffsetsBuffer { int CellOffsets[]; };   // per-cell END offsets (scanned); occupancy = end - prevEnd

layout(push_constant) uniform SurfelDebugConstants
{
    mat4  ViewProjection;      // [-]  - world -> clip (column-major)
    vec4  CameraPosition;      // [m]  - raw eye; drives the surfel radius (matches the grid's eye-distance radius)
    vec4  GridOrigin;          // [m]  - snapped grid origin the slotting used; recovers the eye-relative position for the cascade
    vec4  ScreenAndRadius;     // [-]  - x width px, y height px, z disc-radius scale, w unused
    uint  DebugMode;           // [-]  - 0 off, 1 age, 2 cascade, 3 identity, 4 cell-occupancy heatmap
    uint  Capacity;            // [-]  - pool capacity (the draw's vertex count); slots past PoolMax are dead and culled
    uint  Pad0;
    uint  Pad1;
} Debug;

layout(location = 0) out vec4  SurfelColour;   // resolved disc colour (flat — one colour per point)
layout(location = 1) out float SurfelFade;     // 1 for a live surfel, 0 to fully discard in the fragment stage

// A recognizable, well-separated colour per integer key (golden-ratio hue walk), for the identity / cascade modes.
vec3 DebugHue(uint Key)
{
    float Hue = fract(float(Key) * 0.61803398875);
    vec3  Wrapped = abs(fract(Hue + vec3(0.0, 1.0 / 3.0, 2.0 / 3.0)) * 6.0 - 3.0);
    return clamp(Wrapped - 1.0, 0.0, 1.0);
}

// Blue -> cyan -> green -> yellow -> red ramp for the scalar (age / occupancy) modes.
vec3 DebugHeat(float T)
{
    T = clamp(T, 0.0, 1.0);
    vec3 Cold = mix(vec3(0.1, 0.2, 0.9), vec3(0.1, 0.9, 0.4), clamp(T * 2.0, 0.0, 1.0));
    vec3 Hot  = mix(vec3(0.9, 0.9, 0.1), vec3(0.95, 0.15, 0.1), clamp(T * 2.0 - 1.0, 0.0, 1.0));
    return mix(Cold, Hot, step(0.5, T));
}

void main()
{
    uint Index = uint(gl_VertexIndex);

    // Read the record. A slot past capacity cannot occur (the draw is exactly Capacity vertices), but guard anyway so a stale count never over-reads.
    SurfelRecord Surfel = Surfels[min(Index, Debug.Capacity - 1u)];
    int  Age            = Surfel.Age;

    // Dead / never-spawned cull: both read >= TTL (see the header). Emit a degenerate off-screen point and tell the fragment stage to discard it.
    if (Index >= Debug.Capacity || Age >= SURFEL_TTL || Age < 0)
    {
        gl_Position   = vec4(2.0, 2.0, 2.0, 1.0);   // outside the clip cube -> clipped away
        gl_PointSize  = 0.0;
        SurfelColour  = vec4(0.0);
        SurfelFade    = 0.0;
        return;
    }

    vec3 WorldPosition = Surfel.PositionAndSpare.xyz;

    // Project to clip. A surfel behind the eye (w <= 0) is culled the same way.
    vec4 Clip = Debug.ViewProjection * vec4(WorldPosition, 1.0);
    if (Clip.w <= 0.0)
    {
        gl_Position  = vec4(2.0, 2.0, 2.0, 1.0);
        gl_PointSize = 0.0;
        SurfelColour = vec4(0.0);
        SurfelFade   = 0.0;
        return;
    }

    // World disc radius -> screen pixels. The radius model is the grid's own eye-distance radius, so the disc grows exactly as the surfel's cell does.
    // The projected pixel size is (worldRadius / clip.w) scaled by half the viewport width — the standard perspective point-size projection.
    float WorldRadius  = SurfelRadiusForPositionEye(WorldPosition, Debug.CameraPosition.xyz);
    float PixelRadius  = (WorldRadius / Clip.w) * (Debug.ScreenAndRadius.x * 0.5) * Debug.ScreenAndRadius.z;
    gl_PointSize       = clamp(PixelRadius * 2.0, 2.0, 64.0);   // diameter; floored so a far surfel stays a visible dot, capped so a near one never floods

    gl_Position = Clip;

    // --- resolve the disc colour by mode -------------------------------------------------------------------------------
    vec4 Colour = vec4(1.0);

    if (Debug.DebugMode == 1u)
    {
        // Age: young -> cold, old -> hot, so a surfel about to recycle reads red.
        Colour = vec4(DebugHeat(float(Age) / float(SURFEL_TTL)), 0.9);
    }
    else if (Debug.DebugMode == 2u)
    {
        // Cascade: the eye-relative cascade this surfel falls in, one distinct hue per level.
        vec3  Relative = WorldPosition - Debug.GridOrigin.xyz;
        ivec3 Coord    = SurfelPositionToGridCoord(Relative);
        uint  Cascade  = SurfelCascadeFloatToCascade(SurfelGridCoordToCascadeFloat(Coord));
        Colour = vec4(DebugHue(Cascade * 977u + 13u), 0.9);
    }
    else if (Debug.DebugMode == 3u)
    {
        // Identity: one hue per surfel slot, so neighbouring discs are visibly distinct and coverage density is legible.
        Colour = vec4(DebugHue(Index), 0.9);
    }
    else if (Debug.DebugMode == 4u)
    {
        // Cell occupancy: how full this surfel's grid cell is, end - prevEnd on the scanned Offsets header, normalized by the per-cell cap.
        vec3  Relative = WorldPosition - Debug.GridOrigin.xyz;
        ivec3 Coord    = SurfelPositionToGridCoord(Relative);
        uint  Hash     = SurfelHashOfCoord(Coord);
        int   End      = CellOffsets[Hash + 1u];
        int   Start    = CellOffsets[Hash];
        float Fill     = float(End - Start) / float(SURFEL_MAX_SURFELS_PER_CELL);
        Colour = vec4(DebugHeat(Fill), 0.9);
    }

    SurfelColour = Colour;
    SurfelFade   = 1.0;
}
