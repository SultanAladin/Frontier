//============================================================================================================================================
//                                                      CLUSTERDEBUGFALLBACK.WGSL
//============================================================================================================================================
// 🧩 Placeholder written into the display inspection target when a cluster debug view (Display modes
//    10-13) is selected but the virtualized runtime has no resident geometry to show yet — programs
//    still compiling, or no facet set in the scene is eligible for virtualization. A magenta checker
//    is unmistakable next to a genuinely black image, so "no data" never masquerades as a dead view.

@group(0) @binding(0) var Target : texture_storage_2d<rgba16float, write>;

@compute @workgroup_size(8, 8)
fn DebugUnavailable(@builtin(global_invocation_id) ID : vec3u)
{
    let Extent = textureDimensions(Target);
    if (ID.x >= Extent.x || ID.y >= Extent.y) { return; }
    let Checker = ((ID.x / 24u) + (ID.y / 24u)) % 2u == 0u;
    let Colour = select(vec3f(0.10, 0.02, 0.14), vec3f(0.62, 0.08, 0.52), Checker);
    textureStore(Target, vec2i(ID.xy), vec4f(Colour, 1.0));
}
