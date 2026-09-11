#version 330 core
in vec3 vTexCoords;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec2 FragVelocity;

uniform samplerCube uSkybox;
uniform vec4 uTint;

void main() {
    FragColor = texture(uSkybox, vTexCoords) * uTint;
    FragVelocity = vec2(0.0);
}
