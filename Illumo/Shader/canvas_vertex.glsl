#version 330 core
layout (location = 0) in vec3 aPos;
// The cell look, constant across the quad: glow / 2, LED keys (1) or flat
// pixels (0), and grid lines (1) or none (0).
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec2 aTexCoord;

out vec2 TexCoord;
flat out vec3 vLook;

uniform mat4 uMVP;

void main()
{
	gl_Position = uMVP * vec4(aPos, 1.0);
	TexCoord = aTexCoord;
	vLook = aColor;
}
