#pragma once

#include "IlscCodec.h"
#include <Illumo/Scene/SceneGraph.h>
#include <cstddef>
#include <string>

class EditorDocument
{
public:
  EditorDocument();
  ~EditorDocument();

  EditorDocument(const EditorDocument&) = delete;
  EditorDocument& operator=(const EditorDocument&) = delete;
  EditorDocument(EditorDocument&&) = delete;
  EditorDocument& operator=(EditorDocument&&) = delete;

  void clear();
#if !defined(ILLUMO_SERIAL_GUEST)
  bool loadFromFile(const std::string& path, std::string* error);
  bool saveToFile(const std::string& path, std::string* error);
#endif
  bool loadFromText(const std::string& text, std::string* error);
  std::string encode() const;

  bool isDirty() const { return m_dirty; }
  void markDirty() { m_dirty = true; }
  // The document's saved state now matches `location` (a completed save).
  void markSaved(const std::string& location, const std::string& label)
  {
    setLocation(location, label);
    m_dirty = false;
  }
  // The save-in-place location: a path natively, an opaque grant in WASM.
  const std::string& path() const { return m_path; }
  void setPath(const std::string& path)
  {
    m_path = path;
    m_label.clear();
  }
  void setLocation(const std::string& location, const std::string& label)
  {
    m_path = location;
    m_label = label;
  }
  // What the UI calls the document: its label, else its location.
  const std::string& displayName() const
  {
    return m_label.empty() ? m_path : m_label;
  }
  const IlscCameraState& camera() const { return m_document.camera; }
  void setCamera(const IlscCameraState& camera);
  IlscWorldMode worldMode() const { return m_document.worldMode; }
  void setWorldMode(IlscWorldMode mode);

  size_t nodeCount() const { return m_document.nodes.size(); }
  const IlscNode* nodeAt(size_t index) const;
  const IlscNode* findNode(const std::string& id) const;
  SceneGraph& graph() { return m_graph; }
  const SceneGraph& graph() const { return m_graph; }
  SceneNodeHandle nodeHandle(const std::string& id) const;
  bool setEnabled(const std::string& id, bool enabled);
  bool setVisible(const std::string& id, bool visible);

  std::string createNode(SceneNodeKind kind, const std::string& parentId);
  bool destroySubtree(const std::string& id);
  bool canSetParent(const std::string& id, const std::string& parentId) const;
  bool setParent(const std::string& id, const std::string& parentId);
  bool setTransform(const std::string& id, const Transform3D& transform);
  bool setName(const std::string& id, const std::string& name);
  bool setExtent(const std::string& id, const Vector3& extent);
  bool setColor(const std::string& id, ColorRgba color);
  bool translate(const std::string& id, float dx, float dy);
  bool translate(const std::string& id, const Vector3& deltaWorld);
  bool pick(float worldX, float worldY, std::string* id) const;
  bool pickRay(const Vector3& origin,
               const Vector3& direction,
               std::string* id) const;
  Matrix4 worldMatrix(const std::string& id) const;
  Transform3D makeEditPlaneTransform(float planeX, float planeY) const;
  EditorSceneDetail sceneDetail(const std::string& selectedId) const;

private:
  struct RuntimeNode;
  SceneGraph m_graph;
  std::vector<std::unique_ptr<RuntimeNode>> m_runtime;
  mutable std::vector<SceneRayHit> m_pickCandidates;
  IlscDocument m_document;
  std::string m_path;
  std::string m_label;
  bool m_dirty;
  unsigned int m_nextId;

  std::string allocateId();
  size_t indexOf(const std::string& id) const;
  IlscNode* mutableNode(const std::string& id);
  void rebuildRuntime();
  void updateProxy(size_t index);
  IlscDocument serializationDocument() const;
};
