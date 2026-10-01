#version 330 core

// mesh_lit_vertex.glsl for instanced draws: the model matrices and tint are
// per-instance attributes, and the camera comes from the FrameUniforms block.

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aNormal;
layout (location = 4) in mat4 aModel;
layout (location = 8) in mat4 aPreviousModel;
layout (location = 12) in vec4 aTint;

layout (std140) uniform FrameUniforms
{
    mat4 uViewProjection;
    mat4 uPreviousViewProjection;
    mat4 uLightSpace;
    vec4 uShadowState;
};

out vec3 vFragPos;
out vec3 vNormal;
out vec4 vColor;
out vec2 vTexCoord;
out vec4 vCurrentClip;
out vec4 vPrevClip;

void main()
{
    vec4 worldPos = aModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(aModel)));
    vNormal = normalize(normalMatrix * aNormal);

    vColor = aColor * aTint;
    vTexCoord = aTexCoord;

    vec4 currentClip = uViewProjection * worldPos;
    vCurrentClip = currentClip;
    vPrevClip = uPreviousViewProjection * (aPreviousModel * vec4(aPos, 1.0));
    gl_Position = currentClip;
}
