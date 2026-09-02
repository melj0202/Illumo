#pragma once
#include <array>
#include <string>

enum class TextureFilter
{
  Nearest,
  Linear
};

enum class TextureWrap
{
  ClampToEdge,
  Repeat
};

enum class TextureFormat : uint8_t
{
  RGBA8,
  RGB8,
  R8,
  RGBA16F,
  RG16F,
  R16F,
  Depth24,
  Depth24Stencil8,
  None
};

struct TextureOptions
{
  TextureFilter filter = TextureFilter::Nearest;
  TextureWrap wrapX = TextureWrap::ClampToEdge;
  TextureWrap wrapY = TextureWrap::ClampToEdge;
  bool generateMipmaps = false;
};

struct TextureInfo
{
  int width = 0;
  int height = 0;
  int channels = 0;
};

class ITexture
{
public:
  ITexture() = default;
  virtual ~ITexture() = default;
  virtual void Bind(unsigned int slot) const = 0;
  virtual unsigned int getID() const = 0;
  virtual std::array<int, 2> getSize() const = 0;
  virtual int getChannels() const = 0;
  virtual void Destroy() = 0;

protected:
  unsigned int _textureID;
  std::array<int, 2> _size;
};
