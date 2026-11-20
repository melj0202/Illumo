#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/IShaderProgram.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>

// Built-in style shader sources and enrollment (D-R14). Owned by Renderer;
// production drawables only bind styles and emit content tokens.

static const char* kUiTextVertexShader = R"(
#version 330 core
#include <illumo/screen_transform.glsl>
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
out vec4 ourColor;
uniform vec2 u_resolution;
uniform vec2 u_scale;
uniform vec2 u_position;
void main() {
    gl_Position = illumoScaledScreenToClip(aPos.xy, u_scale, u_position, u_resolution, aPos.z);
    ourColor = aColor;
}
)";

static const char* kUiTextFragmentShader = R"(
#version 330 core
in vec4 ourColor;
out vec4 FragColor;
void main() {
    FragColor = ourColor;
}
)";

// Console batch uses absolute pixel positions (no u_position offset).
static const char* kConsoleVertexShader = R"(
#version 330 core
#include <illumo/screen_transform.glsl>
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
out vec4 ourColor;
uniform vec2 u_resolution;
uniform vec2 u_scale;
void main() {
    gl_Position = illumoScaledScreenToClip(aPos.xy, u_scale, vec2(0.0), u_resolution, aPos.z);
    ourColor = aColor;
}
)";

static const char* kConsoleFragmentShader = R"(
#version 330 core
in vec4 ourColor;
out vec4 FragColor;
void main() {
    FragColor = ourColor;
}
)";

// Shape primitives: world or overlay via uMVP (D-R21).
static const char* kShapeVertexShader = R"(
#version 330 core
#include <illumo/common.glsl>
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
out vec4 ourColor;
out vec4 vCurrentClip;
out vec4 vPrevClip;
uniform mat4 uMVP;
uniform mat4 uPrevMVP;
uniform int uMotionBlurEnabled;
void main() {
    vec4 currentClip = uMVP * vec4(aPos, 1.0);
    gl_Position = currentClip;
    ourColor = aColor;
    vCurrentClip = currentClip;
    vPrevClip = (uMotionBlurEnabled != 0) ? (uPrevMVP * vec4(aPos, 1.0)) : currentClip;
}
)";

static const char* kShapeFragmentShader = R"(
#version 330 core
in vec4 ourColor;
in vec4 vCurrentClip;
in vec4 vPrevClip;
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec2 FragVelocity;
uniform int uMotionBlurEnabled;
void main() {
    FragColor = ourColor;
    if (uMotionBlurEnabled != 0) {
        vec2 currentNdc = vCurrentClip.xy / max(abs(vCurrentClip.w), 1e-5);
        vec2 prevNdc = vPrevClip.xy / max(abs(vPrevClip.w), 1e-5);
        FragVelocity = (currentNdc - prevNdc) * 0.5;
    } else {
        FragVelocity = vec2(0.0);
    }
}
)";

// Sprite primitives: same uMVP contract + texture sample with vertex tint.
static const char* kSpriteVertexShader = R"(
#version 330 core
#include <illumo/vertex_2d.glsl>
out vec4 ourColor;
out vec2 ourUv;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    ourColor = aColor;
    ourUv = aUv;
}
)";

static const char* kSpriteFragmentShader = R"(
#version 330 core
#include <illumo/sprite.glsl>
in vec4 ourColor;
in vec2 ourUv;
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec2 FragVelocity;
uniform sampler2D uTexture;
void main() {
    FragColor = sampleSprite(uTexture, ourUv, ourColor);
    FragVelocity = vec2(0.0);
}
)";

static const char* kMotionBlurVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;
layout (location = 2) in vec2 aTexCoord;

out vec2 vTexCoord;

void main() {
    gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char* kMotionBlurFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uColorTexture;
uniform sampler2D uVelocityTexture;
uniform float uMotionBlurAmount;
uniform float uMotionBlurMax;
uniform int uMotionBlurSamples;

void main() {
    vec2 velocity = texture(uVelocityTexture, vTexCoord).rg * uMotionBlurAmount;
    float speed = length(velocity);
    float maxSpeed = max(uMotionBlurMax, 0.0);
    if (speed > maxSpeed && speed > 0.0) {
        velocity = (velocity / speed) * maxSpeed;
    }

    int samples = (uMotionBlurSamples <= 0) ? 8 : min(uMotionBlurSamples, 16);
    if (speed < 0.0001 || samples <= 1) {
        FragColor = texture(uColorTexture, vTexCoord);
        return;
    }

    vec4 color = vec4(0.0);
    for (int i = 0; i < samples; ++i) {
        float t = float(i) / float(samples - 1) - 0.5;
        vec2 offset = velocity * t;
        color += texture(uColorTexture, vTexCoord + offset);
    }
    FragColor = color / float(samples);
}
)";

static const char* kSkyboxVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 vTexCoords;

uniform mat4 uViewProjection;

void main() {
    vTexCoords = aPos;
    vec4 pos = uViewProjection * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
)";

static const char* kSkyboxFragmentShader = R"(
#version 330 core
#include <illumo/pbr_lighting.glsl>
in vec3 vTexCoords;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec2 FragVelocity;

uniform samplerCube uSkybox;
uniform vec4 uTint;

void main() {
    vec4 sampled = texture(uSkybox, vTexCoords);
    if ((uLightCountsAndFlags.w & 2) != 0) {
        vec3 lin = illumoSrgbToLinear(sampled.rgb) * illumoSrgbToLinear(uTint.rgb);
        FragColor = vec4(max(lin, vec3(0.0)), sampled.a * uTint.a);
    } else {
        FragColor = sampled * uTint;
    }
    FragVelocity = vec2(0.0);
}
)";

static const char* kGroundPlaneVertexShader = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

out vec3 vWorldDir;

uniform mat4 uViewProjection;

void main() {
    vWorldDir = aPos;
    vec4 pos = uViewProjection * vec4(aPos, 1.0);
    gl_Position = pos.xyww;
}
)";

static const char* kGroundPlaneFragmentShader = R"(
#version 330 core
#include <illumo/pbr_lighting.glsl>
in vec3 vWorldDir;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec2 FragVelocity;

uniform mat4 uFullViewProjection;
uniform vec3 uCameraPos;
uniform float uPlaneY;
uniform float uCheckSize;
uniform vec4 uColor0;
uniform vec4 uColor1;

uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbientColor;
uniform sampler2D uShadowMap;
uniform mat4 uLightSpaceMatrix;
uniform int uShadowsEnabled;
uniform float uShadowBias;
uniform float uShadowSlopeScale;
uniform float uShadowNormalOffset;
uniform int uShadowPcf;

void main() {
    vec3 dir = normalize(vWorldDir);
    float dy = dir.y;
    float deltaY = uPlaneY - uCameraPos.y;

    if (abs(dy) < 1e-6) {
        discard;
    }
    float t = deltaY / dy;
    if (t <= 0.0) {
        discard;
    }

    vec3 fragPos = uCameraPos + t * dir;

    vec4 clipPos = uFullViewProjection * vec4(fragPos, 1.0);
    if (clipPos.w <= 0.0) {
        discard;
    }
    float ndcZ = clipPos.z / clipPos.w;
    if (ndcZ < -1.0 || ndcZ > 1.0) {
        discard;
    }
    gl_FragDepth = ndcZ * 0.5 + 0.5;

    vec2 p = fragPos.xz / max(uCheckSize, 0.0001);
    vec2 w = fwidth(p);
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5) -
                    abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / max(w, 0.00001);
    float check = clamp(0.5 - 0.5 * i.x * i.y, 0.0, 1.0);
    vec4 baseColor = mix(uColor0, uColor1, check);

    vec3 norm = (deltaY < 0.0) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, -1.0, 0.0);
    float lLenSq = dot(uLightDir, uLightDir);
    vec3 lightDir = lLenSq > 1e-8 ? (uLightDir * inversesqrt(lLenSq)) : vec3(0.0, 1.0, 0.0);
    float shadow = uShadowsEnabled != 0
        ? illumoCalculateDirectionalShadow(
            uShadowMap,
            fragPos,
            norm,
            lightDir,
            clipPos.w,
            uLightSpaceMatrix,
            uShadowBias,
            uShadowSlopeScale,
            uShadowNormalOffset,
            uShadowPcf)
        : 0.0;

    int punctualCount = clamp(uLightCountsAndFlags.x, 0, 64);
    bool hdrTarget = (uLightCountsAndFlags.w & 2) != 0;
    if (!hdrTarget && punctualCount == 0) {
        float diff = max(dot(norm, lightDir), 0.0);
        vec3 lighting = uAmbientColor + (1.0 - shadow) * diff * uLightColor;
        FragColor = vec4(lighting * baseColor.rgb, baseColor.a);
        FragVelocity = vec2(0.0);
        return;
    }

    vec3 albedo = illumoSrgbToLinear(baseColor.rgb);
    vec3 V = normalize(uCameraPos - fragPos);
    vec3 radiance = max(uAmbientColor, vec3(0.0)) * albedo;
    radiance += (1.0 - shadow) * illumoEvaluateDirectLight(
        norm, V, lightDir, max(uLightColor, vec3(0.0)), albedo, 0.0, 0.9);
    for (int li = 0; li < punctualCount; ++li) {
        IllumoPunctualLight light = uPunctualLights[li];
        vec3 toLight = light.positionAndInvRange.xyz - fragPos;
        float distSq = dot(toLight, toLight);
        float invRange = light.positionAndInvRange.w;
        float rangeRatio2 = distSq * (invRange * invRange);
        if (rangeRatio2 >= 1.0) {
            continue;
        }
        float dist = sqrt(max(distSq, 1e-8));
        vec3 L = dist > 1e-4 ? (toLight / dist) : vec3(0.0, 1.0, 0.0);
        float window = max(1.0 - rangeRatio2 * rangeRatio2, 0.0);
        float attenuation = (window * window) / max(distSq, 0.01);
        float outerCos = light.radianceAndOuterCos.w;
        if (outerCos > -1.5) {
            float cd = dot(-L, light.directionAndAngleScale.xyz);
            float spot = clamp((cd - outerCos) * light.directionAndAngleScale.w, 0.0, 1.0);
            attenuation *= spot * spot;
        }
        if (attenuation > 0.0) {
            float spotShadow = 0.0;
            if (uShadowsEnabled != 0 && (uLightCountsAndFlags.w & 4) != 0 &&
                li == uLightCountsAndFlags.z) {
                float spotNDotL = dot(norm, L);
                spotShadow = spotNDotL > 0.0
                    ? illumoSampleShadowQuadrant(
                        uShadowMap,
                        fragPos,
                        norm,
                        spotNDotL,
                        uAtlasLightMatrices[3],
                        3,
                        uShadowBias,
                        uShadowSlopeScale,
                        uShadowNormalOffset,
                        uShadowPcf,
                        true)
                    : 1.0;
            }
            if (spotShadow < 1.0) {
                radiance += (1.0 - spotShadow) * illumoEvaluateDirectLight(
                    norm,
                    V,
                    L,
                    light.radianceAndOuterCos.rgb * attenuation,
                    albedo,
                    0.0,
                    0.9);
            }
        }
    }
    vec3 outRgb = hdrTarget
        ? max(radiance, vec3(0.0))
        : illumoLinearToSrgb(clamp(radiance, 0.0, 1.0));
    FragColor = vec4(outRgb, baseColor.a);
    FragVelocity = vec2(0.0);
}
)";

