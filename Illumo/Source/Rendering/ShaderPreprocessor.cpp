#include <Illumo/Foundation/Profile.h>
#include <Illumo/Rendering/AssetSource.h>
#include <Illumo/Rendering/ShaderPreprocessor.h>
#include <Illumo/Services/Logger.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_set>

static std::mutex s_virtualModulesMutex;
static std::unordered_map<std::string, std::string> s_virtualModules;
static bool s_defaultsRegistered = false;

static std::string
trimString(const std::string& str)
{
  size_t first = str.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  size_t last = str.find_last_not_of(" \t\r\n");
  return str.substr(first, (last - first + 1));
}

static bool
startsWith(const std::string& str, const std::string& prefix)
{
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

std::string
ShaderPreprocessor::normalizeModuleName(const std::string& name)
{
  std::string result = trimString(name);
  if (startsWith(result, "<") && result.size() >= 2 && result.back() == '>') {
    result = result.substr(1, result.size() - 2);
  } else if (startsWith(result, "\"") && result.size() >= 2 &&
             result.back() == '\"') {
    result = result.substr(1, result.size() - 2);
  }
  std::replace(result.begin(), result.end(), '\\', '/');
#ifdef _WIN32
  std::transform(
    result.begin(), result.end(), result.begin(), [](char c) -> char {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
#endif
  return result;
}

static std::string
readFileToString(const std::string& filePath)
{
  std::ifstream file(filePath, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return "";
  }
  std::stringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

void
ShaderPreprocessor::RegisterVirtualInclude(const std::string& name,
                                           const std::string& source)
{
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  s_virtualModules[normalizeModuleName(name)] = source;
}

void
ShaderPreprocessor::UnregisterVirtualInclude(const std::string& name)
{
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  s_virtualModules.erase(normalizeModuleName(name));
}

bool
ShaderPreprocessor::HasVirtualInclude(const std::string& name)
{
  RegisterDefaultModules();
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  return s_virtualModules.find(normalizeModuleName(name)) !=
         s_virtualModules.end();
}

std::string
ShaderPreprocessor::GetVirtualInclude(const std::string& name)
{
  RegisterDefaultModules();
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  std::unordered_map<std::string, std::string>::const_iterator it =
    s_virtualModules.find(normalizeModuleName(name));
  if (it != s_virtualModules.end()) {
    return it->second;
  }
  return "";
}

void
ShaderPreprocessor::ClearVirtualIncludes()
{
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  s_virtualModules.clear();
  s_defaultsRegistered = false;
}

void
ShaderPreprocessor::RegisterDefaultModules()
{
  std::lock_guard<std::mutex> lock(s_virtualModulesMutex);
  if (s_defaultsRegistered) {
    return;
  }

  // <illumo/common.glsl>
  s_virtualModules["illumo/common.glsl"] = R"(
#ifndef ILLUMO_COMMON_GLSL
#define ILLUMO_COMMON_GLSL

// Standard Illumo uniform contracts
// uMVP: 4x4 model-view-projection matrix for 2D/3D visual objects
// u_resolution: 2D target resolution in pixels (width, height)
// u_scale: 2D object scale factor
// u_position: 2D object position offset

#endif
)";

  // <illumo/screen_transform.glsl>
  s_virtualModules["illumo/screen_transform.glsl"] = R"(
#ifndef ILLUMO_SCREEN_TRANSFORM_GLSL
#define ILLUMO_SCREEN_TRANSFORM_GLSL

vec4 illumoScreenToClip(vec2 pixelPos, vec2 resolution, float z) {
    float x = (pixelPos.x / resolution.x) * 2.0 - 1.0;
    float y = 1.0 - (pixelPos.y / resolution.y) * 2.0;
    return vec4(x, y, z, 1.0);
}

vec4 illumoScaledScreenToClip(vec2 pos, vec2 scale, vec2 offset, vec2 resolution, float z) {
    vec2 pixelPos = pos * scale + offset;
    return illumoScreenToClip(pixelPos, resolution, z);
}

#endif
)";

  // <illumo/vertex_2d.glsl>
  s_virtualModules["illumo/vertex_2d.glsl"] = R"(
#ifndef ILLUMO_VERTEX_2D_GLSL
#define ILLUMO_VERTEX_2D_GLSL

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aUv;

#endif
)";

  // <illumo/vertex_3d.glsl>
  s_virtualModules["illumo/vertex_3d.glsl"] = R"(
#ifndef ILLUMO_VERTEX_3D_GLSL
#define ILLUMO_VERTEX_3D_GLSL

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec2 aUv;
layout (location = 3) in vec3 aNormal;

#endif
)";

  // <illumo/sprite.glsl>
  s_virtualModules["illumo/sprite.glsl"] = R"(
#ifndef ILLUMO_SPRITE_GLSL
#define ILLUMO_SPRITE_GLSL

vec4 sampleSprite(sampler2D tex, vec2 uv, vec4 tint) {
    return texture(tex, uv) * tint;
}

#endif
)";

  // <illumo/light_uniforms.glsl>
  s_virtualModules["illumo/light_uniforms.glsl"] = R"(
