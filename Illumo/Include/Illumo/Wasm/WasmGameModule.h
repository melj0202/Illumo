#pragma once

#include <Illumo/Engine/IModule.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmGameServices.h>
#include <Illumo/Wasm/WasmGuest.h>
#include <Illumo/Wasm/WasmRenderServices.h>

// Generic native shell. Product update, UI and geometry are supplied by a
// GuestApplication reactor. This adapter grants rendering and bounded messages.
class WasmGameModule : public IModule
{
public:
  explicit WasmGameModule(std::vector<std::byte> module,
                          std::vector<std::byte> startup = {},
                          WasmLimits limits = {},
                          std::vector<std::byte> mod = {},
                          std::vector<std::byte> worker = {},
                          WasmFileRoots files = {});
  ~WasmGameModule() override;
  WasmGameModule(const WasmGameModule&) = delete;
  WasmGameModule& operator=(const WasmGameModule&) = delete;
  WasmGameModule(WasmGameModule&&) = delete;
  WasmGameModule& operator=(WasmGameModule&&) = delete;

  bool Start(IllumoContext* context) override;
  void Update(double elapsed) override;
  void DispatchDrawables(Scene* scene) override;
  void Exit() override;
  bool OnCloseRequested() override;
  const std::string& error() const;
  const std::string& modError() const;
  bool hasActiveMod() const;

private:
  void fail(std::string error);
  std::vector<std::byte> m_module;
  std::vector<std::byte> m_startup;
  std::vector<std::byte> m_modModule;
  std::vector<std::byte> m_workerModule;
  WasmFileRoots m_fileRoots;
  WasmGuest m_guest;
  std::unique_ptr<WasmGuest> m_mod;
  std::unique_ptr<WasmFrameRenderer> m_frames;
  std::unique_ptr<WasmGameServices> m_services;
  std::vector<std::byte> m_completions;
  std::string m_error;
  std::string m_modError;
};