// One fragment per cell of a canvas fade texture (frame schema v9, D-R31):
// the fade's start colour at 2x, its target at 2x + 1, their alpha bytes the
// start time in 1/512 s ticks wrapping every 128 s. Mirrors canvas_frag.glsl.
static const char* kCanvasFadeFragmentShader = R"(
#version 330 core
out vec4 FragColor;

uniform sampler2D uTexture;
// x: the fade clock in seconds, y: the fade speed.
uniform vec3 uCanvasFade;

void main() {
    ivec2 cell = ivec2(gl_FragCoord.xy);
    vec4 from = texelFetch(uTexture, ivec2(cell.x * 2, cell.y), 0);
    vec4 to = texelFetch(uTexture, ivec2(cell.x * 2 + 1, cell.y), 0);
    float ticks = floor(from.a * 255.0 + 0.5) + floor(to.a * 255.0 + 0.5) * 256.0;
    float age = mod(uCanvasFade.x - ticks / 512.0 + 128.0, 128.0);
    // A start time a rounding step ahead of the clock: just begun.
    if (age > 127.0) {
        age = 0.0;
    }
    FragColor = vec4(mix(to.rgb, from.rgb, exp(-uCanvasFade.y * age)), 1.0);
}
)";

static const char* kBloomDownsampleFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSourceTexture;
uniform int uKarisAverage;

vec3 sanitizeHdr(vec3 c) {
    bvec3 invalid = bvec3(
        isnan(c.r) || isinf(c.r),
        isnan(c.g) || isinf(c.g),
        isnan(c.b) || isinf(c.b));
    vec3 safe = mix(c, vec3(0.0), invalid);
    return clamp(safe, vec3(0.0), vec3(65504.0));
}

float karisWeight(vec3 box) {
    float luma = dot(box, vec3(0.2126, 0.7152, 0.0722));
    return 1.0 / (1.0 + max(luma, 0.0));
}

void main() {
    vec2 texel = 1.0 / vec2(textureSize(uSourceTexture, 0));
    float x = texel.x;
    float y = texel.y;
    vec2 uv = vTexCoord;

    vec3 a = sanitizeHdr(texture(uSourceTexture, vec2(uv.x - 2.0 * x, uv.y + 2.0 * y)).rgb);
    vec3 b = sanitizeHdr(texture(uSourceTexture, vec2(uv.x,           uv.y + 2.0 * y)).rgb);
    vec3 c = sanitizeHdr(texture(uSourceTexture, vec2(uv.x + 2.0 * x, uv.y + 2.0 * y)).rgb);

    vec3 d = sanitizeHdr(texture(uSourceTexture, vec2(uv.x - 2.0 * x, uv.y)).rgb);
    vec3 e = sanitizeHdr(texture(uSourceTexture, vec2(uv.x,           uv.y)).rgb);
    vec3 f = sanitizeHdr(texture(uSourceTexture, vec2(uv.x + 2.0 * x, uv.y)).rgb);

    vec3 g = sanitizeHdr(texture(uSourceTexture, vec2(uv.x - 2.0 * x, uv.y - 2.0 * y)).rgb);
    vec3 h = sanitizeHdr(texture(uSourceTexture, vec2(uv.x,           uv.y - 2.0 * y)).rgb);
    vec3 i = sanitizeHdr(texture(uSourceTexture, vec2(uv.x + 2.0 * x, uv.y - 2.0 * y)).rgb);

    vec3 j = sanitizeHdr(texture(uSourceTexture, vec2(uv.x - x, uv.y + y)).rgb);
    vec3 k = sanitizeHdr(texture(uSourceTexture, vec2(uv.x + x, uv.y + y)).rgb);
    vec3 l = sanitizeHdr(texture(uSourceTexture, vec2(uv.x - x, uv.y - y)).rgb);
    vec3 m = sanitizeHdr(texture(uSourceTexture, vec2(uv.x + x, uv.y - y)).rgb);

    vec3 result = vec3(0.0);
    if (uKarisAverage != 0) {
        vec3 g0 = (a + b + d + e) * 0.25;
        vec3 g1 = (b + c + e + f) * 0.25;
        vec3 g2 = (d + e + g + h) * 0.25;
        vec3 g3 = (e + f + h + i) * 0.25;
        vec3 g4 = (j + k + l + m) * 0.25;
        float w0 = 0.125 * karisWeight(g0);
        float w1 = 0.125 * karisWeight(g1);
        float w2 = 0.125 * karisWeight(g2);
        float w3 = 0.125 * karisWeight(g3);
        float w4 = 0.5 * karisWeight(g4);
        float wSum = max(w0 + w1 + w2 + w3 + w4, 1e-5);
        result = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4) / wSum;
    } else {
        result = e * 0.125 +
                 (a + c + g + i) * 0.03125 +
                 (b + d + f + h) * 0.0625 +
                 (j + k + l + m) * 0.125;
    }
    FragColor = vec4(max(result, vec3(0.0)), 1.0);
}
)";

static const char* kBloomUpsampleFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uSourceTexture;
uniform float uFilterRadius;

void main() {
    vec2 texel = (1.0 / vec2(textureSize(uSourceTexture, 0))) * max(uFilterRadius, 0.25);
    float x = texel.x;
    float y = texel.y;
    vec2 uv = vTexCoord;

    vec3 a = texture(uSourceTexture, vec2(uv.x - x, uv.y + y)).rgb;
    vec3 b = texture(uSourceTexture, vec2(uv.x,     uv.y + y)).rgb;
    vec3 c = texture(uSourceTexture, vec2(uv.x + x, uv.y + y)).rgb;

    vec3 d = texture(uSourceTexture, vec2(uv.x - x, uv.y)).rgb;
    vec3 e = texture(uSourceTexture, vec2(uv.x,     uv.y)).rgb;
    vec3 f = texture(uSourceTexture, vec2(uv.x + x, uv.y)).rgb;

    vec3 g = texture(uSourceTexture, vec2(uv.x - x, uv.y - y)).rgb;
    vec3 h = texture(uSourceTexture, vec2(uv.x,     uv.y - y)).rgb;
    vec3 i = texture(uSourceTexture, vec2(uv.x + x, uv.y - y)).rgb;

    vec3 upsample = e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i);
    FragColor = vec4(max(upsample * (1.0 / 16.0), vec3(0.0)), 1.0);
}
)";

static const char* kHdrCompositeFragmentShader = R"(
#version 330 core
#include <illumo/pbr_lighting.glsl>
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uColorTexture;
uniform sampler2D uVelocityTexture;
uniform sampler2D uBloomTexture;
uniform int uMotionBlurEnabled;
uniform float uMotionBlurAmount;
uniform float uMotionBlurMax;
uniform int uMotionBlurSamples;
uniform int uBloomEnabled;
uniform float uBloomIntensity;
uniform float uExposure;
uniform int uTonemapper;
uniform int uStoreLumaInAlpha;

vec3 sanitizeHdrSample(vec3 c) {
    bvec3 invalid = bvec3(
        isnan(c.r) || isinf(c.r),
        isnan(c.g) || isinf(c.g),
        isnan(c.b) || isinf(c.b));
    return clamp(mix(c, vec3(0.0), invalid), vec3(0.0), vec3(65504.0));
}

vec3 agxDefaultContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2
         - 40.14 * x4 * x
         + 31.96 * x4
         - 6.868 * x2 * x
         + 0.4298 * x2
         + 0.1191 * x
         - 0.00232;
}

vec3 tonemapAgxLinear(vec3 val, bool punchy) {
    const mat3 agxMat = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 agxMatInv = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433, 1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    vec3 inset = agxMat * max(val, vec3(1e-10));
    vec3 logVal = clamp(log2(max(inset, vec3(1e-10))), minEv, maxEv);
    logVal = (logVal - minEv) / (maxEv - minEv);
    vec3 curved = clamp(agxDefaultContrastApprox(logVal), vec3(0.0), vec3(1.0));
    if (punchy) {
        const vec3 lw = vec3(0.2126, 0.7152, 0.0722);
        curved = pow(max(curved, vec3(0.0)), vec3(1.35));
        float luma = dot(curved, lw);
        curved = luma + 1.4 * (curved - vec3(luma));
    }
    vec3 outset = agxMatInv * curved;
    return clamp(pow(max(outset, vec3(0.0)), vec3(2.2)), vec3(0.0), vec3(1.0));
}

vec3 tonemapKhronosNeutralLinear(vec3 color) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    color = max(color, vec3(0.0));
    float x = min(color.r, min(color.g, color.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    color -= offset;

    float peak = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return clamp(color, vec3(0.0), vec3(1.0));
    }
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return clamp(mix(color, vec3(newPeak), g), vec3(0.0), vec3(1.0));
}

