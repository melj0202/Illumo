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
    // camera and self-shadows whole triangles. Offset along the normal so
    // the receiver is tested slightly toward the light.
    vec3 offsetPos = fragPos + normal * uShadowNormalOffset;
    vec4 lightClip = uLightSpaceMatrix * vec4(offsetPos, 1.0);
    vec3 projCoords = lightClip.xyz / lightClip.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float bias = uShadowBias + uShadowSlopeScale * (1.0 - nDotL);

    int radius = uShadowPcf > 0 ? 1 : 0;
    float shadow = 0.0;
    float samples = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            if (abs(x) > radius || abs(y) > radius) {
                continue;
            }
            float pcfDepth =
                texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            samples += 1.0;
        }
    }
    return shadow / max(samples, 1.0);
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
    vec4 baseColor = vColor;
    FragColor = vec4(lighting * baseColor.rgb, baseColor.a);

    vec2 currentNdc = vCurrentClip.xy / max(abs(vCurrentClip.w), 1e-5);
    vec2 prevNdc = vPrevClip.xy / max(abs(vPrevClip.w), 1e-5);
    FragVelocity = (currentNdc - prevNdc) * 0.5;
}
