#version 330 core

// shadow_depth_vertex.glsl for instanced draws: the model matrix is a
// per-instance attribute and the light-space matrix comes from FrameUniforms.

layout (location = 0) in vec3 aPos;
layout (location = 4) in mat4 aModel;

layout (std140) uniform FrameUniforms
{
    mat4 uViewProjection;
    mat4 uPreviousViewProjection;
    mat4 uLightSpace;
    vec4 uShadowState;
};

void main()
{
    gl_Position = uLightSpace * (aModel * vec4(aPos, 1.0));
    // Same caster depth push as shadow_depth_vertex.glsl.
    gl_Position.z += 0.002 * gl_Position.w;
}
