#include <Illumo/Rendering/AssetSource.h>

// Guests have no file system: without a supplied source, AssetManager fails
// every load visibly instead of importing WASI file functions.
IAssetSource*
DefaultAssetSource()
{
  return nullptr;
}