vec3 tonemapAcesFilmicLinear(vec3 val) {
    const mat3 acesInput = mat3(
        0.59719, 0.07600, 0.02840,
        0.35458, 0.90834, 0.13383,
        0.04823, 0.01566, 0.83777);
    const mat3 acesOutput = mat3(
         1.60475, -0.10208, -0.00327,
        -0.53108,  1.10813, -0.07276,
        -0.07367, -0.00605,  1.07602);
    vec3 v = acesInput * max(val, vec3(0.0));
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return clamp(acesOutput * (a / b), vec3(0.0), vec3(1.0));
}

vec3 tonemapUchimuraLinear(vec3 x) {
    const float P = 1.0;
    const float a = 1.0;
    const float m = 0.22;
    const float l = 0.4;
    const float c = 1.33;
    const float b = 0.0;

    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    vec3 v = max(x, vec3(0.0));
    vec3 w0 = vec3(1.0) - smoothstep(vec3(0.0), vec3(m), v);
    vec3 w2 = step(vec3(m + l0), v);
    vec3 w1 = vec3(1.0) - w0 - w2;

    vec3 T = vec3(m) * pow(max(v / m, vec3(0.0)), vec3(c)) + vec3(b);
    vec3 S = vec3(P) - (P - S1) * exp(CP * (v - vec3(S0)));
    vec3 L = vec3(m) + a * (v - vec3(m));

    return clamp(T * w0 + L * w1 + S * w2, vec3(0.0), vec3(1.0));
}

void main() {
    vec3 hdrScene = sanitizeHdrSample(texture(uColorTexture, vTexCoord).rgb);
    if (uMotionBlurEnabled != 0) {
        vec2 velocity = texture(uVelocityTexture, vTexCoord).rg * uMotionBlurAmount;
        float speed = length(velocity);
        float maxSpeed = max(uMotionBlurMax, 0.0);
        if (speed > maxSpeed && speed > 0.0) {
            velocity = (velocity / speed) * maxSpeed;
        }
        int samples = (uMotionBlurSamples <= 0) ? 8 : min(uMotionBlurSamples, 16);
        if (speed >= 0.0001 && samples > 1) {
            vec3 accum = vec3(0.0);
            for (int i = 0; i < samples; ++i) {
                float t = float(i) / float(samples - 1) - 0.5;
                accum += sanitizeHdrSample(texture(uColorTexture, vTexCoord + velocity * t).rgb);
            }
            hdrScene = accum / float(samples);
        }
    }

    if (uBloomEnabled != 0) {
        vec3 bloom = sanitizeHdrSample(texture(uBloomTexture, vTexCoord).rgb);
        hdrScene = mix(hdrScene, bloom, clamp(uBloomIntensity, 0.0, 1.0));
    }

    hdrScene *= exp2(clamp(uExposure, -8.0, 8.0));

    vec3 ldrLinear = vec3(0.0);
    if (uTonemapper == 6) {
        ldrLinear = tonemapUchimuraLinear(hdrScene);
    } else if (uTonemapper == 5) {
        ldrLinear = tonemapAcesFilmicLinear(hdrScene);
    } else if (uTonemapper == 4) {
        ldrLinear = tonemapAgxLinear(hdrScene, true);
    } else if (uTonemapper == 3) {
        ldrLinear = tonemapKhronosNeutralLinear(hdrScene);
    } else if (uTonemapper == 2) {
        ldrLinear = tonemapAgxLinear(hdrScene, false);
    } else if (uTonemapper == 1) {
        vec3 pos = max(hdrScene, vec3(0.0));
        ldrLinear = pos / (vec3(1.0) + pos);
    } else {
        ldrLinear = clamp(hdrScene, vec3(0.0), vec3(1.0));
    }

    vec3 srgb = clamp(illumoLinearToSrgb(ldrLinear), vec3(0.0), vec3(1.0));
    float outAlpha = uStoreLumaInAlpha != 0
        ? dot(srgb, vec3(0.299, 0.587, 0.114))
        : 1.0;
    FragColor = vec4(srgb, outAlpha);
}
)";

static const char* kFxaaResolveFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uColorTexture;

float sampleLuma(vec4 rgba) {
    return rgba.a;
}

void main() {
    vec2 invTex = 1.0 / vec2(textureSize(uColorTexture, 0));
    vec4 centerSample = texture(uColorTexture, vTexCoord);
    float lumaM = sampleLuma(centerSample);
    float lumaNW = sampleLuma(texture(uColorTexture, vTexCoord + vec2(-1.0,  1.0) * invTex));
    float lumaNE = sampleLuma(texture(uColorTexture, vTexCoord + vec2( 1.0,  1.0) * invTex));
    float lumaSW = sampleLuma(texture(uColorTexture, vTexCoord + vec2(-1.0, -1.0) * invTex));
    float lumaSE = sampleLuma(texture(uColorTexture, vTexCoord + vec2( 1.0, -1.0) * invTex));

    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    float lumaRange = lumaMax - lumaMin;
    if (lumaRange < max(0.0312, lumaMax * 0.125)) {
        FragColor = vec4(centerSample.rgb, 1.0);
        return;
    }

    vec2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    float dirReduce = max(
        (lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * (1.0 / 8.0)),
        1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, vec2(-8.0), vec2(8.0)) * invTex;

    vec3 rgbA = 0.5 * (
        texture(uColorTexture, vTexCoord + dir * (1.0 / 3.0 - 0.5)).rgb +
        texture(uColorTexture, vTexCoord + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (
        texture(uColorTexture, vTexCoord + dir * -0.5).rgb +
        texture(uColorTexture, vTexCoord + dir *  0.5).rgb);
    float lumaB = dot(rgbB, vec3(0.299, 0.587, 0.114));
    vec3 resolved = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    FragColor = vec4(clamp(resolved, vec3(0.0), vec3(1.0)), 1.0);
}
)";

static const char* kSobelEdgeFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uColorTexture;
uniform sampler2D uDepthTexture;
uniform int uSobelMode;
uniform int uSobelSource;
uniform int uHasDepth;
uniform float uSobelStrength;
uniform float uSobelThreshold;
uniform float uSobelThickness;
uniform int uStoreLumaInAlpha;

float sampleLogDepth(vec2 uv) {
    float d = texture(uDepthTexture, uv).r;
    return log2(max(1.0 - d, 1e-5));
}

void main() {
    vec2 stepUv = (1.0 / vec2(textureSize(uColorTexture, 0))) * clamp(uSobelThickness, 0.25, 4.0);
    vec3 center = texture(uColorTexture, vTexCoord).rgb;

    float colorMag = 0.0;
    if (uSobelSource == 0 || uSobelSource == 2 || uHasDepth == 0) {
        vec3 tl = texture(uColorTexture, vTexCoord + vec2(-stepUv.x,  stepUv.y)).rgb;
        vec3 tc = texture(uColorTexture, vTexCoord + vec2(     0.0,   stepUv.y)).rgb;
        vec3 tr = texture(uColorTexture, vTexCoord + vec2( stepUv.x,  stepUv.y)).rgb;
        vec3 ml = texture(uColorTexture, vTexCoord + vec2(-stepUv.x,       0.0)).rgb;
        vec3 mr = texture(uColorTexture, vTexCoord + vec2( stepUv.x,       0.0)).rgb;
        vec3 bl = texture(uColorTexture, vTexCoord + vec2(-stepUv.x, -stepUv.y)).rgb;
        vec3 bc = texture(uColorTexture, vTexCoord + vec2(     0.0,  -stepUv.y)).rgb;
        vec3 br = texture(uColorTexture, vTexCoord + vec2( stepUv.x, -stepUv.y)).rgb;

        vec3 gx = -tl - 2.0 * ml - bl + tr + 2.0 * mr + br;
        vec3 gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;
        vec3 magRgb = sqrt(gx * gx + gy * gy);
        colorMag = length(magRgb) * 0.35;
    }

    float depthMag = 0.0;
    if (uHasDepth != 0 && (uSobelSource == 1 || uSobelSource == 2)) {
        float dtl = sampleLogDepth(vTexCoord + vec2(-stepUv.x,  stepUv.y));
        float dtc = sampleLogDepth(vTexCoord + vec2(     0.0,   stepUv.y));
        float dtr = sampleLogDepth(vTexCoord + vec2( stepUv.x,  stepUv.y));
        float dml = sampleLogDepth(vTexCoord + vec2(-stepUv.x,       0.0));
        float dmr = sampleLogDepth(vTexCoord + vec2( stepUv.x,       0.0));
        float dbl = sampleLogDepth(vTexCoord + vec2(-stepUv.x, -stepUv.y));
        float dbc = sampleLogDepth(vTexCoord + vec2(     0.0,  -stepUv.y));
        float dbr = sampleLogDepth(vTexCoord + vec2( stepUv.x, -stepUv.y));

        float dgx = -dtl - 2.0 * dml - dbl + dtr + 2.0 * dmr + dbr;
        float dgy = -dtl - 2.0 * dtc - dtr + dbl + 2.0 * dbc + dbr;
        depthMag = length(vec2(dgx, dgy)) * 0.25;
    }

    float combined = (uSobelSource == 1 && uHasDepth != 0)
        ? depthMag
        : ((uSobelSource == 2 && uHasDepth != 0) ? max(colorMag, depthMag) : colorMag);

    float thresh = max(uSobelThreshold, 0.001);
    float edge = smoothstep(thresh, thresh + 0.15, combined);
    float strength = clamp(uSobelStrength, 0.0, 1.0);

    vec3 outRgb = (uSobelMode == 1)
        ? vec3(clamp(edge * strength, 0.0, 1.0))
        : mix(center, vec3(0.04, 0.04, 0.06), clamp(edge * strength, 0.0, 1.0));
    outRgb = clamp(outRgb, vec3(0.0), vec3(1.0));

    float outAlpha = uStoreLumaInAlpha != 0
        ? dot(outRgb, vec3(0.299, 0.587, 0.114))
        : 1.0;
    FragColor = vec4(outRgb, outAlpha);
}
)";

static const char* kMotionFeedbackFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 FragMeta;

