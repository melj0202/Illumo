#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aNormal;

out vec3 vFragPos;
out vec3 vNormal;
out vec4 vColor;
out vec2 vTexCoord;
out vec4 vCurrentClip;
out vec4 vPrevClip;

uniform mat4 uMVP;
uniform mat4 uPrevMVP;
uniform mat4 uModel;
uniform int uMotionBlurEnabled;
uniform float uMotionBlurAmount;
uniform float uMotionBlurMax;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vNormal = normalize(normalMatrix * aNormal);

    vColor = aColor;
    vTexCoord = aTexCoord;

    vec4 currentClip = uMVP * vec4(aPos, 1.0);
    vCurrentClip = currentClip;
    vPrevClip = uPrevMVP * vec4(aPos, 1.0);
    gl_Position = currentClip;
}
