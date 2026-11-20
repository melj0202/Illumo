#pragma once

#include <Illumo/Rendering/IBackend.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

struct PooledRenderTargetDesc
{
  std::string name;
  bool windowRelative = true;
  float scale = 1.0f;
  int mipLevel = 0;
  int fixedWidth = 0;
  int fixedHeight = 0;
  std::vector<FramebufferAttachmentDesc> colorAttachments;
  TextureFormat depthStencilFormat = TextureFormat::None;
  TextureFilter depthFilter = TextureFilter::Nearest;
};

struct PooledRenderTarget
{
  FramebufferHandle fboHandle{};
  FramebufferAttachments attachments{};
  int width = 0;
  int height = 0;

  bool isValid() const { return fboHandle.isValid(); }
};

class RenderTargetPool
{
public:
  RenderTargetPool() = default;
  explicit RenderTargetPool(IBackend* backend)
    : m_backend(backend)
  {
  }

  ~RenderTargetPool() { releaseAll(); }

  void setBackend(IBackend* backend)
  {
    if (m_backend != backend) {
      releaseAll();
      m_backend = backend;
    }
  }

  PooledRenderTarget acquire(const PooledRenderTargetDesc& desc,
                             int windowWidth,
                             int windowHeight)
  {
    if (!m_backend) {
      return PooledRenderTarget{};
    }

    int targetWidth = 0;
    int targetHeight = 0;

    if (desc.windowRelative) {
      if (desc.mipLevel > 0) {
        const unsigned int shift =
          static_cast<unsigned int>(std::min(desc.mipLevel, 16));
        const unsigned int baseW =
          windowWidth > 0 ? static_cast<unsigned int>(windowWidth) : 1u;
        const unsigned int baseH =
          windowHeight > 0 ? static_cast<unsigned int>(windowHeight) : 1u;
        targetWidth = std::max(1, static_cast<int>(baseW >> shift));
        targetHeight = std::max(1, static_cast<int>(baseH >> shift));
      } else {
        const float s = (desc.scale > 0.0f) ? desc.scale : 1.0f;
        targetWidth = std::max(1, static_cast<int>(windowWidth * s));
        targetHeight = std::max(1, static_cast<int>(windowHeight * s));
      }
    } else {
      targetWidth = std::max(1, desc.fixedWidth);
      targetHeight = std::max(1, desc.fixedHeight);
    }

    std::unordered_map<std::string, Entry>::iterator it =
      m_entries.find(desc.name);
    if (it != m_entries.end()) {
      if (it->second.target.width == targetWidth &&
          it->second.target.height == targetHeight &&
          sameAttachments(it->second.desc, desc) &&
          it->second.target.isValid()) {
        return it->second.target;
      }
      // Dimensions or attachment formats changed; destroy old and recreate
      if (it->second.target.fboHandle.isValid()) {
        m_backend->DestroyFramebuffer(it->second.target.fboHandle);
      }
      m_entries.erase(it);
    }

    FramebufferDesc fbDesc;
    fbDesc.width = targetWidth;
    fbDesc.height = targetHeight;
    fbDesc.colorAttachments = desc.colorAttachments;
    fbDesc.depthStencilFormat = desc.depthStencilFormat;
    fbDesc.depthFilter = desc.depthFilter;

    FramebufferAttachments atts;
    FramebufferHandle handle = m_backend->CreateFramebuffer(fbDesc, &atts);
    if (!handle.isValid()) {
      return PooledRenderTarget{};
    }

    PooledRenderTarget created;
    created.fboHandle = handle;
    created.attachments = atts;
    created.width = targetWidth;
    created.height = targetHeight;

    Entry entry;
    entry.desc = desc;
    entry.target = created;
    m_entries[desc.name] = entry;

    return created;
  }

  PooledRenderTarget get(const std::string& name) const
  {
    std::unordered_map<std::string, Entry>::const_iterator it =
      m_entries.find(name);
    if (it != m_entries.end()) {
      return it->second.target;
    }
    return PooledRenderTarget{};
  }

  void releaseAll()
  {
    if (m_backend) {
      for (std::unordered_map<std::string, Entry>::iterator it =
             m_entries.begin();
           it != m_entries.end();
           ++it) {
        if (it->second.target.fboHandle.isValid()) {
          m_backend->DestroyFramebuffer(it->second.target.fboHandle);
        }
      }
    }
    m_entries.clear();
  }

  size_t count() const { return m_entries.size(); }

private:
  struct Entry
  {
    PooledRenderTargetDesc desc;
    PooledRenderTarget target;
  };

  static bool sameAttachments(const PooledRenderTargetDesc& left,
                              const PooledRenderTargetDesc& right)
  {
    if (left.depthStencilFormat != right.depthStencilFormat ||
        left.depthFilter != right.depthFilter ||
        left.colorAttachments.size() != right.colorAttachments.size()) {
      return false;
    }
    for (size_t i = 0; i < left.colorAttachments.size(); ++i) {
      const FramebufferAttachmentDesc& a = left.colorAttachments[i];
      const FramebufferAttachmentDesc& b = right.colorAttachments[i];
      if (a.format != b.format || a.filter != b.filter || a.wrap != b.wrap) {
        return false;
      }
    }
    return true;
  }

  IBackend* m_backend = nullptr;
  std::unordered_map<std::string, Entry> m_entries;
};
