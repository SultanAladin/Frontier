// ============================================================================================================================================
//                                                       PARAMETRICSKETCHMATCAP.FRAG
// ============================================================================================================================================
// 🧩 Fragment stage of the parametric-sketch chrome-matcap surface inscription — the ONLY lighting model the preview has. Takes the interpolated
//    camera-space normal, folds it to the front hemisphere (so back-facing shading reuses the front of the disc), maps it into the matcap disc's
//    UV square (x right, y up — hence the -y flip), and reads the chrome image bound at set 1. No lights, no GI, no PBR: the material-capture
//    texture already bakes the whole reflectance into a hemisphere lookup, which is exactly what a fast solid preview wants.
#version 450

layout(set = 1, binding = 0) uniform sampler2D MatcapTexture;

layout(location = 0) in vec3 FragViewNormal;

layout(location = 0) out vec4 OutColor;

void main()
{
    vec3 Normal = normalize(FragViewNormal);
    if (Normal.z < 0.0) Normal = -Normal;
    vec2 DiscUv = vec2(Normal.x, -Normal.y) * 0.5 + 0.5;
    vec3 Chrome = texture(MatcapTexture, DiscUv).rgb;
    OutColor = vec4(Chrome, 1.0);
}