#ifndef ILLUMO_LIGHT_UNIFORMS_GLSL
#define ILLUMO_LIGHT_UNIFORMS_GLSL

struct IllumoPunctualLight
{
    vec4 positionAndInvRange;
    vec4 radianceAndOuterCos;
    vec4 directionAndAngleScale;
};

layout (std140) uniform LightUniforms
{
    mat4 uAtlasLightMatrices[4];
    vec4 uCascadeSplitsAndTexel;
    ivec4 uLightCountsAndFlags;
    vec4 uLightCameraPos;
    IllumoPunctualLight uPunctualLights[64];
};

#endif
)";

  // <illumo/pbr_lighting.glsl>
  s_virtualModules["illumo/pbr_lighting.glsl"] = R"(
#ifndef ILLUMO_PBR_LIGHTING_GLSL
#define ILLUMO_PBR_LIGHTING_GLSL
#include <illumo/light_uniforms.glsl>

vec3 illumoSrgbToLinear(vec3 c)
{
    vec3 clamped = max(c, vec3(0.0));
    vec3 lo = clamped / 12.92;
    vec3 hi = pow((clamped + 0.055) / 1.055, vec3(2.4));
    return mix(lo, hi, step(vec3(0.04045), clamped));
}

vec3 illumoLinearToSrgb(vec3 c)
{
    vec3 clamped = max(c, vec3(0.0));
    vec3 lo = clamped * 12.92;
    vec3 hi = 1.055 * pow(clamped, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), clamped));
}

vec3 illumoResolveViewVector(vec3 fragPos, vec3 N)
{
    if (uLightCameraPos.w > 0.5) {
        vec3 toEye = uLightCameraPos.xyz - fragPos;
        float lenSq = dot(toEye, toEye);
        return lenSq > 1e-8 ? (toEye * inversesqrt(lenSq)) : N;
    }
    if (uLightCameraPos.w < -0.5) {
        float lenSq = dot(uLightCameraPos.xyz, uLightCameraPos.xyz);
        return lenSq > 1e-8 ? (uLightCameraPos.xyz * inversesqrt(lenSq)) : N;
    }
    return N;
}

vec3 illumoPerturbNormal(vec3 N, vec3 worldPos, vec2 uv, vec3 mapSample)
{
    vec3 tangentNormal = mapSample * 2.0 - 1.0;
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    float maxLenSq = max(dot(T, T), dot(B, B));
    if (maxLenSq < 1e-8) {
        return N;
    }
    float detSign = dot(cross(dp1, dp2), N) < 0.0 ? -1.0 : 1.0;
    float invMax = inversesqrt(maxLenSq) * detSign;
    mat3 tbn = mat3(T * invMax, B * invMax, N);
    vec3 perturbed = tbn * tangentNormal;
    float pLenSq = dot(perturbed, perturbed);
    return pLenSq > 1e-8 ? (perturbed * inversesqrt(pLenSq)) : N;
}

