#pragma once

#include <Illumo/Rendering/AssetManager.h>
#include <string>

struct GLFWwindow;
class Renderer;

// A unit cube centred on the origin, one colour per face, with bounds.
MeshAssetInfo
enrollCube(Renderer& renderer);

// Renders every parity scene through OpenGL (on `context`, current) and Vulkan
// and compares each image with OpenGL's. 0 on a match, 77 when Vulkan cannot
// start. A nonempty dumpDirectory receives every image as a PPM file.
int
runBackendParity(GLFWwindow* context, const std::string& dumpDirectory);
