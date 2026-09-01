#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aTexCoord;
layout (location = 3) in vec3 aNormal;

out vec3 vFragPos;
out vec3 vNormal;
out vec4 vColor;
out vec2 vTexCoord;

uniform mat4 uMVP;
uniform mat4 uModel;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vFragPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(uModel)));
    vNormal = normalize(normalMatrix * aNormal);

    vColor = aColor;
    vTexCoord = aTexCoord;

    gl_Position = uMVP * vec4(aPos, 1.0);
}
