#version 440

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vColor;

layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform Ubo {
    mat4 mvp;
    vec4 lightDir;
} ubo;

void main()
{
    // Double-sided diffuse term (abs): the surface is an open sheet, so back
    // faces should light too rather than going black.
    vec3 n = normalize(vNormal);
    float diffuse = abs(dot(n, normalize(ubo.lightDir.xyz)));
    float shade = clamp(0.4 + 0.6 * diffuse, 0.0, 1.0);
    fragColor = vec4(vColor * shade, 1.0);
}
