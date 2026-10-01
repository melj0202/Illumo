#pragma once

#include <Illumo/Rendering/AssetManager.h>
#include <string>

struct GLFWwindow;
class Renderer;

// A unit cube centred on the origin, one colour per face, with bounds.
MeshAssetInfo
enrollCube(Renderer& renderer);

// Renders every parity scene through OpenGL (on `context`, current) and
// Vulkan and compares the images. 0 on a match, 77 when Vulkan is missing.
// A nonempty dumpDirectory receives each pair of images as PPM files.
int
runBackendParity(GLFWwindow* context, const std::string& dumpDirectory);
