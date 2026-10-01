#include <IllumoGuest/Application.h>

class JobControl final : public GuestApplication
{
public:
  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render) |
               static_cast<std::uint32_t>(GuestCapability::Jobs),
             "illumo.job-probe",
             {} };
  }
  bool start(std::span<const std::byte> startup) override
  {
    GuestWireWriter job;
    job.u32(0);
    job.text("isolated compute");
    pending = services().enqueue(GuestService::Job, job.take());
    return startup.empty() && pending != 0;
  }
  void update(const GuestInput&) override
  {
    ++frames;
    GuestServiceRecord result;
    if (pending == 0 || !services().take(pending, result)) {
      return;
    }
    if (stage == 0) {
      GuestWireReader output(result.payload);
      if (result.status != GuestServiceStatus::Complete || output.u32() != 1 ||
          output.text() != "isolated compute" || !output.finished()) {
        __builtin_trap();
      }
      stage = 1;
      GuestWireWriter failing;
      failing.u32(1);
      pending = services().enqueue(GuestService::Job, failing.take());
      if (pending == 0) {
        __builtin_trap();
      }
    } else {
      if (result.status != GuestServiceStatus::Rejected ||
          !result.payload.empty()) {
        __builtin_trap();
      }
      stage = 2;
      pending = 0;
    }
  }
  GuestFrame frame() override
  {
    GuestFrame result;
    GuestBatch batch;
    batch.vertices = { { { -0.5f, -0.5f, 0 }, 0xffffffff, {} },
                       { { 0.5f, -0.5f, 0 }, 0xff88dd66, {} },
                       { { 0, 0.5f, 0 }, 0xffaaddff, {} } };
    batch.indices = { 0, 1, 2 };
    result.batches.push_back(std::move(batch));
    return result;
  }
  bool close() override { return stage == 2 && frames > 1; }
  void shutdown() override {}

private:
  std::uint64_t pending = 0;
  std::uint32_t frames = 0;
  std::uint32_t stage = 0;
};
std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<JobControl>();
}