uniform sampler2D uColorTexture;
uniform sampler2D uVelocityTexture;
uniform sampler2D uHistoryTexture;
uniform sampler2D uDepthTexture;
uniform sampler2D uMetaTexture;
// uStage: 0 = Accum (RGBA16F phosphor+invDepth, RGBA16F dir/energy/entity),
//         1 = CopyStore,
//         2 = MatrixDisplay
uniform int uStage;
// uVariant (for uStage == 0/2):
// 1 = Matrix, 2 = Cascade, 3 = Rain, 4 = Glyph, 5 = ChromaBit
uniform int uVariant;
uniform int uHistoryValid;
uniform int uHasDepth;
uniform float uFeedbackStrength;
uniform float uFeedbackSpread;
uniform float uTime;
uniform float uDeltaTime;
uniform float uDecayStep;
uniform mat4 uPrevFromCurrClip;
uniform mat4 uHistoryFromCurrClip;
uniform int uStoreLumaInAlpha;

float hash11(float p) {
    return fract(sin(p * 127.1 + 311.7) * 43758.5453);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float bayer4x4(ivec2 p) {
    int x = p.x & 3;
    int y = p.y & 3;
    int idx = (y << 2) | x;
    if (idx == 0)  return  0.0 / 16.0;
    if (idx == 1)  return  8.0 / 16.0;
    if (idx == 2)  return  2.0 / 16.0;
    if (idx == 3)  return 10.0 / 16.0;
    if (idx == 4)  return 12.0 / 16.0;
    if (idx == 5)  return  4.0 / 16.0;
    if (idx == 6)  return 14.0 / 16.0;
    if (idx == 7)  return  6.0 / 16.0;
    if (idx == 8)  return  3.0 / 16.0;
    if (idx == 9)  return 11.0 / 16.0;
    if (idx == 10) return  1.0 / 16.0;
    if (idx == 11) return  9.0 / 16.0;
    if (idx == 12) return 15.0 / 16.0;
    if (idx == 13) return  7.0 / 16.0;
    if (idx == 14) return 13.0 / 16.0;
    return 5.0 / 16.0;
}

// 16 procedural 3x5 bit-packed tech/hex micro-glyphs (row 0 = bottom, row 4 = top).
float sampleGlyph3x5(int glyphId, ivec2 sub) {
    if (sub.x < 0 || sub.x > 2 || sub.y < 0 || sub.y > 4) {
        return 0.0;
    }
    int mask = 0;
    if (glyphId == 0)       mask = (7 << 12) | (5 << 9) | (5 << 6) | (5 << 3) | 7;
    else if (glyphId == 1)  mask = (2 << 12) | (6 << 9) | (2 << 6) | (2 << 3) | 7;
    else if (glyphId == 2)  mask = (7 << 12) | (1 << 9) | (7 << 6) | (4 << 3) | 7;
    else if (glyphId == 3)  mask = (7 << 12) | (1 << 9) | (7 << 6) | (1 << 3) | 7;
    else if (glyphId == 4)  mask = (5 << 12) | (5 << 9) | (7 << 6) | (1 << 3) | 1;
    else if (glyphId == 5)  mask = (7 << 12) | (4 << 9) | (7 << 6) | (1 << 3) | 7;
    else if (glyphId == 6)  mask = (7 << 12) | (1 << 9) | (2 << 6) | (2 << 3) | 2;
    else if (glyphId == 7)  mask = (2 << 12) | (5 << 9) | (7 << 6) | (5 << 3) | 5;
    else if (glyphId == 8)  mask = (7 << 12) | (4 << 9) | (4 << 6) | (4 << 3) | 7;
    else if (glyphId == 9)  mask = (7 << 12) | (4 << 9) | (6 << 6) | (4 << 3) | 7;
    else if (glyphId == 10) mask = (7 << 12) | (4 << 9) | (6 << 6) | (4 << 3) | 4;
    else if (glyphId == 11) mask = (5 << 12) | (5 << 9) | (7 << 6) | (5 << 3) | 5;
    else if (glyphId == 12) mask = (5 << 12) | (5 << 9) | (2 << 6) | (5 << 3) | 5;
    else if (glyphId == 13) mask = (0 << 12) | (7 << 9) | (0 << 6) | (7 << 3) | 0;
    else if (glyphId == 14) mask = (0 << 12) | (2 << 9) | (7 << 6) | (2 << 3) | 0;
    else                    mask = (7 << 12) | (4 << 9) | (7 << 6) | (4 << 3) | 4;
    int bit = sub.y * 3 + (2 - sub.x);
    return float((mask >> bit) & 1);
}

// Effective wake strength at a sampled meta pixel: either decaying wake energy
// (.z) or the smoothly decaying live-entity boost (meta.w - 1.0 in [0..1]) when
// gathering strictly along +dir from behind the entity (never in front of it).
float sourceWakeEnergy(vec4 meta) {
    float liveBoost = clamp(meta.w - 1.0, 0.0, 1.0);
    return max(meta.z, liveBoost);
}

// Computes an energy-weighted average 2D travel direction (and speed magnitude
// in .z) across a two-ring star matching the gather reach so trailing chromatic
// gathers scale smoothly with velocity and never snap while fading out.
vec3 sampleDominantTrailDirAndSpeed(vec2 uv, vec2 texel, float radiusPx) {
    vec4 m0 = texture(uMetaTexture, uv);
    float w0 = sourceWakeEnergy(m0);
    vec2 accumDir = m0.xy * w0;
    float accumW = w0;

    vec2 r1 = texel * (radiusPx * 0.5);
    vec2 r2 = texel * radiusPx;
    vec4 a1 = texture(uMetaTexture, clamp(uv + vec2( r1.x,  0.0), vec2(0.001), vec2(0.999)));
    vec4 a2 = texture(uMetaTexture, clamp(uv + vec2(-r1.x,  0.0), vec2(0.001), vec2(0.999)));
    vec4 a3 = texture(uMetaTexture, clamp(uv + vec2( 0.0,  r1.y), vec2(0.001), vec2(0.999)));
    vec4 a4 = texture(uMetaTexture, clamp(uv + vec2( 0.0, -r1.y), vec2(0.001), vec2(0.999)));
    vec4 b1 = texture(uMetaTexture, clamp(uv + vec2( r2.x,  0.0), vec2(0.001), vec2(0.999)));
    vec4 b2 = texture(uMetaTexture, clamp(uv + vec2(-r2.x,  0.0), vec2(0.001), vec2(0.999)));
    vec4 b3 = texture(uMetaTexture, clamp(uv + vec2( 0.0,  r2.y), vec2(0.001), vec2(0.999)));
    vec4 b4 = texture(uMetaTexture, clamp(uv + vec2( 0.0, -r2.y), vec2(0.001), vec2(0.999)));

    float wa1 = sourceWakeEnergy(a1); accumDir += a1.xy * wa1; accumW += wa1;
    float wa2 = sourceWakeEnergy(a2); accumDir += a2.xy * wa2; accumW += wa2;
    float wa3 = sourceWakeEnergy(a3); accumDir += a3.xy * wa3; accumW += wa3;
    float wa4 = sourceWakeEnergy(a4); accumDir += a4.xy * wa4; accumW += wa4;
    float wb1 = sourceWakeEnergy(b1); accumDir += b1.xy * wb1; accumW += wb1;
    float wb2 = sourceWakeEnergy(b2); accumDir += b2.xy * wb2; accumW += wb2;
    float wb3 = sourceWakeEnergy(b3); accumDir += b3.xy * wb3; accumW += wb3;
    float wb4 = sourceWakeEnergy(b4); accumDir += b4.xy * wb4; accumW += wb4;

    float len = length(accumDir);
    if (len <= 1e-4) {
        return vec3(0.0);
    }
    float avgSpeed = clamp(len / max(accumW, 1e-4), 0.0, 1.0);
    return vec3(accumDir / len, avgSpeed);
}

// Analytical 2D box-filtered coverage for a centered square [-halfSize, +halfSize]^2
// evaluated at |cellFrac - 0.5| with pixel filter half-width halfPx (in cell units).
// Remains strictly zero at halfSize == 0 and smoothly attenuates sub-pixel squares.
float squareCoverage(vec2 centered, float halfSize, float halfPx) {
    vec2 lo = max(centered - vec2(halfPx), vec2(-halfSize));
    vec2 hi = min(centered + vec2(halfPx), vec2(halfSize));
    vec2 cov = clamp((hi - lo) / max(2.0 * halfPx, 1e-4), vec2(0.0), vec2(1.0));
    return cov.x * cov.y;
}

void main() {
    vec4 currSample = texture(uColorTexture, vTexCoord);
    if (uStage == 1) {
        // Copy both WorldFeedbackState attachments verbatim into history.
        FragColor = vec4(clamp(currSample.rgb, vec3(0.0), vec3(1.0)), currSample.a);
        FragMeta = texture(uMetaTexture, vTexCoord);
        return;
    }

    vec3 curr = currSample.rgb;
    float currDepth = (uHasDepth != 0) ? texture(uDepthTexture, vTexCoord).r : 1.0;
    float currInvDepth = (uHasDepth != 0) ? clamp(1.0 - currDepth, 0.0, 1.0) : 0.0;
    float strength = clamp(uFeedbackStrength, 0.0, 1.0);
    float spread = clamp(uFeedbackSpread, 0.1, 3.0);

    // Subtract 1-frame camera reprojection velocity so panning/orbiting never
    // tags static world geometry as moving entities and never cancels out a
    // player moving at the same speed as the following camera.
    vec2 rawVel = (uHasDepth != 0 && currDepth >= 0.99999)
        ? vec2(0.0)
        : texture(uVelocityTexture, vTexCoord).rg;
    vec2 camVel = vec2(0.0);
    if (uHasDepth != 0 && currDepth < 0.99999) {
        vec4 currNdc = vec4(vTexCoord * 2.0 - 1.0, currDepth * 2.0 - 1.0, 1.0);
        vec4 prevClip = uPrevFromCurrClip * currNdc;
        if (prevClip.w > 1e-5) {
            vec2 prevUv = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
            camVel = vTexCoord - prevUv;
        }
    }
    vec2 vel = rawVel - camVel;
    float camVelLen = length(camVel);
    float velNoise = camVelLen * 0.004 + 1e-5;
    float rawObjLen = length(vel);
    float velLen = max(0.0, rawObjLen - velNoise);
    vec2 cleanVel = (rawObjLen > 1e-6 && velLen > 0.0) ? (vel * (velLen / rawObjLen)) : vec2(0.0);

    float dtSec = clamp(uDeltaTime, 1e-5, 0.1);
    float frames60 = dtSec * 60.0;
    // True per-second object velocity (in 60Hz-frame units) with camera-pitch Y
    // compensation so behavior is identical at 30, 60, 240, and 2000+ FPS.
    float speed60 = (velLen > 1e-6) ? (velLen / frames60) : 0.0;
    float perceivedSpeed = (velLen > 1e-6)
        ? (length(vec2(cleanVel.x, cleanVel.y * 1.45)) / frames60)
        : 0.0;
    float speedIntensity = smoothstep(0.0012, 0.0080, perceivedSpeed);
    bool movingNow = speed60 > 0.00030;

    if (uStage == 0) {
        float decayStep = clamp(uDecayStep, 0.0, 4.0);

        if (uHistoryValid == 0) {
            vec2 initDir = (movingNow && speedIntensity > 0.001)
                ? ((cleanVel / velLen) * speedIntensity)
                : vec2(0.0);
            float initEntity = movingNow ? (1.0 + speedIntensity) : 0.0;
            FragColor = vec4(clamp(curr, vec3(0.0), vec3(1.0)), currInvDepth);
            FragMeta = vec4(initDir, 0.0, initEntity);
            return;
        }

        // Reproject current surface into WorldFeedbackHistory so trails stay
        // anchored to the 3D world when the camera pans or orbits, and freshly
        // revealed screen edges start clean.
        vec2 histUv = vTexCoord;
        float expectedPrevInvDepth = currInvDepth;
        bool histInBounds = true;
        if (uHasDepth != 0 && currDepth < 0.99999) {
            vec4 currNdc = vec4(vTexCoord * 2.0 - 1.0, currDepth * 2.0 - 1.0, 1.0);
            vec4 histClip = uHistoryFromCurrClip * currNdc;
            if (histClip.w > 1e-5) {
                vec3 histNdc = histClip.xyz / histClip.w;
                histUv = histNdc.xy * 0.5 + 0.5;
                expectedPrevInvDepth = clamp(1.0 - (histNdc.z * 0.5 + 0.5), 0.0, 1.0);
                histInBounds = (histUv.x >= 0.001 && histUv.x <= 0.999 &&
                                histUv.y >= 0.001 && histUv.y <= 0.999);
            } else {
                histInBounds = false;
            }
        }

        vec3 prevRgb = curr;
        float prevInvDepth = expectedPrevInvDepth;
        vec2 prevDir = vec2(0.0);
        float prevEnergy = 0.0;
        float prevEntity = 0.0;
        if (histInBounds) {
            vec4 prevSample = texture(uHistoryTexture, histUv);
            vec4 prevMeta = texture(uMetaTexture, histUv);
            prevRgb = prevSample.rgb;
            prevInvDepth = prevSample.a;
            prevDir = prevMeta.xy;
            prevEnergy = prevMeta.z;
            prevEntity = prevMeta.w;
        }

        // Perceptually smooth decay curve driven by CPU-accumulated decayStep
        // (>= 0.25 60Hz-frame ticks) so 16-bit float history never stalls at
        // uncapped 1000+ FPS frame rates.
        float decay = clamp(mix(0.68, 0.95, strength) * smoothstep(0.0, 0.08, strength), 0.0, 0.95);
        bool doDecay = decayStep > 0.0;
        float effDecayScalar = doDecay ? pow(max(decay, 1e-4), decayStep) : 1.0;
        float minStep = mix(0.005, 0.0018, strength) * decayStep;

        // 1. Live moving entity: takes foreground ownership unconditionally (even
        // when moving directly away from the camera, where its own old wake was
        // closer in depth). Trail boost scales continuously with speed so subtle
        // nudges never pop in a full-strength wake.
        if (movingNow) {
            vec2 scaledDir = (speedIntensity > 0.001)
                ? ((cleanVel / velLen) * speedIntensity)
                : vec2(0.0);
            FragColor = vec4(clamp(curr, vec3(0.0), vec3(1.0)), currInvDepth);
            FragMeta = vec4(scaledDir, 0.0, 1.0 + speedIntensity);
            return;
        }

        // 2. Resting entity: an entity stopped on this exact pixel (same
        // reprojected depth, no abrupt silhouette colour flip). Keep
        // entityState >= 1.0 so cubes stay behind it, while smoothly decaying
        // its live-boost fraction (entityState - 1.0) on decay ticks so cubes
        // behind a stopping entity fade out seamlessly at any FPS.
        float depthTol = max(expectedPrevInvDepth, prevInvDepth) * 0.005 + 3e-5;
        bool sameDepth = (uHasDepth == 0) || (abs(expectedPrevInvDepth - prevInvDepth) <= depthTol);
        if (prevEntity >= 0.5 && sameDepth && length(curr - prevRgb) < 0.14) {
            float liveBoost = max(0.0, prevEntity - 1.0);
            float nextBoost = liveBoost;
            if (doDecay && liveBoost > 0.0) {
                nextBoost = max(0.0, liveBoost * effDecayScalar - minStep * 1.15);
                if (nextBoost <= 0.001) {
                    nextBoost = 0.0;
                }
            }
            float prevDirLen = length(prevDir);
            vec2 heldDir = (nextBoost > 0.0 && prevDirLen > 1e-4)
                ? ((prevDir / prevDirLen) * nextBoost)
                : vec2(0.0);
            FragColor = vec4(clamp(curr, vec3(0.0), vec3(1.0)), currInvDepth);
            FragMeta = vec4(heldDir, 0.0, 1.0 + nextBoost);
            return;
        }

        // 3. World geometry pixel (entity just moved away from here, or ongoing
        // trail decaying over world geometry).
        float nextEnergy = prevEnergy;
        vec2 nextDir = prevDir;
        vec3 outRgb = curr;
        float outInvDepth = currInvDepth;
        bool justSpawned = false;

        if (prevEntity >= 0.5) {
            // Entity just left this pixel: seed trail energy proportionally to
            // the entity's actual speed (0 on tiny nudges, smooth up to 1.0).
            float prevSpeedBoost = clamp(prevEntity - 1.0, 0.0, 1.0);
            nextEnergy = prevSpeedBoost;
            outRgb = (prevSpeedBoost > 0.001) ? prevRgb : curr;
            outInvDepth = (prevSpeedBoost > 0.001) ? prevInvDepth : currInvDepth;
            justSpawned = prevSpeedBoost > 0.001;
        }

        if (nextEnergy > 0.0) {
            if (doDecay) {
                // On a wake pixel's spawn frame, integrate half a step so low
                // frame rates (30 FPS) match high frame rates (240+ FPS) in
                // brightness right behind the entity.
                float stepUse = justSpawned ? (decayStep * 0.5) : decayStep;
                float stepDecayScalar = pow(max(decay, 1e-4), stepUse);
                float stepMin = mix(0.005, 0.0018, strength) * stepUse;

                nextEnergy = max(0.0, nextEnergy * stepDecayScalar - stepMin * 1.15);
                float channelSplit = clamp((spread - 0.5) * 0.018, 0.0, 0.05) * strength;
                vec3 decayRgb = clamp(
                    vec3(decay - channelSplit * 0.5, decay, decay + channelSplit),
                    vec3(0.0),
                    vec3(0.96));
                vec3 effDecay = pow(max(decayRgb, vec3(1e-4)), vec3(stepUse));
                vec3 delta = prevRgb - curr;
                vec3 decayed = delta * effDecay;
                vec3 nextDelta = decayed - sign(decayed) * min(abs(decayed), vec3(stepMin));
                if (nextEnergy <= 0.001) {
                    nextEnergy = 0.0;
                    nextDir = vec2(0.0);
                    outRgb = curr;
                    outInvDepth = currInvDepth;
                } else {
                    outRgb = curr + nextDelta;
                    outInvDepth = prevInvDepth;
                }
            } else {
                outRgb = prevRgb;
                outInvDepth = prevInvDepth;
            }
        } else {
            nextDir = vec2(0.0);
        }

        FragColor = vec4(clamp(outRgb, vec3(0.0), vec3(1.0)), outInvDepth);
        FragMeta = vec4(nextDir, nextEnergy, 0.0);
        return;
    }

    // uStage == 2: Non-recursive Matrix-family display pass.
    // Entities (moving objects or resting entities: movingNow or meta.w >= 0.5)
    // ALWAYS sit cleanly in front of the cubes; world geometry (meta.w < 0.5)
    // sits behind the cubes even while the camera pans or orbits.
    vec4 phosphorSample = texture(uHistoryTexture, vTexCoord);
    vec4 centerMeta = texture(uMetaTexture, vTexCoord);
    vec3 phosphor = phosphorSample.rgb;
    bool pixelIsEntity = movingNow || (centerMeta.w >= 0.5);

    vec3 outRgb = curr;
    if (!pixelIsEntity) {
        vec2 texSize = vec2(textureSize(uColorTexture, 0));
        vec2 texel = 1.0 / texSize;
        vec2 px = vTexCoord * texSize;
        float centerEnergy = centerMeta.z;

        if (uVariant == 1) {
            // Matrix: ordered 4x4 Bayer digital block de-rez
            float cellPx = clamp(floor(3.0 + spread * 2.5 + 0.5), 3.0, 10.0);
            vec2 cellCoord = floor(px / cellPx);
            vec2 blockUv = clamp((cellCoord + vec2(0.5)) * cellPx * texel, vec2(0.001), vec2(0.999));
            vec3 blockPhos = texture(uHistoryTexture, blockUv).rgb;
            float blockEnergy = texture(uMetaTexture, blockUv).z;
            if (blockEnergy > 0.01) {
                float dither = bayer4x4(ivec2(cellCoord));
                float cellAlive = step(dither * 0.92, blockEnergy);
                vec2 cellFrac = fract(px / cellPx);
                float cellBorder = step(0.86, max(cellFrac.x, cellFrac.y));
                float edgeGlow = smoothstep(0.02, 0.25, blockEnergy) * (1.0 - smoothstep(0.28, 0.78, blockEnergy));
                vec3 digitalColor = mix(blockPhos, vec3(0.16, 0.96, 0.68), edgeGlow * (0.45 * strength));
                digitalColor *= (1.0 - cellBorder * 0.28);
                float mixWeight = clamp(cellAlive * smoothstep(0.01, 0.08, blockEnergy), 0.0, 1.0);
                outRgb = mix(curr, digitalColor, mixWeight);
            }
        } else if (uVariant == 2) {
            // Cascade: multi-scale quadtree LOD block de-rez (1x -> 2x -> 4x tiles)
            float finePx = clamp(floor(3.0 + spread * 2.0 + 0.5), 3.0, 8.0);
            float midPx = finePx * 2.0;
            float coarsePx = finePx * 4.0;

            vec2 fineCoord = floor(px / finePx);
            vec2 fineUv = clamp((fineCoord + vec2(0.5)) * finePx * texel, vec2(0.001), vec2(0.999));
            vec3 finePhos = texture(uHistoryTexture, fineUv).rgb;
            float fineE = texture(uMetaTexture, fineUv).z;

            vec2 midCoord = floor(px / midPx);
            vec2 midUv = clamp((midCoord + vec2(0.5)) * midPx * texel, vec2(0.001), vec2(0.999));
            vec3 midPhos = texture(uHistoryTexture, midUv).rgb;
            float midE = texture(uMetaTexture, midUv).z;

            vec2 coarseCoord = floor(px / coarsePx);
            vec2 coarseUv = clamp((coarseCoord + vec2(0.5)) * coarsePx * texel, vec2(0.001), vec2(0.999));
            vec3 coarsePhos = texture(uHistoryTexture, coarseUv).rgb;
            float coarseE = texture(uMetaTexture, coarseUv).z;

            if (fineE > 0.45) {
                vec2 frac1 = fract(px / finePx);
                float border1 = step(0.86, max(frac1.x, frac1.y));
                vec3 col1 = finePhos * (1.0 - border1 * 0.22);
                outRgb = mix(curr, col1, smoothstep(0.02, 0.12, fineE));
            } else if (midE > 0.18) {
                float dither2 = bayer4x4(ivec2(midCoord));
                float alive2 = step(dither2 * 0.78, midE);
                vec2 frac2 = fract(px / midPx);
                float border2 = step(0.88, max(frac2.x, frac2.y));
                vec3 col2 = mix(midPhos, vec3(0.14, 0.94, 0.78), 0.24 * strength);
                col2 = mix(col2, vec3(0.20, 1.0, 0.85), border2 * 0.35 * strength);
                outRgb = mix(curr, col2, alive2 * smoothstep(0.02, 0.14, midE));
            } else if (coarseE > 0.015) {
                float dither4 = bayer4x4(ivec2(coarseCoord));
                float alive4 = step(dither4 * 0.88, coarseE * 1.45);
                vec2 frac4 = fract(px / coarsePx);
                vec2 edgeDist = min(frac4, vec2(1.0) - frac4);
                float nearEdge = step(min(edgeDist.x, edgeDist.y), 0.10);
                float nearCorner = step(max(edgeDist.x, edgeDist.y), 0.32);
                float bracket = nearEdge * nearCorner;
                vec3 col4 = mix(coarsePhos, vec3(0.12, 0.90, 0.84), 0.38 * strength);
                col4 = mix(col4, vec3(0.45, 1.0, 0.95), bracket * 0.72 * strength);
                outRgb = mix(curr, col4, alive4 * smoothstep(0.015, 0.12, coarseE));
            }
        } else if (uVariant == 3) {
            // Rain: falling digital bit-columns shedding from the phosphor wake
            float cellPx = clamp(floor(3.0 + spread * 2.4 + 0.5), 3.0, 10.0);
            vec2 cellCoord = floor(px / cellPx);
            float colSeed = hash11(cellCoord.x * 19.37 + 3.1);
            float fallSpeed = 7.0 + colSeed * 11.0;
            float rainPhase = fract((cellCoord.y + uTime * fallSpeed) * 0.095 + colSeed * 5.7);
            float dripCells = floor((1.0 - rainPhase) * (1.0 + spread * 1.8));
            vec2 srcCoord = cellCoord + vec2(0.0, dripCells);
            vec2 blockUv = clamp((srcCoord + vec2(0.5)) * cellPx * texel, vec2(0.001), vec2(0.999));
            vec3 blockPhos = texture(uHistoryTexture, blockUv).rgb;
            float blockE = texture(uMetaTexture, blockUv).z;

            if (blockE > 0.015) {
                float dither = bayer4x4(ivec2(cellCoord));
                float streamPulse = smoothstep(0.12, 0.85, 1.0 - rainPhase);
                float cellAlive = step(dither * 0.90, blockE * (0.55 + 0.75 * streamPulse));
                vec2 cellFrac = fract(px / cellPx);
                float cellBorder = step(0.84, max(cellFrac.x, cellFrac.y));
                float isHead = step(0.86, 1.0 - rainPhase) * step(0.08, blockE);
                float tailTint = (1.0 - smoothstep(0.25, 0.80, blockE)) * (0.55 * strength);
                vec3 rainCol = mix(blockPhos, vec3(0.10, 0.98, 0.52), tailTint);
                rainCol = mix(rainCol, vec3(0.82, 1.0, 0.90), isHead * 0.70 * strength);
                rainCol *= (0.72 + 0.36 * streamPulse) * (1.0 - cellBorder * 0.34);
                float mixWeight = clamp(cellAlive * smoothstep(0.015, 0.08, blockE), 0.0, 1.0);
                outRgb = mix(curr, rainCol, mixWeight);
            }
        } else if (uVariant == 4) {
            // Glyph: procedural 3x5 simulation hex/code micro-glyphs stamped into cells
            float cellPx = clamp(floor(6.0 + spread * 3.0 + 0.5), 6.0, 16.0);
            vec2 cellCoord = floor(px / cellPx);
            vec2 blockUv = clamp((cellCoord + vec2(0.5)) * cellPx * texel, vec2(0.001), vec2(0.999));
            vec3 blockPhos = texture(uHistoryTexture, blockUv).rgb;
            float blockE = texture(uMetaTexture, blockUv).z;

            if (blockE > 0.015) {
                float dither = bayer4x4(ivec2(cellCoord));
                float cellAlive = step(dither * 0.90, blockE);
                vec2 cellFrac = fract(px / cellPx);
                ivec2 sub = ivec2(floor(cellFrac.x * 4.0), floor(cellFrac.y * 6.0));
                float tick = floor(uTime * 7.0 + hash21(cellCoord) * 16.0);
                int glyphId = int(floor(hash21(cellCoord + vec2(tick * 0.13, tick * 0.29)) * 16.0)) & 15;
                float glyphInk = sampleGlyph3x5(glyphId, sub);

                float backWeight = smoothstep(0.35, 0.85, blockE) * 0.72;
                vec3 backing = mix(curr, blockPhos * 0.58, backWeight);
                vec3 glyphColor = mix(blockPhos * 1.15 + vec3(0.08, 0.35, 0.25),
                                      vec3(0.22, 1.0, 0.76),
                                      (1.0 - smoothstep(0.20, 0.75, blockE)) * (0.65 * strength));
                vec3 cellColor = mix(backing, glyphColor, glyphInk * clamp(0.45 + 0.55 * strength, 0.0, 1.0));
                float mixWeight = clamp(cellAlive * smoothstep(0.015, 0.07, blockE), 0.0, 1.0);
                outRgb = mix(curr, cellColor, mixWeight);
            }
        } else if (uVariant == 5) {
            // ChromaBit: smooth, unidirectional motion-aligned RGB Bayer cubes
            // strictly behind entities and cleanly in front of world geometry,
            // smoothly shrinking and fading out to zero.
            float cellPx = clamp(4.0 + spread * 2.2, 4.0, 10.5);
            vec2 cellCoord = floor(px / cellPx);
            vec2 centerPx = (cellCoord + vec2(0.5)) * cellPx;
            vec2 cellUv = clamp(centerPx * texel, vec2(0.001), vec2(0.999));

            float baseShiftPx = (0.70 + spread * 0.75) * cellPx;
            vec3 dirAndSpeed = sampleDominantTrailDirAndSpeed(
                cellUv, texel, max(cellPx * 2.2, baseShiftPx * 1.5));
            vec2 dir = dirAndSpeed.xy;
            float avgSpeed = dirAndSpeed.z;
            float dirLen = length(dir);
            // Gather strictly along +dir, scaling separation distance smoothly
            // with movement speed so gentle motion stays tight and subtle while
            // fast motion fans out cleanly behind the entity.
            float shiftPx = baseShiftPx * mix(0.28, 1.0, avgSpeed) * smoothstep(0.0, 0.25, dirLen);
            vec2 uvR = clamp((centerPx + dir * (shiftPx * 0.45)) * texel, vec2(0.001), vec2(0.999));
            vec2 uvG = clamp((centerPx + dir * (shiftPx * 0.95)) * texel, vec2(0.001), vec2(0.999));
            vec2 uvB = clamp((centerPx + dir * (shiftPx * 1.45)) * texel, vec2(0.001), vec2(0.999));

            vec4 metaR = texture(uMetaTexture, uvR);
            vec4 metaG = texture(uMetaTexture, uvG);
            vec4 metaB = texture(uMetaTexture, uvB);

            float eR = sourceWakeEnergy(metaR);
            float eG = max(sourceWakeEnergy(metaG), texture(uMetaTexture, cellUv).z);
            float eB = sourceWakeEnergy(metaB);

            // Silky translucent phosphor mist under the cubes for continuous motion.
            float mistWeight = smoothstep(0.0, 0.55, centerEnergy) * (0.32 * strength);
            vec3 baseRgb = mix(curr, phosphor, mistWeight);

            if (max(eR, max(eG, eB)) > 0.002) {
                vec3 phosR = texture(uHistoryTexture, uvR).rgb;
                vec3 phosG = texture(uHistoryTexture, uvG).rgb;
                vec3 phosB = texture(uHistoryTexture, uvB).rgb;

                // Each Bayer cell smoothly fades AND shrinks toward its cell
                // center over a wide 0.42 energy span so cubes dissolve into
                // tiny sparks instead of popping off.
                float dR = bayer4x4(ivec2(cellCoord) + ivec2(1, 0)) * 0.60;
                float dG = bayer4x4(ivec2(cellCoord)) * 0.60;
                float dB = bayer4x4(ivec2(cellCoord) + ivec2(0, 1)) * 0.60;

                float lifeR = smoothstep(dR, dR + 0.42, eR) * smoothstep(0.0, 0.55, eR);
                float lifeG = smoothstep(dG, dG + 0.42, eG) * smoothstep(0.0, 0.55, eG);
                float lifeB = smoothstep(dB, dB + 0.42, eB) * smoothstep(0.0, 0.55, eB);

                vec2 cellFrac = fract(px / cellPx);
                vec2 centered = abs(cellFrac - vec2(0.5));
                float halfPx = 0.60 / max(cellPx, 1.0);

                // Linearly shrink each channel's cube toward its cell center so
                // the size reduction reads clearly before opacity hits zero,
                // using analytical box coverage for flicker-free sub-pixel fade.
                float shapeR = squareCoverage(centered, 0.46 * lifeR, halfPx);
                float shapeG = squareCoverage(centered, 0.46 * lifeG, halfPx);
                float shapeB = squareCoverage(centered, 0.46 * lifeB, halfPx);

                float fadeR = 0.5 * (lifeR + sqrt(max(lifeR, 0.0)));
                float fadeG = 0.5 * (lifeG + sqrt(max(lifeG, 0.0)));
                float fadeB = 0.5 * (lifeB + sqrt(max(lifeB, 0.0)));

                float maxAlpha = clamp(0.50 + 0.50 * strength, 0.0, 1.0);
                float wR = shapeR * fadeR * maxAlpha;
                float wG = shapeG * fadeG * maxAlpha;
                float wB = shapeB * fadeB * maxAlpha;

                float phase = cellFrac.x * 6.2831853;
                vec3 ledSmooth = vec3(
                    1.0 + 0.10 * cos(phase),
                    1.0 + 0.10 * cos(phase - 2.0943951),
                    1.0 + 0.10 * cos(phase + 2.0943951));
                vec3 cellShade = mix(vec3(1.0), ledSmooth, 0.30 * strength);

                float outR = mix(baseRgb.r, clamp(phosR.r * 1.10, 0.0, 1.0) * cellShade.r, wR);
                float outG = mix(baseRgb.g, clamp(phosG.g * 1.06, 0.0, 1.0) * cellShade.g, wG);
                float outB = mix(baseRgb.b, clamp(phosB.b * 1.14, 0.0, 1.0) * cellShade.b, wB);
                outRgb = vec3(outR, outG, outB);
            } else {
                outRgb = baseRgb;
            }
        } else {
            outRgb = mix(curr, phosphor, smoothstep(0.01, 0.15, centerEnergy));
        }
    }

    outRgb = clamp(outRgb, vec3(0.0), vec3(1.0));
    float outAlpha = uStoreLumaInAlpha != 0
        ? dot(outRgb, vec3(0.299, 0.587, 0.114))
        : 1.0;
    FragColor = vec4(outRgb, outAlpha);
    FragMeta = vec4(0.0);
}
)";