vec3 illumoEvaluateDirectLight(
    vec3 N,
    vec3 V,
    vec3 L,
    vec3 radiance,
    vec3 albedo,
    float metallic,
    float roughness)
{
    float nDotL = max(dot(N, L), 0.0);
    if (nDotL <= 0.0) {
        return vec3(0.0);
    }
    float nDotV = max(dot(N, V), 1e-4);
    vec3 halfSum = V + L;
    float halfLenSq = dot(halfSum, halfSum);
    vec3 H = halfLenSq > 1e-8 ? (halfSum * inversesqrt(halfLenSq)) : N;
    float nDotH = max(dot(N, H), 0.0);
    float vDotH = max(dot(V, H), 0.0);

    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float denom = nDotH * nDotH * (a2 - 1.0) + 1.0;
    float D_pi = a2 / max(denom * denom, 1e-7);

    float k = (roughness + 1.0);
    k = (k * k) * 0.125;
    float gV = nDotV / (nDotV * (1.0 - k) + k);
    float gL = nDotL / (nDotL * (1.0 - k) + k);
    float G = gV * gL;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = F0 + (vec3(1.0) - F0) * pow(clamp(1.0 - vDotH, 0.0, 1.0), 5.0);

    vec3 specular = (D_pi * G * F) / max(4.0 * nDotV * nDotL, 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo + specular) * radiance * nDotL;
}

float illumoSampleSingleShadowMap(
    sampler2D shadowMap,
    vec3 fragPos,
    vec3 normal,
    float nDotL,
    mat4 lightSpaceMatrix,
    float shadowBias,
    float shadowSlopeScale,
    float shadowNormalOffset,
    int shadowPcf)
{
    vec2 mapSize = vec2(textureSize(shadowMap, 0));
    vec2 texelSize = 1.0 / mapSize;
    float invHalfExtent = length(vec3(
        lightSpaceMatrix[0].x, lightSpaceMatrix[1].x, lightSpaceMatrix[2].x));
    float worldTexel =
        invHalfExtent > 1e-5 ? 2.0 / (invHalfExtent * mapSize.x) : 0.0;

    float sinTheta = sqrt(max(1.0 - nDotL * nDotL, 0.0));
    float pcfTexelRadius = shadowPcf > 0 ? 2.5 : 1.25;
    float minTexelOffset =
        shadowNormalOffset > 0.0 ? pcfTexelRadius * worldTexel : 0.0;
    vec3 offsetPos =
        fragPos + normal * (max(shadowNormalOffset, minTexelOffset) * sinTheta);

    vec4 lightClip = lightSpaceMatrix * vec4(offsetPos, 1.0);
    vec3 projCoords = lightClip.xyz / lightClip.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float invHalfDepthSpan = length(vec3(
        lightSpaceMatrix[0].z, lightSpaceMatrix[1].z, lightSpaceMatrix[2].z));
    const float kReferenceDepthSpan = 16.0;
    float depthScale = invHalfDepthSpan > 1e-5
        ? (0.5 * kReferenceDepthSpan * invHalfDepthSpan)
        : 1.0;
    float tanTheta = min(sinTheta / max(nDotL, 0.1), 3.0);
    float bias = (shadowBias + shadowSlopeScale * (1.0 - nDotL) +
        shadowBias * tanTheta) * depthScale;

    if (shadowPcf <= 0) {
        float pcfDepth = texture(shadowMap, projCoords.xy).r;
        return currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }

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
            float pcfDepth = texture(shadowMap, sampleUv).r;
            float occluded = currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            shadow += occluded * wx[ix] * wy[iy];
        }
    }
    return shadow / 9.0;
}

