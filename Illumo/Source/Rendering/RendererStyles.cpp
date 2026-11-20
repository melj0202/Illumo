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
