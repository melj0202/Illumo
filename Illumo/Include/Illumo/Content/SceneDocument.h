#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Scene/Transform3D.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// Plain-value model of one .ilsc format 2 scene. It is what IlscCodec reads
// and writes and what SceneInstance builds a live graph from; it carries no
// handles, attachments or renderer state. Node order is graph preorder, so
// sibling order is the order of children in the vector.

enum class SceneWorldMode
{
  World2D,
  World3D
};

enum class SceneAssetType
{
  Mesh,
  Texture,
  // A texture divided into a grid of equal cells, addressed by sprites.
  Atlas,
  // One image laid out as a horizontal or vertical cube cross.
  CubemapCross,
  // Six square images in +X, -X, +Y, -Y, +Z, -Z order.
  CubemapFaces
};

enum class SceneTextureFilter
{
  Nearest,
  Linear
};

enum class SceneTextureWrap
{
  Clamp,
  Repeat
};

struct SceneTextureOptions
{
  SceneTextureFilter filter = SceneTextureFilter::Linear;
  SceneTextureWrap wrap = SceneTextureWrap::Clamp;
  bool mipmaps = false;
};

struct SceneMeshOptions
{
  bool centerAndNormalize = false;
  float targetRadius = 1.0f;
  bool flipV = true;
  bool generateNormals = true;
};

struct SceneAsset
{
  std::string id;
  SceneAssetType type = SceneAssetType::Texture;
  // As written: package-relative ("meshes/a.obj") or absolute virtual
  // ("/engine/Skybox/sky.png"). Unused by CubemapFaces.
  std::string path;
  // CubemapFaces only.
  std::array<std::string, 6> faces;
  SceneTextureOptions texture;
  SceneMeshOptions mesh;
  // Atlas only: grid columns and rows, each at least 1.
  int columns = 1;
  int rows = 1;
};

enum class ScenePrimitiveShape
{
  Rect,
  Ellipse,
  Triangle,
  Cube,
  Pyramid,
  Sphere,
  WireCube,
  WireSphere
};

struct ScenePrimitive
{
  ScenePrimitiveShape shape = ScenePrimitiveShape::Cube;
  // Half-size along each local axis; flat shapes ignore z.
  Vector3 extent{ 0.5f, 0.5f, 0.5f };
  ColorRgba color{ 200, 200, 200, 255 };
};

struct SceneMeshRenderer
{
  std::string asset;
  ColorRgba tint{ 255, 255, 255, 255 };
  bool castShadows = true;
};

enum class SceneSpriteFacing
{
  World,
  Billboard
};

struct SceneSprite
{
  // A Texture or Atlas asset.
  std::string texture;
  // Normalized source rectangle, used when hasCell is false.
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 1.0f;
  float v1 = 1.0f;
  // Atlas cell, used when hasCell is true (requires an Atlas asset).
  bool hasCell = false;
  int column = 0;
  int row = 0;
  float width = 1.0f;
  float height = 1.0f;
  SceneSpriteFacing facing = SceneSpriteFacing::World;
  ColorRgba tint{ 255, 255, 255, 255 };
  bool flipX = false;
  bool flipY = false;
};

// Directional light. It shines along the node's local -Y axis, so an
// unrotated light points straight down; the first enabled light in preorder
// replaces the environment sun.
struct SceneLight
{
  Vector3 color{ 1.0f, 1.0f, 1.0f };
  float intensity = 1.0f;
  bool shadows = true;
};

enum class SceneProjection
{
  Orthographic,
  Perspective
};

struct SceneCamera
{
  SceneProjection projection = SceneProjection::Perspective;
  float fovDegrees = 50.0f;
  float nearPlane = 0.1f;
  float farPlane = 250.0f;
  // Orthographic pixels per world unit.
  float zoom = 32.0f;
  bool primary = false;
};

// A namespaced component ("vendor.name") preserved verbatim for the program
// that understands it. data is canonical compact JSON (an object).
struct SceneOpaqueComponent
{
  std::string type;
  std::string data = "{}";
};

enum class SceneComponentType
{
  Primitive,
  Mesh,
  Sprite,
  Light,
  Camera,
  Opaque
};

struct SceneComponent
{
  std::variant<ScenePrimitive,
               SceneMeshRenderer,
               SceneSprite,
               SceneLight,
               SceneCamera,
               SceneOpaqueComponent>
    value;

