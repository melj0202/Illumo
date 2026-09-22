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
  virtual std::vector<std::byte> receive(std::span<const std::byte> message)
  {
    (void)message;
    return {};
  }

private:
  GuestServiceQueue m_services;
};

std::unique_ptr<GuestApplication>
CreateGuestApplication();
