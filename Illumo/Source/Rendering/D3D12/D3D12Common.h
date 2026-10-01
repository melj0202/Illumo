#pragma once

// Direct3D 12 and DXGI headers for this directory only (docs/
// d3d12-backend-plan.md). Nothing outside Rendering/D3D12/ includes this.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <string>

// An owning COM reference: releases on destruction, copies add a reference.
template<typename T>
class D3D12Ref
{
public:
  D3D12Ref() = default;
  ~D3D12Ref() { reset(); }
  D3D12Ref(const D3D12Ref& other)
    : m_pointer(other.m_pointer)
  {
    if (m_pointer != nullptr) {
      m_pointer->AddRef();
    }
  }
  D3D12Ref& operator=(const D3D12Ref& other)
  {
    if (this != &other) {
      T* pointer = other.m_pointer;
      if (pointer != nullptr) {
        pointer->AddRef();
      }
      reset();
      m_pointer = pointer;
    }
    return *this;
  }
  D3D12Ref(D3D12Ref&& other) noexcept
    : m_pointer(other.m_pointer)
  {
    other.m_pointer = nullptr;
  }
  D3D12Ref& operator=(D3D12Ref&& other) noexcept
  {
    if (this != &other) {
      reset();
      m_pointer = other.m_pointer;
      other.m_pointer = nullptr;
    }
    return *this;
  }

  T* get() const { return m_pointer; }
  T* operator->() const { return m_pointer; }
  explicit operator bool() const { return m_pointer != nullptr; }
  bool operator==(const D3D12Ref& other) const
  {
    return m_pointer == other.m_pointer;
  }

  void reset()
  {
    if (m_pointer != nullptr) {
      m_pointer->Release();
      m_pointer = nullptr;
    }
  }
  // Releases the current object and exposes the slot a creation call fills.
  T** put()
  {
    reset();
    return &m_pointer;
  }
  void** putVoid() { return reinterpret_cast<void**>(put()); }
  // Another interface of the same object; false when it has none.
  template<typename U>
  bool query(D3D12Ref<U>& out) const
  {
    return m_pointer != nullptr &&
           SUCCEEDED(m_pointer->QueryInterface(__uuidof(U), out.putVoid()));
  }

private:
  T* m_pointer = nullptr;
};

// "0x887A0005 (device removed)" style text for logs.
std::string
d3d12ResultText(HRESULT result);
