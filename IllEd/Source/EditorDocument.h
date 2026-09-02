#pragma once

#include "IlscCodec.h"
#include <cstddef>
#include <string>

class EditorDocument
{
public:
  EditorDocument();
  ~EditorDocument() = default;

  EditorDocument(const EditorDocument&) = delete;
  EditorDocument& operator=(const EditorDocument&) = delete;
  EditorDocument(EditorDocument&&) = delete;
  EditorDocument& operator=(EditorDocument&&) = delete;

  void clear();
  bool loadFromFile(const std::string& path, std::string* error);
  bool saveToFile(const std::string& path, std::string* error);
  bool loadFromText(const std::string& text, std::string* error);
  std::string encode() const;

  bool isDirty() const { return m_dirty; }
  void markDirty() { m_dirty = true; }
  const std::string& path() const { return m_path; }
  void setPath(const std::string& path) { m_path = path; }
  const IlscCameraState& camera() const { return m_document.camera; }
  void setCamera(const IlscCameraState& camera);
  IlscWorldMode worldMode() const { return m_document.worldMode; }
  void setWorldMode(IlscWorldMode mode);

  size_t nodeCount() const { return m_document.nodes.size(); }
  const IlscNode* nodeAt(size_t index) const;
  const IlscNode* findNode(const std::string& id) const;
  IlscNode* findNode(const std::string& id);

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
  Matrix4 worldMatrix(const std::string& id) const;
  Transform3D makeEditPlaneTransform(float planeX, float planeY) const;
  EditorSceneDetail sceneDetail(const std::string& selectedId) const;

private:
  IlscDocument m_document;
  std::string m_path;
  bool m_dirty;
  unsigned int m_nextId;

  std::string allocateId();
  void refreshNextId();
  size_t indexOf(const std::string& id) const;
  bool wouldCreateCycle(const std::string& id,
                        const std::string& parentId) const;
  bool isDescendant(const std::string& ancestorId,
                    const std::string& nodeId) const;
};
