#version 300 es
precision lowp float;

out vec4 FragColor;

// 🧩 WebGL2 requires both stages even when RASTERIZER_DISCARD is enabled; this fragment never runs
void main()
{
    FragColor = vec4(0.0, 0.0, 0.0, 1.0);
}