static const char* kPosterizeFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uColorTexture;
// 0 = RGB, 1 = Luma (hue-preserving), 2 = HSV
uniform int uPosterizeMode;
uniform float uPosterizeLevels;
uniform float uPosterizeGamma;
// 0 = Off, 1 = Bayer 4x4, 2 = Bayer 8x8, 3 = Interleaved Gradient Noise
uniform int uPosterizeDitherMode;
uniform float uPosterizeDitherAmount;
uniform float uPosterizeDitherScale;
uniform float uPosterizeStrength;

float safePow1(float x, float g) {
    return x > 0.0 ? pow(x, g) : 0.0;
}

vec3 safePow3(vec3 c, float g) {
    return vec3(
        c.r > 0.0 ? pow(c.r, g) : 0.0,
        c.g > 0.0 ? pow(c.g, g) : 0.0,
        c.b > 0.0 ? pow(c.b, g) : 0.0);
}

float bayer4x4Centered(ivec2 p) {
    int x = p.x & 3;
    int y = p.y & 3;
    int xc = x ^ y;
    int v = ((((xc & 1) << 1) | (y & 1)) << 2) |
            ((((xc >> 1) & 1) << 1) | ((y >> 1) & 1));
    return (float(v) + 0.5) / 16.0 - 0.5;
}

