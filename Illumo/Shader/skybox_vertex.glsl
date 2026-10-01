#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 vTexCoords;

uniform mat4 uViewProjection;

void main() {
    vTexCoords = aPos;
    vec4 pos = uViewProjection * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
