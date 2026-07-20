#version 440

// Height-map surface vertex: model-space position, precomputed normal, and a
// per-vertex colormap colour (sampled on the CPU so no LUT texture is needed).
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 color;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vColor;

layout(std140, binding = 0) uniform Ubo {
    mat4 mvp;
    vec4 lightDir;   // xyz = world-space direction toward the light
} ubo;

void main()
{
    vNormal = normal;
    vColor = color;
    gl_Position = ubo.mvp * vec4(position, 1.0);
}