float illumoSampleShadowQuadrant(
    sampler2D shadowMap,
    vec3 fragPos,
    vec3 normal,
    float nDotL,
    mat4 lightMatrix,
    int quadrant,
    float shadowBias,
    float shadowSlopeScale,
    float shadowNormalOffset,
    int shadowPcf,
    bool perspectiveLight)
{
    vec2 atlasSize = vec2(textureSize(shadowMap, 0));
    vec2 atlasTexel = 1.0 / atlasSize;
    vec2 quadSize = atlasSize * 0.5;
    vec2 uvOrigin = vec2(
        float(quadrant & 1) * 0.5,
        float((quadrant >> 1) & 1) * 0.5);

    float invHalfExtent = length(vec3(
        lightMatrix[0].x, lightMatrix[1].x, lightMatrix[2].x));
    float perspDepth = 1.0;
    if (perspectiveLight) {
        vec4 rawClip = lightMatrix * vec4(fragPos, 1.0);
        if (rawClip.w <= 0.0001) {
            return 0.0;
        }
        perspDepth = rawClip.w;
    }
    float worldTexel = invHalfExtent > 1e-5
        ? (2.0 * perspDepth) / (invHalfExtent * quadSize.x)
        : 0.0;

    float sinTheta = sqrt(max(1.0 - nDotL * nDotL, 0.0));
    float pcfTexelRadius = shadowPcf > 0 ? 2.5 : 1.25;
    float minTexelOffset =
        shadowNormalOffset > 0.0 ? pcfTexelRadius * worldTexel : 0.0;
    vec3 offsetPos =
        fragPos + normal * (max(shadowNormalOffset, minTexelOffset) * sinTheta);

    vec4 lightClip = lightMatrix * vec4(offsetPos, 1.0);
    if (lightClip.w <= 0.0001) {
        return 0.0;
    }
    vec3 projCoords = (lightClip.xyz / lightClip.w) * 0.5 + 0.5;
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }

    float currentDepth = projCoords.z;
    float depthScale = 1.0;
    if (perspectiveLight) {
        depthScale = clamp((1.0 - currentDepth) * 8.0, 0.02, 1.0);
    } else {
        float invHalfDepthSpan = length(vec3(
            lightMatrix[0].z, lightMatrix[1].z, lightMatrix[2].z));
        const float kReferenceDepthSpan = 16.0;
        depthScale = invHalfDepthSpan > 1e-5
            ? (0.5 * kReferenceDepthSpan * invHalfDepthSpan)
            : 1.0;
    }
    float tanTheta = min(sinTheta / max(nDotL, 0.1), 3.0);
    float bias = (shadowBias + shadowSlopeScale * (1.0 - nDotL) +
        shadowBias * tanTheta) * depthScale;

    vec2 uvMin = uvOrigin + 0.5 * atlasTexel;
    vec2 uvMax = uvOrigin + vec2(0.5) - 0.5 * atlasTexel;
    if (shadowPcf <= 0) {
        vec2 atlasUv = clamp(uvOrigin + projCoords.xy * 0.5, uvMin, uvMax);
        float pcfDepth = texture(shadowMap, atlasUv).r;
        return currentDepth - bias > pcfDepth ? 1.0 : 0.0;
    }

    vec2 texelPos = projCoords.xy * quadSize - 0.5;
    vec2 baseTexel = floor(texelPos);
    vec2 frac = texelPos - baseTexel;
    vec2 baseUv = uvOrigin + (baseTexel + 0.5) * atlasTexel;
    vec4 wx = vec4(1.0 - frac.x, 1.0, 1.0, frac.x);
    vec4 wy = vec4(1.0 - frac.y, 1.0, 1.0, frac.y);

    float shadow = 0.0;
    for (int ix = 0; ix < 4; ++ix) {
        for (int iy = 0; iy < 4; ++iy) {
            vec2 sampleUv = clamp(
                baseUv + vec2(float(ix - 1), float(iy - 1)) * atlasTexel,
                uvMin,
                uvMax);
            float pcfDepth = texture(shadowMap, sampleUv).r;
            float occluded = currentDepth - bias > pcfDepth ? 1.0 : 0.0;
            shadow += occluded * wx[ix] * wy[iy];
        }
    }
    return shadow / 9.0;
}

