#pragma once

#include <Illumo/Rendering/AssetManager.h>
#include <string>

struct GLFWwindow;
class Renderer;

// A unit cube centred on the origin, one colour per face, with bounds.
MeshAssetInfo
enrollCube(Renderer& renderer);

// Renders every parity scene (or only `sceneFilter` when nonempty) through
// OpenGL (on `context`, current) and Vulkan and compares each image with
// OpenGL's. 0 on a match, 77 when Vulkan cannot start. A nonempty
// dumpDirectory receives every image as a PPM file.
int
runBackendParity(GLFWwindow* context,
                 const std::string& dumpDirectory,
                 const std::string& sceneFilter = "");

// Measures 2560x1440 HDR forward pipeline (2,000 textured PBR instances, 32
// dynamic punctual lights, 4-quadrant 2048x2048 shadow atlas, 5-mip Karis
// bloom, AgX tonemapping, FXAA) on OpenGL and Vulkan, checking zero Vulkan
// renderPassBreaks.
int
runHdrForwardBench(GLFWwindow* context);
