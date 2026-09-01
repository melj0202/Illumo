#version 330 core

layout (location = 0) in vec3 aPos;

uniform mat4 uMVP;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    // Push caster depth slightly away from the light so receivers do not
    // self-shadow from rasterization precision.
    gl_Position.z += 0.002 * gl_Position.w;
}