float bayer8x8Centered(ivec2 p) {
    int x = p.x & 7;
    int y = p.y & 7;
    int xc = x ^ y;
    int v = ((((xc & 1) << 1) | (y & 1)) << 4) |
            (((((xc >> 1) & 1) << 1) | ((y >> 1) & 1)) << 2) |
            ((((xc >> 2) & 1) << 1) | ((y >> 2) & 1));
    return (float(v) + 0.5) / 64.0 - 0.5;
}

float ignCentered(vec2 p) {
    float v = fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
    return v - 0.5;
}

vec3 rgbToHsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-6;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsvToRgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    vec3 orig = clamp(texture(uColorTexture, vTexCoord).rgb, vec3(0.0), vec3(1.0));
    float levels = clamp(floor(uPosterizeLevels + 0.5), 2.0, 32.0);
    float steps = max(levels - 1.0, 1.0);
    float gamma = clamp(uPosterizeGamma, 0.4, 2.5);
    float invGamma = 1.0 / gamma;

    float ditherOffset = 0.0;
    float ditherAmt = clamp(uPosterizeDitherAmount, 0.0, 1.5);
    if (uPosterizeDitherMode > 0 && ditherAmt > 0.0) {
        float scale = clamp(floor(uPosterizeDitherScale + 0.5), 1.0, 4.0);
        vec2 texDims = vec2(textureSize(uColorTexture, 0));
        vec2 cellPos = floor((vTexCoord * texDims) / scale);
        ivec2 cellCoord = ivec2(cellPos);
        float sampleDither = 0.0;
        if (uPosterizeDitherMode == 1) {
            sampleDither = bayer4x4Centered(cellCoord);
        } else if (uPosterizeDitherMode == 2) {
            sampleDither = bayer8x8Centered(cellCoord);
        } else {
            sampleDither = ignCentered(cellPos);
        }
        ditherOffset = sampleDither * ditherAmt;
    }

    vec3 posterized = orig;
    if (uPosterizeMode == 1) {
        // Luminance-only quantization preserving original hue and chromaticity.
        float luma = dot(orig, vec3(0.2126, 0.7152, 0.0722));
        float shapedL = safePow1(clamp(luma, 0.0, 1.0), gamma);
        float qL = clamp(floor(shapedL * steps + 0.5 + ditherOffset) / steps, 0.0, 1.0);
        float targetL = safePow1(qL, invGamma);
        if (luma > 1e-4) {
            vec3 scaled = orig * (targetL / luma);
            float maxComp = max(scaled.r, max(scaled.g, scaled.b));
            if (maxComp > 1.0) {
                float t = clamp((1.0 - targetL) / max(maxComp - targetL, 1e-4), 0.0, 1.0);
                scaled = mix(vec3(targetL), scaled, t);
            }
            posterized = clamp(scaled, vec3(0.0), vec3(1.0));
        } else {
            posterized = vec3(targetL);
        }
    } else if (uPosterizeMode == 2) {
        // Stylized HSV quantization: stepped hue wheel, saturation, and value.
        vec3 hsv = rgbToHsv(orig);
        float hueBins = max(floor(levels * 2.0 + 0.5), 6.0);
        float satSteps = max(floor(levels * 0.5 + 0.5), 1.0);
        float qH = fract(floor(hsv.x * hueBins + 0.5 + ditherOffset * 0.5) / hueBins);
        float qS = clamp(floor(hsv.y * satSteps + 0.5 + ditherOffset * 0.5) / satSteps, 0.0, 1.0);
        float shapedV = safePow1(clamp(hsv.z, 0.0, 1.0), gamma);
        float qV = clamp(floor(shapedV * steps + 0.5 + ditherOffset) / steps, 0.0, 1.0);
        float targetV = safePow1(qV, invGamma);
        posterized = clamp(hsvToRgb(vec3(qH, qS, targetV)), vec3(0.0), vec3(1.0));
    } else {
        // Per-channel RGB quantization.
        vec3 shaped = safePow3(orig, gamma);
        vec3 q = clamp(floor(shaped * steps + 0.5 + vec3(ditherOffset)) / steps,
                       vec3(0.0), vec3(1.0));
        posterized = clamp(safePow3(q, invGamma), vec3(0.0), vec3(1.0));
    }

    vec3 outRgb = clamp(mix(orig, posterized, clamp(uPosterizeStrength, 0.0, 1.0)),
                        vec3(0.0), vec3(1.0));
    FragColor = vec4(outRgb, 1.0);
}
)";

