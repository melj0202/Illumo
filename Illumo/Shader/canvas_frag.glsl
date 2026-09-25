#version 330 core
// Location 1 matches the sprite shader's, for the motion-blur geometry pass.
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec2 FragVelocity;

in vec2 TexCoord;
// The player's cell look (see canvas_vertex.glsl): glow / 2, LED keys amount
// (0 draws flat pixels up close too) and grid lines.
flat in vec3 vLook;

// Faded display colors (RGB), one texel per cell at the exact LOD. Domain
// state stays on the CPU as lifeCanvas.
uniform sampler2D uTexture;

// Up close every cell is an LED in a matrix: a raised rounded-square key set
// in a dark housing. A lit key has a hot core that runs toward white and falls
// off to its edges, a bevel lit from the upper left and a drop shadow onto the
// housing; very close, the emitter chip shows at its centre. Each lit LED throws light into the housing around it and onto
// darker neighbours. "Lit" is judged by
// brightness, so it needs no knowledge of the ruleset's background colour.
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

// How strongly a colour reads as a lit LED, 0 (off) to 1 (fully lit).
float litness(vec3 color)
{
	return smoothstep(0.08, 0.55, dot(color, vec3(0.2126, 0.7152, 0.0722)));
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
	float glowScale = vLook.r * 2.0;
	float detail = smoothstep(4.0, 12.0, cellPixels) * vLook.g;
	// Grid lines fade in once a cell is big enough to frame.
	float gridShow = vLook.b * smoothstep(6.0, 14.0, cellPixels);
	// Cell-local position, -0.5..0.5; y grows downward on screen.
	vec2 local = fract(cellPos) - 0.5;
	// A one-pixel dark line on every cell border.
	float borderPixels = (0.5 - max(abs(local.x), abs(local.y))) * cellPixels;
	float gridLine = gridShow * (1.0 - smoothstep(0.0, 1.0, borderPixels));
	if (detail <= 0.0) {
		FragColor = vec4(mix(own, own * 0.3, gridLine * 0.8), 1.0);
		return;
	}

	float edgeWidth = 0.75 / cellPixels;
	// Each LED is a raised rounded-square key.
	const vec2 kKeyHalf = vec2(0.43);
	const float kKeyCorner = 0.2;
	float d = roundedBox(local, kKeyHalf, kKeyCorner);
	float key = 1.0 - smoothstep(-edgeWidth, edgeWidth, d);
	float ownLit = litness(own);

	// The key's face: a hot core that runs toward white on a lit LED and
	// falls off toward the edges; an unlit LED is dark tinted glass.
	// A squircle distance keeps the falloff smooth, without the diagonal
	// creases the box distance would leave across the face.
	vec2 spread = abs(local) / kKeyHalf;
	vec2 spread2 = spread * spread;
	float squircle = sqrt(sqrt(dot(spread2, spread2)));
	float core = 1.0 - smoothstep(0.1, 1.0, squircle);
	vec3 hot = mix(own, vec3(1.0), 0.45 * ownLit);
	vec3 face = mix(own * (0.8 + 0.06 * (1.0 - ownLit)), hot * 1.04, core);
	// A bevel around the face, lit from the upper left like a raised key: the
	// upper and left edges catch the light, the lower and right fall in shade.
	const float kBevel = 0.07;
	float bevel = 1.0 - smoothstep(0.0, kBevel, -d);
	vec2 probe = vec2(0.01, 0.0);
	vec2 normal = normalize(
	  vec2(roundedBox(local + probe.xy, kKeyHalf, kKeyCorner) -
	         roundedBox(local - probe.xy, kKeyHalf, kKeyCorner),
	       roundedBox(local + probe.yx, kKeyHalf, kKeyCorner) -
	         roundedBox(local - probe.yx, kKeyHalf, kKeyCorner)) +
	  vec2(1e-6));
	float facing = dot(normal, vec2(-0.70710678));
	face *= 1.0 + bevel * facing * (0.16 + 0.06 * ownLit);
	face += bevel * max(facing, 0.0) * 0.05 * mix(vec3(1.0), own, 0.5);
	// Very close, the emitter chip shows as a tiny bright square.
	float chipShow = smoothstep(18.0, 40.0, cellPixels);
	float chipEdge = 1.0 / cellPixels;
	float chip =
	  1.0 - smoothstep(0.06 - chipEdge,
	                   0.06 + chipEdge,
	                   max(abs(local.x), abs(local.y)));
	face = mix(face, hot * 1.2, chip * chipShow * ownLit * 0.55);

	// One pass over the 3x3 neighbourhood. Each cell's weight falls off with
	// the distance to its square, which is continuous across cell borders, so
	// the housing colour (a darkened blend of the cells around it) has no
	// seams where two colours meet. Every lit LED also throws light from its
	// key into the housing around it, and brighter neighbours bleed onto
	// darker keys, strongest at the shared edge.
	vec3 field = vec3(0.0);
	float fieldWeight = 0.0;
	vec3 glow = vec3(0.0);
	vec3 spill = vec3(0.0);
	for (int y = -1; y <= 1; ++y) {
		for (int x = -1; x <= 1; ++x) {
			vec3 neighbour = cellAt(cell + ivec2(x, y), size);
			vec2 offset = local - vec2(float(x), float(y));
			vec2 outside = abs(offset) - 0.5;
			float away = length(max(outside, 0.0));
			float weight = exp(-away * 6.0);
			field += neighbour * weight;
			fieldWeight += weight;
			float fromKey = max(roundedBox(offset, kKeyHalf, kKeyCorner), 0.0);
			glow += neighbour * litness(neighbour) * exp(-fromKey * 5.5);
			if (x != 0 || y != 0) {
				spill += max(neighbour - own, 0.0) * exp(-fromKey * 3.2);
			}
		}
	}
	// World-fixed grain gives the housing a moulded-plastic texture, and the
	// raised key casts a soft shadow down and to the right onto it.
	float grain =
	  valueNoise(cellPos * 5.0) * 0.6 + valueNoise(cellPos * 13.0) * 0.4;
	vec3 housing = field / fieldWeight * 0.26 * (0.92 + 0.16 * grain);
	float shadowD =
	  roundedBox(local - vec2(0.025, 0.03), kKeyHalf, kKeyCorner);
	float shadow = 1.0 - smoothstep(-0.02, 0.05, shadowD);
	housing *= 1.0 - 0.3 * shadow;
	housing += glow * 0.42 * glowScale;
	vec3 styled = mix(housing, face + spill * 0.34 * glowScale, key);

	vec3 color = clamp(mix(own, styled, detail), 0.0, 1.0);
	FragColor = vec4(mix(color, color * 0.3, gridLine * 0.8), 1.0);
}