  SceneComponentType type() const
  {
    return static_cast<SceneComponentType>(value.index());
  }
};

struct SceneNode
{
  std::string id;
  // Empty for a root.
  std::string parentId;
  std::string name;
  bool enabled = true;
  bool visible = true;
  Transform3D transform;
  std::vector<std::string> tags;
  std::vector<SceneComponent> components;

  // The first component of a core type, or nullptr.
  const SceneComponent* find(SceneComponentType type) const;
  SceneComponent* find(SceneComponentType type);
  // The first opaque component with this namespaced type, or nullptr.
  const SceneComponent* findOpaque(std::string_view type) const;
};

struct SceneSun
{
  // Direction the light travels (from the sun toward the scene).
  Vector3 direction{ -0.4f, -1.0f, -0.3f };
  Vector3 color{ 1.0f, 0.96f, 0.9f };
  float intensity = 1.0f;
  bool shadows = true;
};

struct SceneEnvironment
{
  // A CubemapCross or CubemapFaces asset id, or empty for no sky.
  std::string skybox;
  Vector3 skyboxTint{ 1.0f, 1.0f, 1.0f };
  Vector3 ambient{ 0.25f, 0.27f, 0.3f };
  // Without a sun (and without a light component) the scene is unlit.
  bool hasSun = true;
  SceneSun sun;
};

struct SceneMetadata
{
  std::string title;
  std::string author;
  std::string description;
};

// Program-private view state for IllEd. It never makes a document dirty and
// other programs ignore it.
struct SceneEditorState
{
  double cameraX = 0.0;
  double cameraY = 0.0;
  float zoom = 32.0f;
  float yaw = 0.7f;
  float pitch = 0.45f;
  float gridSpacing = 1.0f;
  bool gridVisible = true;
  bool snapEnabled = false;
  float snapTranslate = 0.5f;
  float snapRotateDegrees = 15.0f;
  float snapScale = 0.1f;
};

// A namespaced top-level extension entry ("illumogame.world") preserved
// verbatim; data is canonical compact JSON (any value).
struct SceneExtension
{
  std::string key;
  std::string data = "null";
};

struct SceneDocument
{
  static constexpr int kFormatMajor = 2;
  static constexpr int kFormatMinor = 0;
  static constexpr std::size_t kMaximumNodes = 262144;
  static constexpr std::size_t kMaximumAssets = 65536;
  static constexpr std::size_t kMaximumComponents = 16;
  static constexpr std::size_t kMaximumTags = 32;

  // The minor version a file declared; files are always written at
  // kFormatMinor.
  int formatMinor = kFormatMinor;
  SceneMetadata metadata;
  SceneWorldMode worldMode = SceneWorldMode::World2D;
  SceneEnvironment environment;
  std::vector<SceneAsset> assets;
  std::vector<SceneNode> nodes;
  std::vector<SceneExtension> extensions;
  bool hasEditor = false;
  SceneEditorState editor;

  const SceneNode* findNode(std::string_view id) const;
  SceneNode* findNode(std::string_view id);
  const SceneAsset* findAsset(std::string_view id) const;
};

// Checks every invariant the codec enforces on read (identifiers, ranges,
// references, component uniqueness, parents before children and no cycles),
// so programmatically built documents meet the same contract. Returns false
// with a message naming the first problem.
bool
validateSceneDocument(const SceneDocument& document, std::string& error);

// Field-by-field equality (ids, parent, flags, exact transform floats, tags and
// components, opaque data by canonical text).
bool
sceneNodesEqual(const SceneNode& a, const SceneNode& b);
bool
sceneComponentsEqual(const SceneComponent& a, const SceneComponent& b);

// Printable single-line identifier of 1-128 bytes (node and asset ids).
bool
validSceneId(std::string_view id);

// A namespaced type or extension key: 3-64 bytes of [a-z0-9._-] with at least
// one '.', no leading, trailing or doubled '.'.
bool
validSceneNamespace(std::string_view name);

const char*
sceneAssetTypeName(SceneAssetType type);
const char*
scenePrimitiveShapeName(ScenePrimitiveShape shape);
bool
parseScenePrimitiveShape(std::string_view text, ScenePrimitiveShape& shape);
