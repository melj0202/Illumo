#pragma once

#include "Game/CSimPlatform.h"
#include <IllumoGuest/Clipboard.h>
#include <IllumoGuest/Dialog.h>
#include <IllumoGuest/Files.h>
#include <deque>

// Simulation lanes over host job services: one LaneJob per lane in flight,
// and the granted lane count from one JobLanes query (none without Jobs).
class GuestSimulationLanes final : public SimulationLaneTransport
{
public:
  explicit GuestSimulationLanes(GuestServiceQueue& services);
  ~GuestSimulationLanes() override = default;
  GuestSimulationLanes(const GuestSimulationLanes&) = delete;
  GuestSimulationLanes& operator=(const GuestSimulationLanes&) = delete;
  GuestSimulationLanes(GuestSimulationLanes&&) = delete;
  GuestSimulationLanes& operator=(GuestSimulationLanes&&) = delete;

  void pump();
  std::uint32_t laneCount() override;
  bool laneCountKnown() const override;
  bool submit(std::uint32_t lane, std::vector<std::byte>&& request) override;
  int poll(std::uint32_t lane, std::vector<std::byte>& reply) override;
  bool busy(std::uint32_t lane) const override;

private:
  enum class Grant
  {
    Unknown,
    Requested,
    Known
  };
  GuestServiceQueue& m_services;
  Grant m_grant = Grant::Unknown;
  std::uint64_t m_query = 0;
  std::uint32_t m_lanes = 0;
  std::vector<std::uint64_t> m_outstanding;
  std::vector<bool> m_failed;
};

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
  SimulationLaneTransport* simulationLanes() override { return &m_lanes; }

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
  GuestSimulationLanes m_lanes;
  GuestDialog m_dialog;
  GuestClipboard m_clipboard;
  std::deque<DialogRequest> m_dialogs;
  bool m_dialogActive = false;
  std::deque<ReadRequest> m_reads;
  std::deque<WriteRequest> m_writes;
  std::deque<TextCallback> m_clipboardReads;
  bool m_clipboardActive = false;
};