static void
fillCanvasPipeline(PipelineState& ps)
{
  ps.depthTestEnabled = false;
  ps.blendEnabled = false;
  ps.faceCullingEnabled = false;
  ps.primitives = Primitives::Triangles;
}

static void
fillUiBlendPipeline(PipelineState& ps)
{
  ps.depthTestEnabled = false;
  ps.blendEnabled = true;
  ps.blendSrc = BlendFactor::SrcAlpha;
  ps.blendDst = BlendFactor::OneMinusSrcAlpha;
  ps.faceCullingEnabled = false;
  ps.primitives = Primitives::Triangles;
}

void
Renderer::ensureBuiltinStyles()
{
  if (_builtinStylesReady || !_backend) {
    return;
  }
  ILLUMO_PROFILE_ZONE("Renderer.ensureBuiltinStyles");

  // Canvas: file-backed shaders (same paths as historical Canvas enroll).
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderPaths paths;
    paths.vertexPath = "Shader/canvas_vertex.glsl";
    paths.fragmentPath = "Shader/canvas_frag.glsl";
    style.shaderHandle = enrollShader(paths);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Canvas)] =
      createStyle(style);
  }

  // UI text (GLString / SplashText / FPS overlay).
  {
    RenderStyle style;
    fillUiBlendPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kUiTextVertexShader;
    sources.fragmentSource = kUiTextFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::UiText)] =
      createStyle(style);
  }

  // Console panel batch (CommandLine).
  {
    RenderStyle style;
    fillUiBlendPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kConsoleVertexShader;
    sources.fragmentSource = kConsoleFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Console)] =
      createStyle(style);
  }

  // Shape primitives (GameVisual).
  {
    RenderStyle style;
    fillUiBlendPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kShapeVertexShader;
    sources.fragmentSource = kShapeFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Shape)] =
      createStyle(style);
  }

  // Sprite primitives (GameVisual).
  {
    RenderStyle style;
    fillUiBlendPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kSpriteVertexShader;
    sources.fragmentSource = kSpriteFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Sprite)] =
      createStyle(style);
  }

  // 3D Lit Mesh / Solid Primitives (MeshVisual).
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = true;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderPaths paths;
    paths.vertexPath = "Shader/mesh_lit_vertex.glsl";
    paths.fragmentPath = "Shader/mesh_lit_frag.glsl";
    style.shaderHandle = enrollShader(paths);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::LitMesh)] =
      createStyle(style);
  }

  // Shadow Depth pass (depth-only offscreen).
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = true;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderPaths paths;
    paths.vertexPath = "Shader/shadow_depth_vertex.glsl";
    paths.fragmentPath = "Shader/shadow_depth_frag.glsl";
    style.shaderHandle = enrollShader(paths);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::ShadowDepth)] =
      createStyle(style);
  }

  // Motion Blur Post-Process pass.
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = false;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kMotionBlurFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::MotionBlur)] =
      createStyle(style);
  }

  // Canvas fade resolve: the full-screen quad into a cell-sized target.
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kCanvasFadeFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::CanvasFade)] =
      createStyle(style);
  }

  // 3D Cubemap Skybox.
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = true;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderSources sources;
    sources.vertexSource = kSkyboxVertexShader;
    sources.fragmentSource = kSkyboxFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Skybox)] =
      createStyle(style);
  }

  // Instanced lit meshes and their shadow depth pass.
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = true;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderPaths paths;
    paths.vertexPath = "Shader/mesh_lit_instanced_vertex.glsl";
    paths.fragmentPath = "Shader/mesh_lit_instanced_frag.glsl";
    style.shaderHandle = enrollShader(paths);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::LitMeshInstanced)] =
      createStyle(style);
  }
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = true;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderPaths paths;
    paths.vertexPath = "Shader/shadow_depth_instanced_vertex.glsl";
    paths.fragmentPath = "Shader/shadow_depth_frag.glsl";
    style.shaderHandle = enrollShader(paths);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::ShadowDepthInstanced)] =
      createStyle(style);
  }

  // Analytical Ground Plane.
  {
    RenderStyle style;
    style.pipeline.depthTestEnabled = true;
    style.pipeline.blendEnabled = false;
    style.pipeline.faceCullingEnabled = false;
    style.pipeline.primitives = Primitives::Triangles;
    ShaderSources sources;
    sources.vertexSource = kGroundPlaneVertexShader;
    sources.fragmentSource = kGroundPlaneFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::GroundPlane)] =
      createStyle(style);
  }

  // HDR Bloom Downsample (Jimenez 13-tap + optional Karis average).
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kBloomDownsampleFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::BloomDownsample)] =
      createStyle(style);
  }

  // HDR Bloom Upsample (3x3 tent additive blend).
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    style.pipeline.blendEnabled = true;
    style.pipeline.blendSrc = BlendFactor::One;
    style.pipeline.blendDst = BlendFactor::One;
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kBloomUpsampleFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::BloomUpsample)] =
      createStyle(style);
  }

  // Linear HDR Composite (motion blur + bloom mix + exposure + tonemap).
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kHdrCompositeFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::HdrComposite)] =
      createStyle(style);
  }

  // Single-pass FXAA resolve.
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kFxaaResolveFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::FxaaResolve)] =
      createStyle(style);
  }

  // 3x3 Sobel edge detection post-process pass.
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kSobelEdgeFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::SobelEdge)] =
      createStyle(style);
  }

  // Temporal phosphor persistence feedback blend / history copy pass.
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kMotionFeedbackFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::MotionFeedback)] =
      createStyle(style);
  }

  // Color quantization / posterization post-process pass.
  {
    RenderStyle style;
    fillCanvasPipeline(style.pipeline);
    ShaderSources sources;
    sources.vertexSource = kMotionBlurVertexShader;
    sources.fragmentSource = kPosterizeFragmentShader;
    style.shaderHandle = enrollShader(sources);
    style.ready = style.shaderHandle.isValid();
    builtinStyleHandles[renderStyleIndex(RenderStyleId::Posterize)] =
      createStyle(style);
  }

  _builtinStylesReady = true;
}