float illumoCalculateDirectionalShadow(
    sampler2D shadowMap,
    vec3 fragPos,
    vec3 normal,
    vec3 lightDir,
    float viewDepth,
    mat4 fallbackMatrix,
    float shadowBias,
    float shadowSlopeScale,
    float shadowNormalOffset,
    int shadowPcf)
{
    float nDotL = dot(normal, lightDir);
    if (nDotL <= 0.0) {
        return 1.0;
    }
    if ((uLightCountsAndFlags.w & 4) != 0) {
        int cascadeCount = clamp(uLightCountsAndFlags.y, 0, 3);
        if (cascadeCount <= 0) {
            return 0.0;
        }
        int c = 0;
        if (cascadeCount > 1 && viewDepth > uCascadeSplitsAndTexel.x) {
            c = (cascadeCount > 2 && viewDepth > uCascadeSplitsAndTexel.y) ? 2 : 1;
        }
        vec2 quadSize = vec2(textureSize(shadowMap, 0)) * 0.5;
        float guard = 2.5 / max(quadSize.x, 1.0);
        for (int step = 0; step < 2; ++step) {
            if (c + 1 >= cascadeCount) {
                break;
            }
            vec4 clip = uAtlasLightMatrices[c] * vec4(fragPos, 1.0);
            vec3 uv = (clip.xyz / max(abs(clip.w), 1e-5)) * 0.5 + 0.5;
            if (uv.x < guard || uv.x > 1.0 - guard ||
                uv.y < guard || uv.y > 1.0 - guard ||
                uv.z > 1.0 || uv.z < 0.0) {
                c += 1;
            } else {
                break;
            }
        }
        return illumoSampleShadowQuadrant(
            shadowMap,
            fragPos,
            normal,
            nDotL,
            uAtlasLightMatrices[c],
            c,
            shadowBias,
            shadowSlopeScale,
            shadowNormalOffset,
            shadowPcf,
            false);
    }
    return illumoSampleSingleShadowMap(
        shadowMap,
        fragPos,
        normal,
        nDotL,
        fallbackMatrix,
        shadowBias,
        shadowSlopeScale,
        shadowNormalOffset,
        shadowPcf);
}

#endif
)";

  s_defaultsRegistered = true;
}

namespace {

struct PreprocessContext
{
  const PreprocessOptions* options = nullptr;
  std::vector<std::string> includeStack;
  std::unordered_set<std::string> pragmaOnceSet;
  std::vector<std::string> dependencies;
  std::unordered_set<std::string> dependencySet;
  std::string versionLine;
  int recursionDepth = 0;
};

static bool
parseInclude(const std::string& line, std::string& outPath, bool& outIsAngled)
{
  std::string trimmed = trimString(line);
  if (!startsWith(trimmed, "#include")) {
    return false;
  }
  size_t restStart = 8; // length of "#include"
  while (restStart < trimmed.size() &&
         (trimmed[restStart] == ' ' || trimmed[restStart] == '\t')) {
    ++restStart;
  }
  if (restStart >= trimmed.size()) {
    return false;
  }

  char openChar = trimmed[restStart];
  char closeChar = '\0';
  if (openChar == '<') {
    closeChar = '>';
    outIsAngled = true;
  } else if (openChar == '\"') {
    closeChar = '\"';
    outIsAngled = false;
  } else {
    return false;
  }

  size_t pathStart = restStart + 1;
  size_t pathEnd = trimmed.find(closeChar, pathStart);
  if (pathEnd == std::string::npos) {
    return false;
  }

  outPath = trimmed.substr(pathStart, pathEnd - pathStart);
  return true;
}

static bool
isPragmaOnceDirective(const std::string& line)
{
  std::string trimmed = trimString(line);
  return trimmed == "#pragma once";
}

static bool
isVersionDirective(const std::string& line, std::string& outVersion)
{
  std::string trimmed = trimString(line);
  if (startsWith(trimmed, "#version")) {
    outVersion = trimmed;
    return true;
  }
  return false;
}

static std::string
canonicalPathString(const std::string& path)
{
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(path, ec);
  if (ec) {
    return std::filesystem::path(path).lexically_normal().string();
  }
  std::filesystem::path canon = std::filesystem::weakly_canonical(abs, ec);
  std::string result = (ec ? abs.lexically_normal() : canon).string();
  std::replace(result.begin(), result.end(), '\\', '/');
#ifdef _WIN32
  std::transform(
    result.begin(), result.end(), result.begin(), [](char c) -> char {
      return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
#endif
  return result;
}

static bool
resolveDiskFile(const std::string& includePath,
                const std::string& currentSourcePath,
                const std::vector<std::string>& searchDirs,
                std::string& outResolvedPath)
{
  // 1. Relative to directory of current file
  if (!currentSourcePath.empty()) {
    std::filesystem::path parentDir =
      std::filesystem::path(currentSourcePath).parent_path();
    std::filesystem::path candidate = parentDir / includePath;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) &&
        !std::filesystem::is_directory(candidate, ec)) {
      outResolvedPath = canonicalPathString(candidate.string());
      return true;
    }
  }

  // 2. Relative to current working directory
  {
    std::filesystem::path candidate = std::filesystem::path(includePath);
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) &&
        !std::filesystem::is_directory(candidate, ec)) {
      outResolvedPath = canonicalPathString(candidate.string());
      return true;
    }
  }

  // 3. Search directories
  for (size_t i = 0; i < searchDirs.size(); ++i) {
    std::filesystem::path candidate =
      std::filesystem::path(searchDirs[i]) / includePath;
    std::error_code ec;
    if (std::filesystem::exists(candidate, ec) &&
        !std::filesystem::is_directory(candidate, ec)) {
      outResolvedPath = canonicalPathString(candidate.string());
      return true;
    }
  }

  return false;
}

