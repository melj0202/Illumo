#pragma once

#include "Game/CSimPlatform.h"
#include <IllumoGuest/Clipboard.h>
#include <IllumoGuest/Dialog.h>
#include <IllumoGuest/Files.h>
#include <deque>

// CSimPlatform over guest services. Locations are storage-relative names, or
// "selected:<capability>" for files granted by a native dialog. Completions
// run from pump() on a later update, never inside the requesting call.
class GuestCSimPlatform final : public CSimPlatform
{
public:
  GuestCSimPlatform(GuestServiceQueue& services, GuestFiles& files);
  ~GuestCSimPlatform() override;
  GuestCSimPlatform(const GuestCSimPlatform&) = delete;
  GuestCSimPlatform& operator=(const GuestCSimPlatform&) = delete;
  GuestCSimPlatform(GuestCSimPlatform&&) = delete;
  GuestCSimPlatform& operator=(GuestCSimPlatform&&) = delete;

  // Makes this instance CSimPlatform::current() for the store's lifetime.
  void install();
  void pump();

  void chooseLoadLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override;
  void chooseSaveLocation(const SaveLoadDialogSpec& specification,
                          LocationCallback done) override;
  void readFirst(std::vector<std::string> locations,
                 ReadCallback done) override;
  void writeFile(const std::string& location,
                 std::string bytes,
                 WriteCallback done) override;
  void readClipboard(TextCallback done) override;
  void writeClipboard(const std::string& text) override;
  void saveUserCatalog(std::vector<RuleFamilyDefinition> families,
                       std::vector<RuleSetDefinition> rules,
                       WriteCallback done) override;

private:
  struct DialogRequest
  {
    bool save = false;
    SaveLoadDialogSpec specification;
    LocationCallback done;
  };
  struct ReadRequest
  {
    std::vector<std::string> locations;
    std::size_t next = 0;
    std::uint64_t task = 0;
    ReadCallback done;
  };
  struct WriteRequest
  {
    // Written in order; the callback reports the first failure.
    std::vector<std::pair<std::string, std::string>> files;
    std::size_t next = 0;
    std::uint64_t task = 0;
    WriteCallback done;
  };
  bool startRead(ReadRequest& request);
  bool startWrite(WriteRequest& request);
  std::uint64_t submitRead(const std::string& location);
  std::uint64_t submitWrite(const std::string& location, std::string bytes);

  GuestFiles& m_files;
  GuestDialog m_dialog;
  GuestClipboard m_clipboard;
  std::deque<DialogRequest> m_dialogs;
  bool m_dialogActive = false;
  std::deque<ReadRequest> m_reads;
  std::deque<WriteRequest> m_writes;
  std::deque<TextCallback> m_clipboardReads;
  bool m_clipboardActive = false;
};
