#pragma once

#include <IllumoGuest/Frame.h>
#include <IllumoGuest/Input.h>
#include <IllumoGuest/Protocol.h>
#include <IllumoGuest/Services.h>
#include <memory>

// Guest-local C++ API. These objects, virtual calls and containers never cross
// into the native host; Exports.cpp translates the explicit wire protocol.
class GuestApplication
{
public:
  GuestApplication() = default;
  virtual ~GuestApplication() = default;
  GuestApplication(const GuestApplication&) = delete;
  GuestApplication& operator=(const GuestApplication&) = delete;
  GuestApplication(GuestApplication&&) = delete;
  GuestApplication& operator=(GuestApplication&&) = delete;

  virtual GuestDescriptor describe() const = 0;
  virtual bool start(std::span<const std::byte> startup) = 0;
  virtual void update(const GuestInput& input) = 0;
  virtual GuestFrame frame() = 0;
  virtual bool close() = 0;
  virtual void shutdown() = 0;
  virtual std::vector<std::byte> extensionRequest() { return {}; }
  // Reported after each update; the host starts its close sequence.
  virtual bool closeRequested() { return false; }
  GuestServiceQueue& services() { return m_services; }
  // What the host granted at startup: the required capabilities plus any
  // optional ones it offers (for example ProjectFiles with --project).
  std::uint32_t grantedCapabilities() const { return m_granted; }
  bool granted(GuestCapability capability) const
  {
    return (m_granted & static_cast<std::uint32_t>(capability)) != 0;
  }
  // Recorded by the export layer before start().
  void grantCapabilities(std::uint32_t grants) { m_granted = grants; }
  virtual std::vector<std::byte> receive(std::span<const std::byte> message)
  {
    (void)message;
    return {};
  }

private:
  GuestServiceQueue m_services;
  std::uint32_t m_granted = 0;
};

std::unique_ptr<GuestApplication>
CreateGuestApplication();
