#pragma once

#include <Illumo/Foundation/MathTypes.h>
#include <Illumo/Rendering/Primitives/PrimitiveTypes.h>
#include <Illumo/Scene/Transform3D.h>
#include <cstddef>
#include <string>
#include <vector>

enum class SceneNodeKind
{
  Empty,
  FilledRect,
  FilledEllipse,
  FilledTriangle,
  SolidCube,
  SolidPyramid,
  WireSphere
};

enum class IlscWorldMode
{
  World2D,
  World3D
};

struct PrimitivePayload
{
  Vector3 extent{ 0.5f, 0.5f, 0.5f };
  ColorRgba color{ 200, 200, 200, 255 };
};

struct IlscNode
{
  std::string id;
  std::string parentId;
  std::string name;
  bool enabled = true;
  bool visible = true;
  Transform3D transform;
  SceneNodeKind kind = SceneNodeKind::Empty;
  PrimitivePayload primitive;
};

struct IlscCameraState
{
  double x = 0.0;
  double y = 0.0;
  float zoom = 32.0f;
  float yaw = 0.7f;
  float pitch = 0.45f;
};

struct IlscDocument
{
  int version = 1;
  IlscWorldMode worldMode = IlscWorldMode::World2D;
  IlscCameraState camera;
  std::vector<IlscNode> nodes;
};

struct EditorSceneDetail
{
  size_t nodeCount = 0;
  IlscWorldMode worldMode = IlscWorldMode::World2D;
  bool hasSelection = false;
  std::string selectedId;
  std::string selectedName;
  SceneNodeKind selectedKind = SceneNodeKind::Empty;
  Transform3D transform;
  Vector3 extent{ 0.5f, 0.5f, 0.5f };
  ColorRgba color{ 200, 200, 200, 255 };
};

class IlscCodec
{
public:
  static const int kVersion = 1;

  static bool parse(const std::string& text,
                    IlscDocument* document,
                    std::string* error);
  static std::string encode(const IlscDocument& document);
  static bool readFile(const std::string& path,
                       IlscDocument* document,
                       std::string* error);
  static bool writeFile(const std::string& path,
                        const IlscDocument& document,
                        std::string* error);
  static std::string withIlscExtension(const std::string& filename);
  static const char* kindName(SceneNodeKind kind);
  static bool parseKind(const std::string& text, SceneNodeKind* kind);
  static bool kindHasGeometry(SceneNodeKind kind);
  static bool kindIs2D(SceneNodeKind kind);
  static bool kindIs3D(SceneNodeKind kind);
  static const char* worldModeName(IlscWorldMode mode);
  static bool parseWorldMode(const std::string& text, IlscWorldMode* mode);

private:
  static void setError(std::string* error, const char* message);
};
