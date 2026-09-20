#include <IllumoGuest/Application.h>

class PaletteMod final : public GuestApplication
{
public:
  GuestDescriptor describe() const override
  {
    return { GuestRole::Mod,
             static_cast<std::uint32_t>(GuestCapability::Messages),
             "illumo.paddle.palette",
             "illumo.paddle.palette.v1" };
  }
  bool start(std::span<const std::byte> startup) override
  {
    return startup.empty();
  }
  void update(const GuestInput&) override {}
  GuestFrame frame() override { return {}; }
  bool close() override { return true; }
  void shutdown() override {}
  std::vector<std::byte> receive(std::span<const std::byte> message) override
  {
#ifdef ILLUMO_FAULTY_MOD
    (void)message;
    __builtin_trap();
#else
    GuestWireReader input(message);
    const std::uint32_t magic = input.u32();
    const std::uint32_t score = input.u32();
    if (magic != 0x31444150 || score > 30 || !input.finished()) {
      return {};
    }
    GuestWireWriter response;
    response.u32(magic);
    response.u32(score % 2 == 0 ? 0xffff77bb : 0xff66ddff);
    return response.take();
#endif
  }
};

std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<PaletteMod>();
}
