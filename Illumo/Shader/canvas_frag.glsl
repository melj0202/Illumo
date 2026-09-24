#version 330 core
// Location 1 matches the sprite shader's, for the motion-blur geometry pass.
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec2 FragVelocity;

in vec2 TexCoord;

// Faded display colors (RGB), one texel per cell at the exact LOD. Domain
// state stays on the CPU as lifeCanvas.
uniform sampler2D uTexture;

// Up close every cell is drawn as a softly domed tile: a thin groove between
// neighbours, a rim that glows in the cell's own colour, a faint world-fixed
// grain, and light that spills from brighter cells onto darker neighbours
// (so bright cells halo over any background without knowing its colour).
// Far out, once a cell spans only a few pixels (and at the density
// overviews), it fades back to the flat texel.

vec3 cellAt(ivec2 cell, ivec2 size)
{
	return texelFetch(uTexture, clamp(cell, ivec2(0), size - 1), 0).rgb;
}

float hash12(vec2 p)
{
	vec3 p3 = fract(vec3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p)
{
	vec2 i = floor(p);
	vec2 f = fract(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = hash12(i);
	float b = hash12(i + vec2(1.0, 0.0));
	float c = hash12(i + vec2(0.0, 1.0));
	float d = hash12(i + vec2(1.0, 1.0));
	return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

// Signed distance to a rounded box centred on the origin (negative inside).
float roundedBox(vec2 p, vec2 halfSize, float radius)
{
	vec2 q = abs(p) - halfSize + radius;
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
}

void main()
{
	FragVelocity = vec2(0.0);
	ivec2 size = textureSize(uTexture, 0);
	vec2 cellPos = TexCoord * vec2(size);
	ivec2 cell = ivec2(floor(cellPos));
	vec3 own = cellAt(cell, size);

	// Screen pixels per cell. Derivatives are taken before any branch.
	vec2 perPixel = fwidth(cellPos);
	float cellPixels = 1.0 / max(max(perPixel.x, perPixel.y), 1e-5);
	float detail = smoothstep(4.0, 12.0, cellPixels);
	if (detail <= 0.0) {
		FragColor = vec4(own, 1.0);
		return;
	}

	vec2 local = fract(cellPos) - 0.5;
	// The groove stays about a pixel wide on small cells and a thin line on
	// large ones; edges are antialiased over a pixel.
	float groove = max(0.04, 0.9 / cellPixels);
	float d = roundedBox(local, vec2(0.5 - groove), 0.2);
	float edgeWidth = 0.75 / cellPixels;
	float face = 1.0 - smoothstep(-edgeWidth, edgeWidth, d);

	// 0 on the tile's edge, 1 a quarter of a cell inside it.
	float depth = clamp(-d / 0.25, 0.0, 1.0);
	float rim = (1.0 - depth) * (1.0 - depth);
	// A slightly deeper face with a rim lit in the cell's own colour.
	vec3 tile = own * (0.86 + 0.08 * depth) + own * rim * 0.5;
	// World-fixed grain: two octaves, a few percent either way.
	float grain =
	  valueNoise(cellPos * 5.0) * 0.6 + valueNoise(cellPos * 13.0) * 0.4;
	tile *= 0.96 + 0.08 * grain;

	// One pass over the 3x3 neighbourhood. Each cell's weight falls off with
	// the distance to its square, which is continuous across cell borders, so
	// the groove colour (a darkened blend of the cells around it) has no
	// seams where two colours meet. Tile faces also take light from brighter
	// neighbours, strongest at the shared edge.
	vec3 field = vec3(0.0);
	float fieldWeight = 0.0;
	vec3 spill = vec3(0.0);
	for (int y = -1; y <= 1; ++y) {
		for (int x = -1; x <= 1; ++x) {
			vec3 neighbour = cellAt(cell + ivec2(x, y), size);
			vec2 outside = abs(local - vec2(float(x), float(y))) - 0.5;
			float away = length(max(outside, 0.0));
			float weight = exp(-away * 6.0);
			field += neighbour * weight;
			fieldWeight += weight;
			if (x != 0 || y != 0) {
				spill += max(neighbour - own, 0.0) * exp(-away * 7.0);
			}
		}
	}
	vec3 grooveColor = field / fieldWeight * 0.42;
	vec3 styled = mix(grooveColor, tile + spill * 0.3, face);

	FragColor = vec4(clamp(mix(own, styled, detail), 0.0, 1.0), 1.0);
}
