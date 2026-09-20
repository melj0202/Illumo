#include <IllumoGuest/Application.h>
#include <IllumoGuest/Display.h>
#include <IllumoGuest/Environment.h>
#include <IllumoGuest/Files.h>

class FileControl final : public GuestApplication
{
public:
  FileControl()
    : files(services())
    , environment(files)
    , invalidEnvironment(files, "broken.json")
    , display(services())
  {
  }
  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render) |
               static_cast<std::uint32_t>(GuestCapability::Assets) |
               static_cast<std::uint32_t>(GuestCapability::Storage) |
               static_cast<std::uint32_t>(GuestCapability::Display),
             "illumo.file-test",
             {} };
  }
  bool start(std::span<const std::byte>) override
  {
    task = files.read(GuestFileArea::Package, "asset.txt");
    return task != 0;
  }
  void update(const GuestInput&) override
  {
    ++ticks;
    files.pump();
    environment.pump();
    invalidEnvironment.pump();
    if (phase >= 4) {
      updateSettings();
      return;
    }
    if (phase == 3) {
      const std::size_t end = std::min(verified + 16384u, received.size());
      for (; verified < end; ++verified) {
        if (received[verified] != static_cast<std::byte>(verified % 251)) {
          throw std::runtime_error("Guest streaming bytes mismatch");
        }
      }
      if (verified == received.size()) {
        received.clear();
        task = files.read(GuestFileArea::Package, "asset.txt");
        files.pump();
        files.cancel(task);
        phase = 4;
      }
      return;
    }
    GuestFileResult result;
    if (!files.take(task, result)) {
      return;
    }
    if (result.outcome != GuestFileOutcome::Success) {
      throw std::runtime_error("Guest file transfer failed");
    }
    if (phase == 0) {
      if (result.bytes != std::vector<std::byte>{ std::byte{ 'a' },
                                                  std::byte{ 'b' },
                                                  std::byte{ 'c' } }) {
        throw std::runtime_error("Guest asset data mismatch");
      }
      std::vector<std::byte> bytes(GuestFileRequest::MaximumBlock * 9 + 7);
      for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::byte>(index % 251);
      }
      task = files.write("roundtrip.csim", std::move(bytes));
    } else if (phase == 1) {
      task = files.read(GuestFileArea::Storage, "roundtrip.csim");
    } else if (phase == 2) {
      if (result.bytes.size() != GuestFileRequest::MaximumBlock * 9 + 7) {
        throw std::runtime_error("Guest streaming length mismatch");
      }
      received = std::move(result.bytes);
    }
    ++phase;
  }
  GuestFrame frame() override
  {
    GuestFrame frame;
    GuestBatch batch;
    batch.vertices = { { { 0, 0, 0 } }, { { 1, 0, 0 } }, { { 0, 1, 0 } } };
    batch.indices = { 0, 1, 2 };
    frame.batches.push_back(std::move(batch));
    return frame;
  }
  bool close() override { return phase == 12 && ticks > 5 && files.idle(); }
  void shutdown() override { files.cancel(task); }

private:
  void updateSettings()
  {
    if (phase == 4 && files.idle()) {
      environment.setVar("retained-default", "present");
      environment.load();
      ++phase;
    } else if (phase == 5 && environment.loaded()) {
      if (!environment.writable() || !environment.error().empty()) {
        throw std::runtime_error("Missing settings must allow first save");
      }
      environment.setVar("persisted", "first");
      environment.save();
      environment.pump();
      environment.setVar("persisted", "latest");
      environment.save();
      ++phase;
    } else if (phase == 6 && environment.idle()) {
      if (!environment.error().empty()) {
        throw std::runtime_error("Settings save failed");
      }
      environment.setVar("persisted", "changed in memory");
      environment.load();
      ++phase;
    } else if (phase == 7 && environment.loaded()) {
      if (!environment.writable() ||
          environment.getVar("persisted").value != "latest" ||
          environment.getVar("retained-default").value != "present") {
        throw std::runtime_error("Coalesced settings were not persisted");
      }
      task = files.write("broken.json", { std::byte{ '{' } });
      ++phase;
    } else if (phase == 8) {
      GuestFileResult result;
      if (files.take(task, result)) {
        if (result.outcome != GuestFileOutcome::Success) {
          throw std::runtime_error("Malformed settings setup failed");
        }
        invalidEnvironment.load();
        ++phase;
      }
    } else if (phase == 9 && invalidEnvironment.loaded()) {
      if (invalidEnvironment.writable() || invalidEnvironment.error().empty()) {
        throw std::runtime_error("Malformed settings authorized replacement");
      }
      invalidEnvironment.setVar("must-not-write", true);
      invalidEnvironment.save();
      invalidEnvironment.pump();
      if (!invalidEnvironment.idle()) {
        throw std::runtime_error("Malformed settings must remain untouched");
      }
      ++phase;
    } else if (phase == 10) {
      display.apply({ false, false, 120, 2 });
      display.pump();
      ++phase;
    } else if (phase == 11) {
      display.pump();
      if (display.idle()) {
        if (!display.ready() || !display.error().empty() ||
            display.actual() != GuestDisplayState{ false, false, 120, 2 }) {
          throw std::runtime_error("Display settings did not round-trip");
        }
        ++phase;
      }
    }
  }
  GuestFiles files;
  GuestEnvironment environment;
  GuestEnvironment invalidEnvironment;
  GuestDisplay display;
  std::uint64_t task = 0;
  std::vector<std::byte> received;
  std::size_t verified = 0;
  unsigned int phase = 0, ticks = 0;
};
std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<FileControl>();
}
