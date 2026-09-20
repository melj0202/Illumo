#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <IllumoGuest/Application.h>
#include <IllumoGuest/Diagnostics.h>
#include <IllumoGuest/FontProvider.h>
#include <IllumoGuest/SnapshotWindow.h>

class PresentationGame final : public GuestApplication
{
public:
  PresentationGame()
    : diagnostics(services())
    , backend(services())
    , renderer(&window, nullptr, nullptr, &backend, false)
    , fonts(services(), backend)
  {
    backend.setRenderer(renderer);
  }
  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render) |
               static_cast<std::uint32_t>(GuestCapability::Assets),
             "illumo.presentation",
             {} };
  }
  bool start(std::span<const std::byte> startup) override
  {
    if (!startup.empty()) {
      return false;
    }
    renderer.ensureBuiltinStyles();
    visual.setRenderer(&renderer);
    visual.setWindow(&window);
    visual.setSpace(PrimitiveSpace::Pixels);
    visual.setPixelClipRect({ 40, 40, 560, 400 });
    visual.addFilledRect(0, 0, 640, 480, { 24, 30, 44, 255 });
    visual.addText("CSim guest UI", 64, 72, 32, { 225, 235, 249, 255 });
    const std::array<unsigned char, 16> pixels{ 255, 100, 60, 255, 60,  180,
                                                240, 255, 60, 220, 110, 255,
                                                245, 210, 70, 255 };
    texture = renderer.enrollTexture(pixels.data(), 2, 2, 4, {});
    visual.addSprite(texture, 64, 140, 128, 128);
    visual.addFilledRect(180, 140, 32, 128, { 225, 235, 249, 255 });
    Font::getDefaultFont();
    title =
      Font::loadFromFile("default", 128); // concurrent font request queues
    return texture.isValid() && title != nullptr;
  }
  void update(const GuestInput& input) override
  {
    window.accept(input);
    fonts.pump();
    ++tick;
  }
  GuestFrame frame() override
  {
    renderer.BeginFrame();
    const std::array<int, 2> dimensions = window.getWindowDimensions();
    backend.setFrame(dimensions[0], dimensions[1]);
    backend.pump();
    backend.setLayer(GuestLayer::Ui);
    const std::array<unsigned char, 4> pixel{
      static_cast<unsigned char>(tick % 255), 180, 240, 255
    };
    renderer.pushUpdateTexture(texture, 0, 0, 1, 1, 4, pixel.data());
    visual.AppendCommands(&renderer);
    renderer.EndFrame();
    if (!renderer.frameError().empty()) {
      throw std::runtime_error(renderer.frameError());
    }
    return backend.takeFrame();
  }
  bool close() override
  {
    return title && title->isValid() && Font::getDefaultFont()->isValid();
  }
  void shutdown() override { renderer.destroyTexture(texture); }

private:
  GuestDiagnostics diagnostics;
  GuestRecordingBackend backend;
  GuestSnapshotWindow window;
  Renderer renderer;
  GuestFontProvider fonts;
  GameVisual visual;
  TextureHandle texture;
  std::shared_ptr<Font> title;
  std::uint32_t tick = 0;
};
std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<PresentationGame>();
}
