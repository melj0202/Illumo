#pragma once
#include <Illumo/Services/EnvValues.h>
#include <IllumoGuest/Files.h>

// Application-owned settings. load/save enqueue work; pump reconciles actual
// completion. The application waits for loaded() before starting its modules
// and for idle() before orderly shutdown. Failed loads never authorize writes.
class GuestEnvironment final : public EnvValues
{
public:
  explicit GuestEnvironment(GuestFiles& files,
                            std::string path = "envvars.json");
  ~GuestEnvironment() override;
  GuestEnvironment(const GuestEnvironment&) = delete;
  GuestEnvironment& operator=(const GuestEnvironment&) = delete;
  GuestEnvironment(GuestEnvironment&&) = delete;
  GuestEnvironment& operator=(GuestEnvironment&&) = delete;
  void load() override;
  void save() override;
  void pump();
  bool loaded() const { return m_loaded; }
  bool writable() const { return m_writable; }
  bool idle() const { return m_request == 0 && !m_savePending && !m_reading; }
  const std::string& error() const { return m_error; }

private:
  GuestFiles& m_files;
  std::string m_path;
  std::string m_error;
  std::uint64_t m_request = 0;
  bool m_reading = false;
  bool m_loaded = false;
  bool m_writable = false;
  bool m_savePending = false;
};
