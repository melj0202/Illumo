#version 330 core

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

float calculateShadow(vec3 fragPos, vec3 normal, vec3 lightDir)
{
    if (uShadowsEnabled == 0) {
        return 0.0;
    }

    float nDotL = dot(normal, lightDir);
    if (nDotL <= 0.0) {
        return 1.0;
    }

    // Compare in the fragment, not from an interpolated light-space varying.
    // Perspective-correct interpolation of that quantity follows the view
    // camera and self-shadows whole triangles. Scale normal offset by sin(theta)
    // so flat lit surfaces do not detach, and ensure grazing walls clear the
    // bilinear PCF + rasterizer footprint.
    vec2 mapSize = vec2(textureSize(uShadowMap, 0));
    vec2 texelSize = 1.0 / mapSize;
    float invHalfExtent = length(vec3(
        uLightSpaceMatrix[0].x, uLightSpaceMatrix[1].x, uLightSpaceMatrix[2].x));
    float worldTexel =
        invHalfExtent > 1e-5 ? 2.0 / (invHalfExtent * mapSize.x) : 0.0;

    float sinTheta = sqrt(max(1.0 - nDotL * nDotL, 0.0));
    float pcfTexelRadius = uShadowPcf > 0 ? 2.5 : 1.25;
    float minTexelOffset =
        uShadowNormalOffset > 0.0 ? pcfTexelRadius * worldTexel : 0.0;
    vec3 offsetPos =
        fragPos + normal * (max(uShadowNormalOffset, minTexelOffset) * sinTheta);

    vec4 lightClip = uLightSpaceMatrix * vec4(offsetPos, 1.0);
    vec3 projCoords = lightClip.xyz / lightClip.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float tanTheta = min(sinTheta / max(nDotL, 0.1), 3.0);
    float bias =
        uShadowBias + uShadowSlopeScale * (1.0 - nDotL) + uShadowBias * tanTheta;

    if (uShadowPcf <= 0) {
        float pcfDepth = texture(uShadowMap, projCoords.xy).r;
        return currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }

    // Separable 4x4 taps equal the average of 3x3 bilinear depth comparisons
    // while keeping Nearest sampler compatibility across OpenGL and Vulkan.
    vec2 texelPos = projCoords.xy * mapSize - 0.5;
    vec2 baseTexel = floor(texelPos);
    vec2 frac = texelPos - baseTexel;
    vec2 baseUv = (baseTexel + 0.5) * texelSize;
    vec4 wx = vec4(1.0 - frac.x, 1.0, 1.0, frac.x);
    vec4 wy = vec4(1.0 - frac.y, 1.0, 1.0, frac.y);

    float shadow = 0.0;
    for (int ix = 0; ix < 4; ++ix) {
        for (int iy = 0; iy < 4; ++iy) {
            vec2 sampleUv =
                baseUv + vec2(float(ix - 1), float(iy - 1)) * texelSize;
            float pcfDepth = texture(uShadowMap, sampleUv).r;
            float occluded = currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            shadow += occluded * wx[ix] * wy[iy];
        }
    }
    return shadow / 9.0;
}

void main()
{
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);

    vec3 ambient = uAmbientColor;
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    float shadow = calculateShadow(vFragPos, norm, lightDir);

    vec3 lighting = ambient + (1.0 - shadow) * diffuse;
    vec4 baseColor = vColor * uTint;
    FragColor = vec4(lighting * baseColor.rgb, baseColor.a);

    if (uMotionBlurEnabled != 0) {
        vec2 currentNdc = vCurrentClip.xy / max(abs(vCurrentClip.w), 1e-5);
        vec2 prevNdc = vPrevClip.xy / max(abs(vPrevClip.w), 1e-5);
        FragVelocity = (currentNdc - prevNdc) * 0.5;
    } else {
        FragVelocity = vec2(0.0);
    }
}