static bool
processSourceInternal(const std::string& source,
                      const std::string& currentIdentifier,
                      PreprocessContext& ctx,
                      std::stringstream& output,
                      std::string& outError)
{
  if (ctx.recursionDepth > 32) {
    outError = "Maximum include recursion depth (32) exceeded";
    return false;
  }

  std::stringstream ss(source);
  std::string line;
  int lineNumber = 0;

  while (std::getline(ss, line)) {
    ++lineNumber;

    // Check version directive
    std::string ver;
    if (isVersionDirective(line, ver)) {
      if (ctx.versionLine.empty()) {
        ctx.versionLine = ver;
      }
      // Omit #version from intermediate line emission; placed at the very top
      // later
      continue;
    }

    // Check pragma once
    if (isPragmaOnceDirective(line)) {
      ctx.pragmaOnceSet.insert(currentIdentifier);
      continue;
    }

    // Check include directive
    std::string incPath;
    bool isAngled = false;
    if (parseInclude(line, incPath, isAngled)) {
      std::string normalizedModule =
        ShaderPreprocessor::HasVirtualInclude(incPath) ? incPath : "";
      if (normalizedModule.empty() && isAngled) {
        // Try stripping angle brackets / lookup
        if (ShaderPreprocessor::HasVirtualInclude(incPath)) {
          normalizedModule = incPath;
        }
      }

      std::string resolvedDiskPath;
      bool isVirtual = !normalizedModule.empty();

      if (!isVirtual) {
        if (!resolveDiskFile(incPath,
                             currentIdentifier,
                             ctx.options->searchDirectories,
                             resolvedDiskPath)) {
          outError = "Could not resolve include: '" + incPath + "' from '" +
                     currentIdentifier + "'";
          return false;
        }
      }

      const std::string targetId =
        isVirtual ? ("virtual:" + incPath) : resolvedDiskPath;

      // Check circular inclusion
      for (size_t i = 0; i < ctx.includeStack.size(); ++i) {
        if (ctx.includeStack[i] == targetId) {
          outError = "Circular include detected: '" + targetId +
                     "' already in include stack";
          return false;
        }
      }

      // Check pragma once
      if (ctx.pragmaOnceSet.find(targetId) != ctx.pragmaOnceSet.end()) {
        continue;
      }

      std::string contentToInclude;
      if (isVirtual) {
        contentToInclude = ShaderPreprocessor::GetVirtualInclude(incPath);
      } else {
        contentToInclude = readFileToString(resolvedDiskPath);
        if (contentToInclude.empty()) {
          // Check if file truly empty vs read failure
          std::error_code ec;
          if (std::filesystem::file_size(resolvedDiskPath, ec) > 0 || ec) {
            outError =
              "Failed to read include file: '" + resolvedDiskPath + "'";
            return false;
          }
        }
        if (ctx.dependencySet.insert(resolvedDiskPath).second) {
          ctx.dependencies.push_back(resolvedDiskPath);
        }
      }

      // Preprocess the included content
      ctx.includeStack.push_back(targetId);
      ctx.recursionDepth += 1;

      if (ctx.options->emitLineDirectives) {
        output << "#line 1\n";
      }

      if (!processSourceInternal(
            contentToInclude, targetId, ctx, output, outError)) {
        return false;
      }

      if (ctx.options->emitLineDirectives) {
        output << "\n#line " << (lineNumber + 1) << "\n";
      }

      ctx.recursionDepth -= 1;
      ctx.includeStack.pop_back();
      continue;
    }

    output << line << "\n";
  }

  return true;
}

} // namespace

