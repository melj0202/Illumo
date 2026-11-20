#version 330 core
#include <illumo/light_uniforms.glsl>

// shadow_depth_vertex.glsl for instanced draws: the model matrix is a
// per-instance attribute and the light-space matrix comes from FrameUniforms
// (single-map fallback) or LightUniforms (4-quadrant shadow atlas).

layout (location = 0) in vec3 aPos;
layout (location = 4) in mat4 aModel;

layout (std140) uniform FrameUniforms
{
    mat4 uViewProjection;
    mat4 uPreviousViewProjection;
    mat4 uLightSpace;
    vec4 uShadowState;
};

uniform int uShadowQuadrant;

void main()
{
    mat4 lightMatrix = (uLightCountsAndFlags.w & 4) != 0
        ? uAtlasLightMatrices[clamp(uShadowQuadrant, 0, 3)]
        : uLightSpace;
    gl_Position = lightMatrix * (aModel * vec4(aPos, 1.0));
}
