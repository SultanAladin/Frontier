#version 300 es
precision highp float;

out vec2 vScreenCoordinate;

void main()
{
    vec2 Corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vScreenCoordinate = Corner;
    gl_Position = vec4(Corner * 2.0 - 1.0, 0.0, 1.0);
}