PreprocessResult
ShaderPreprocessor::Process(const std::string& source,
                            const PreprocessOptions& options)
{
  ILLUMO_PROFILE_ZONE("ShaderPreprocessor.Process");
  RegisterDefaultModules();

  PreprocessContext ctx;
  ctx.options = &options;
  if (!options.sourcePath.empty()) {
    std::string canon = canonicalPathString(options.sourcePath);
    ctx.includeStack.push_back(canon);
  }

  std::stringstream body;
  std::string error;
  if (!processSourceInternal(source, options.sourcePath, ctx, body, error)) {
    PreprocessResult result;
    result.success = false;
    result.errorMessage = error;
    result.fileDependencies = ctx.dependencies;
    return result;
  }

  std::stringstream finalSource;

  // 1. Version directive first
  if (!ctx.versionLine.empty()) {
    finalSource << ctx.versionLine << "\n";
  }

  // 2. Defines immediately after version
  for (size_t i = 0; i < options.defines.size(); ++i) {
    const std::string& def = options.defines[i];
    std::string trimmedDef = trimString(def);
    if (trimmedDef.empty()) {
      continue;
    }
    size_t eqPos = trimmedDef.find('=');
    if (eqPos != std::string::npos) {
      std::string key = trimString(trimmedDef.substr(0, eqPos));
      std::string val = trimString(trimmedDef.substr(eqPos + 1));
      finalSource << "#define " << key << " " << val << "\n";
    } else {
      finalSource << "#define " << trimmedDef << "\n";
    }
  }

  // 3. Body
  finalSource << body.str();

  PreprocessResult result;
  result.success = true;
  result.source = finalSource.str();
  result.fileDependencies = ctx.dependencies;
  return result;
}

PreprocessResult
ShaderPreprocessor::ProcessFile(const std::string& filePath,
                                const PreprocessOptions& options,
                                const IAssetSource* fileSource)
{
  ILLUMO_PROFILE_ZONE("ShaderPreprocessor.ProcessFile");
  // Through the default source unless told otherwise, so a distribution's
  // engine package serves the engine's shaders.
  const IAssetSource* files =
    fileSource != nullptr ? fileSource : DefaultAssetSource();
  std::vector<unsigned char> bytes;
  if (files == nullptr || !files->read(files->canonical(filePath), bytes)) {
    PreprocessResult result;
    result.success = false;
    result.errorMessage = "Failed to open shader file: " + filePath;
    return result;
  }
  const std::string source(bytes.begin(), bytes.end());

  PreprocessOptions fileOptions = options;
  if (fileOptions.sourcePath.empty()) {
    fileOptions.sourcePath = filePath;
  }
  return Process(source, fileOptions);
}
