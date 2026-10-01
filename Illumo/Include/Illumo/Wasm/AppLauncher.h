#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Starts installed applications for a guest granted Launch (an editor's
// Play). The host decides which applications exist and how they run; the
// guest only names one and hands over its launch document. One launched
// application at a time: launch() replaces the previous one.
class IAppLauncher
{
public:
  virtual ~IAppLauncher() = default;
  // Whether this host can launch applications at all.
  virtual bool available() const = 0;
  // Starts `application` with `document` saved as `name` and opened as its
  // launch document. False with a reason when the application is unknown or
  // could not start.
  virtual bool launch(const std::string& application,
                      const std::string& name,
                      std::span<const std::byte> document,
                      std::string& error) = 0;
  // Ends the launched application, if any.
  virtual void stop() = 0;
  // Whether the launched application is still running.
  virtual bool running() = 0;
};
