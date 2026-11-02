#version 300 es
precision highp float;

in vec3 a_Position;
in vec3 a_Normal;
in vec3 a_Color;

uniform mat4 uVesselTransform;
uniform mat4 uViewProjection;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec3 vColor;

void main()
{
    vec4 World = uVesselTransform * vec4(a_Position, 1.0);

    vWorldPosition = World.xyz;
    vNormal = mat3(uVesselTransform) * a_Normal;
    vColor = a_Color;

    gl_Position = uViewProjection * World;
}
