#pragma once

#include <Illumo/Content/SceneDocument.h>
#include <cstddef>
#include <string>
#include <vector>

// What IllEd does with a file from the virtual file tree: a mesh or texture
// becomes a node plus an asset entry when dropped into the viewport; scenes
// open; everything else is only browsable.
enum class EditorAssetKind
{
  Mesh,
  Texture,
  Scene,
  Other
};

class EditorAssets
{
public:
  // One texture upload is at most this many RGBA8 bytes (the runtime's
  // per-texture cap); imports larger than that are refused up front.
  static constexpr std::size_t kMaximumTextureBytes = 16u * 1024u * 1024u;

  static EditorAssetKind kindFor(const std::string& path);
  // The project folder an import of this kind lands in ("meshes",
  // "textures", "scenes" or "assets").
  static const char* importFolder(EditorAssetKind kind);
  // Checks an image's dimensions against the upload cap; other kinds pass.
  static bool validateImport(const std::string& name,
                             const std::vector<unsigned char>& bytes,
                             std::string* error);
  // An asset id derived from the file stem, unique within the document.
  static std::string assetIdFor(const std::string& path,
                                const SceneDocument& document);
  // The node that shows an asset: a mesh renderer or a world sprite. The
  // asset's own id is referenced; placement sets the transform.
  static bool nodeFor(const SceneAsset& asset, SceneNode* node);
};