TextureHandle
Renderer::whiteTexture()
{
  if (_whiteTextureHandle.isValid() || _backend == nullptr ||
      !_backend->TexturesDrawWhenCreated()) {
    return _whiteTextureHandle;
  }
  const std::array<unsigned char, 4> white{ 255, 255, 255, 255 };
  TextureOptions options;
  options.filter = TextureFilter::Nearest;
  options.wrapX = TextureWrap::ClampToEdge;
  options.wrapY = TextureWrap::ClampToEdge;
  _whiteTextureHandle = enrollTexture(white.data(), 1, 1, 4, options);
  return _whiteTextureHandle;
}

TextureHandle
Renderer::flatNormalTexture()
{
  if (_flatNormalTextureHandle.isValid() || _backend == nullptr ||
      !_backend->TexturesDrawWhenCreated()) {
    return _flatNormalTextureHandle;
  }
  const std::array<unsigned char, 4> flatNormal{ 128, 128, 255, 255 };
  TextureOptions options;
  options.filter = TextureFilter::Nearest;
  options.wrapX = TextureWrap::ClampToEdge;
  options.wrapY = TextureWrap::ClampToEdge;
  _flatNormalTextureHandle = enrollTexture(flatNormal.data(), 1, 1, 4, options);
  return _flatNormalTextureHandle;
}

TextureHandle
Renderer::defaultOrmTexture()
{
  if (_defaultOrmTextureHandle.isValid() || _backend == nullptr ||
      !_backend->TexturesDrawWhenCreated()) {
    return _defaultOrmTextureHandle;
  }
  const std::array<unsigned char, 4> orm{ 255, 255, 255, 255 };
  TextureOptions options;
  options.filter = TextureFilter::Nearest;
  options.wrapX = TextureWrap::ClampToEdge;
  options.wrapY = TextureWrap::ClampToEdge;
  _defaultOrmTextureHandle = enrollTexture(orm.data(), 1, 1, 4, options);
  return _defaultOrmTextureHandle;
}

RenderStyleHandle
Renderer::createStyle(const RenderStyle& style)
{
  if (!style.ready || !style.shaderHandle.isValid()) {
    return RenderStyleHandle{};
  }
  RenderStyleHandle handle = styleHandles.allocate();
  RenderStyleEntry entry;
  entry.generation = handle.generation;
  entry.style = style;
  styleRegistry[handle.slot] = entry;
  return handle;
}

bool
Renderer::updateStyle(RenderStyleHandle handle, const RenderStyle& style)
{
  std::unordered_map<uint32_t, RenderStyleEntry>::iterator it =
    styleRegistry.find(handle.slot);
  if (!styleHandles.isCurrent(handle) || it == styleRegistry.end() ||
      it->second.generation != handle.generation || !style.ready ||
      !style.shaderHandle.isValid()) {
    Logger::LogWarning("updateStyle: invalid or stale style ignored");
    return false;
  }
  it->second.style = style;
  return true;
}

bool
Renderer::destroyStyle(RenderStyleHandle handle)
{
  std::unordered_map<uint32_t, RenderStyleEntry>::iterator it =
    styleRegistry.find(handle.slot);
  if (!styleHandles.isCurrent(handle) || it == styleRegistry.end() ||
      it->second.generation != handle.generation) {
    Logger::LogWarning("destroyStyle: stale style handle ignored");
    return false;
  }
  styleRegistry.erase(it);
  return styleHandles.release(handle);
}

const RenderStyle*
Renderer::getStyle(RenderStyleHandle handle) const
{
  std::unordered_map<uint32_t, RenderStyleEntry>::const_iterator it =
    styleRegistry.find(handle.slot);
  if (!styleHandles.isCurrent(handle) || it == styleRegistry.end() ||
      it->second.generation != handle.generation || !it->second.style.ready) {
    return nullptr;
  }
  return &it->second.style;
}

RenderStyle*
Renderer::getStyle(RenderStyleHandle handle)
{
  std::unordered_map<uint32_t, RenderStyleEntry>::iterator it =
    styleRegistry.find(handle.slot);
  if (!styleHandles.isCurrent(handle) || it == styleRegistry.end() ||
      it->second.generation != handle.generation || !it->second.style.ready) {
    return nullptr;
  }
  return &it->second.style;
}

bool
Renderer::bindStyle(RenderStyleHandle handle)
{
  const RenderStyle* style = getStyle(handle);
  if (!style) {
    Logger::LogWarning("bindStyle: invalid or stale style ignored");
    return false;
  }
  pushPipelineState(style->pipeline);
  pushSetShader(style->shaderHandle);
  return true;
}

RenderStyleHandle
Renderer::getBuiltinStyleHandle(RenderStyleId id) const
{
  const unsigned index = renderStyleIndex(id);
  if (index >= renderStyleCount()) {
    return RenderStyleHandle{};
  }
  return builtinStyleHandles[index];
}

const RenderStyle*
Renderer::getStyle(RenderStyleId id) const
{
  const unsigned index = renderStyleIndex(id);
  if (index >= renderStyleCount()) {
    return nullptr;
  }
  return getStyle(builtinStyleHandles[index]);
}

RenderStyle*
Renderer::getStyle(RenderStyleId id)
{
  ensureBuiltinStyles();
  const unsigned index = renderStyleIndex(id);
  if (index >= renderStyleCount()) {
    return nullptr;
  }
  return getStyle(builtinStyleHandles[index]);
}

bool
Renderer::bindStyle(RenderStyleId id)
{
  ensureBuiltinStyles();
  return bindStyle(getBuiltinStyleHandle(id));
}
