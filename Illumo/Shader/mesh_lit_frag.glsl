#version 330 core
#include <illumo/pbr_lighting.glsl>

in vec3 vFragPos;
in vec3 vNormal;
in vec4 vColor;
in vec2 vTexCoord;
in vec4 vCurrentClip;
in vec4 vPrevClip;

layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec2 FragVelocity;

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
uniform int uMotionBlurEnabled;
uniform vec4 uTint;
uniform sampler2D uAlbedoMap;
uniform sampler2D uNormalMap;
uniform sampler2D uOrmMap;
uniform sampler2D uEmissiveMap;
uniform vec4 uMaterialFactors;
uniform vec3 uEmissiveFactor;

float calculateShadow(vec3 fragPos, vec3 normal, vec3 lightDir)
{
    if (uShadowsEnabled == 0) {
        return 0.0;
    }
    return illumoCalculateDirectionalShadow(
        uShadowMap,
        fragPos,
        normal,
        lightDir,
        vCurrentClip.w,
        uLightSpaceMatrix,
        uShadowBias,
        uShadowSlopeScale,
        uShadowNormalOffset,
        uShadowPcf);
}

void main()
{
    float nLenSq = dot(vNormal, vNormal);
    vec3 geomN = nLenSq > 1e-8 ? (vNormal * inversesqrt(nLenSq)) : vec3(0.0, 1.0, 0.0);

    vec4 srgbBase = vColor * uTint;
    vec4 albedoTex = vec4(1.0);
    vec3 ormTex = vec3(1.0);
    vec3 emissiveTex = vec3(1.0);
    float metallic = 0.0;
    float roughness = 0.8;
    vec3 N = geomN;

    if (uMaterialFactors.w >= 0.5) {
        albedoTex = texture(uAlbedoMap, vTexCoord);
        ormTex = texture(uOrmMap, vTexCoord).rgb;
        emissiveTex = texture(uEmissiveMap, vTexCoord).rgb;
        metallic = clamp(uMaterialFactors.x * ormTex.b, 0.0, 1.0);
        roughness = clamp(uMaterialFactors.y * ormTex.g, 0.045, 1.0);
        if (uMaterialFactors.z > 0.5) {
            N = illumoPerturbNormal(geomN, vFragPos, vTexCoord, texture(uNormalMap, vTexCoord).rgb);
        }
    }

    vec3 albedo = illumoSrgbToLinear(srgbBase.rgb) * illumoSrgbToLinear(albedoTex.rgb);
    float alpha = srgbBase.a * albedoTex.a;
    float ao = clamp(ormTex.r, 0.0, 1.0);
    vec3 V = illumoResolveViewVector(vFragPos, N);

    float lDirLenSq = dot(uLightDir, uLightDir);
    vec3 sunDir = lDirLenSq > 1e-8 ? (uLightDir * inversesqrt(lDirLenSq)) : vec3(0.0, 1.0, 0.0);
    float shadow = calculateShadow(vFragPos, geomN, sunDir);

    vec3 radiance = ao * max(uAmbientColor, vec3(0.0)) * albedo;
    radiance += (1.0 - shadow) * illumoEvaluateDirectLight(
        N, V, sunDir, max(uLightColor, vec3(0.0)), albedo, metallic, roughness);

    int punctualCount = clamp(uLightCountsAndFlags.x, 0, 64);
    for (int i = 0; i < punctualCount; ++i) {
        IllumoPunctualLight light = uPunctualLights[i];
        vec3 toLight = light.positionAndInvRange.xyz - vFragPos;
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
                i == uLightCountsAndFlags.z) {
                float spotNDotL = dot(geomN, L);
                spotShadow = spotNDotL > 0.0
                    ? illumoSampleShadowQuadrant(
                        uShadowMap,
                        vFragPos,
                        geomN,
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
                    N,
                    V,
                    L,
                    light.radianceAndOuterCos.rgb * attenuation,
                    albedo,
                    metallic,
                    roughness);
            }
        }
    }

    if (uMaterialFactors.w >= 0.5) {
        radiance += illumoSrgbToLinear(emissiveTex) * max(uEmissiveFactor, vec3(0.0));
    }

    vec3 outRgb = (uLightCountsAndFlags.w & 2) != 0
        ? max(radiance, vec3(0.0))
        : illumoLinearToSrgb(clamp(radiance, 0.0, 1.0));
    FragColor = vec4(outRgb, alpha);

    if (uMotionBlurEnabled != 0) {
        vec2 currentNdc = vCurrentClip.xy / max(abs(vCurrentClip.w), 1e-5);
        vec2 prevNdc = vPrevClip.xy / max(abs(vPrevClip.w), 1e-5);
        FragVelocity = (currentNdc - prevNdc) * 0.5;
    } else {
        FragVelocity = vec2(0.0);
    }
}
