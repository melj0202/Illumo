#pragma once
#include <Illumo/Rendering/ITexture.h>
#include <array>

// Stub texture resource; VulkanBackend does not yet allocate any image or
// image view, so every instance reports zero size and no GPU binding.
class VulkanTexture : public ITexture
{
public:
  VulkanTexture(const unsigned char* data,
                int width,
                int height,
                int channels,
                const TextureOptions& options);
  VulkanTexture(const std::array<const unsigned char*, 6>& facesData,
                int width,
                int height,
                int channels);
  ~VulkanTexture() override;
  VulkanTexture(const VulkanTexture&) = delete;
  VulkanTexture& operator=(const VulkanTexture&) = delete;
  VulkanTexture(VulkanTexture&&) = delete;
  VulkanTexture& operator=(VulkanTexture&&) = delete;

  void Bind(unsigned int slot) const override;
  std::array<int, 2> getSize() const override { return m_size; }
  int getChannels() const override { return m_channels; }
  bool isCubemap() const override { return m_isCubemap; }
  void Destroy() override;

private:
  std::array<int, 2> m_size{ 0, 0 };
  int m_channels = 0;
  bool m_isCubemap = false;
};
