#pragma once

#include <Illumo/Rendering/ITexture.h>
#include <cstddef>
#include <cstdint>

// CPU texel work the Vulkan and Direct3D 12 backends do where OpenGL's pixel
// transfer would convert: uploads into the stored formats and readbacks into
// top-down RGBA8.

// The same rectangle, channel and stride rules as the OpenGL backend's
// texture updates (TextureUploadPolicy::validLayout).
bool
validTextureUpdate(int textureWidth,
                   int textureHeight,
                   int x,
                   int y,
                   int width,
                   int height,
                   int channels,
                   int rowStridePixels);

// Converts `height` rows of `sourceChannels`-channel bytes (row length
// `sourceRowPixels`, 0 for `width`) into tightly packed storage texels of
// `storageBytes` (1: R8, 4: RGBA8). As OpenGL does, missing colour channels
// read 0 and a missing alpha reads 255; R8 storage keeps the first channel.
void
convertTexelsForStorage(const unsigned char* source,
                        int width,
                        int height,
                        int sourceChannels,
                        int sourceRowPixels,
                        int storageBytes,
                        unsigned char* destination);

// Bytes per texel of a render-target format as the GPU stores it, or 0.
unsigned
storedTexelBytes(TextureFormat format);

// Reads `height` rows of stored `format` texels (row pitch `sourceRowBytes`,
// the first row the bottom one, as OpenGL returns them) into top-down RGBA8
// the way glReadPixels(GL_RGBA, GL_UNSIGNED_BYTE) converts them: float
// channels clamp to [0, 1], missing colour channels read 0 and a missing
// alpha 255. RGB8 targets read an opaque alpha.
void
convertReadbackTexels(const unsigned char* source,
                      size_t sourceRowBytes,
                      int width,
                      int height,
                      TextureFormat format,
                      unsigned char* destination);

float
halfToFloat(uint16_t half);
