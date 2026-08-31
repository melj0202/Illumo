#version 330 core

in vec3 vFragPos;
in vec3 vNormal;
in vec4 vColor;
in vec2 vTexCoord;
in vec4 vFragPosLightSpace;

out vec4 FragColor;

uniform vec3 uLightDir;         // Direction TO light
uniform vec3 uLightColor;       // Light diffuse & specular color
uniform vec3 uAmbientColor;     // Ambient illumination
uniform sampler2D uShadowMap;   // Depth texture (slot 1)

float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // Perspective divide to NDC [-1, 1]
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    // Transform to [0, 1] range
    projCoords = projCoords * 0.5 + 0.5;
    
    // Outside far plane or shadow frustum -> not in shadow
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    
    float currentDepth = projCoords.z;
    // Adaptive depth bias based on slope
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
    
    // 3x3 Percentage-Closer Filtering (PCF)
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(uShadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;
    
    return shadow;
}

void main()
{
    vec3 norm = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    
    // Ambient
    vec3 ambient = uAmbientColor;
    
    // Diffuse
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * uLightColor;
    
    // Shadow factor
    float shadow = calculateShadow(vFragPosLightSpace, norm, lightDir);
    
    // Combined lighting
    vec3 lighting = ambient + (1.0 - shadow) * diffuse;
    
    vec4 baseColor = vColor;
    FragColor = vec4(lighting * baseColor.rgb, baseColor.a);
}