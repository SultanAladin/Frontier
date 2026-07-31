// ==============================================================================================================================================
//                                                        CLIPMAPDEBUGLATTICE.FRAG
// ==============================================================================================================================================
// Development-only clipmap lattice fragment step: passes the interpolated per-cell line colour straight through. All the category / level
// shading happened per-vertex — a line has no area to shade, so nothing further is computed here.

#version 450

layout(location = 0) in  vec4 FragmentColour;
layout(location = 0) out vec4 SurfaceColour;

void main()
{
    SurfaceColour = FragmentColour;
}
