#include "VulkanTexture.h"

VulkanTexture::VulkanTexture(const unsigned char* data,
                             int width,
                             int height,
                             int channels,
                             const TextureOptions& options)
{
  (void)data;
  (void)width;
  (void)height;
  (void)channels;
  (void)options;
}

VulkanTexture::VulkanTexture(
  const std::array<const unsigned char*, 6>& facesData,
  int width,
  int height,
  int channels)
  : m_isCubemap(true)
{
  (void)facesData;
  (void)width;
  (void)height;
  (void)channels;
}

VulkanTexture::~VulkanTexture()
{
  Destroy();
}

void
VulkanTexture::Bind(unsigned int slot) const
{
  (void)slot;
}

void
VulkanTexture::Destroy()
{
}
