#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <IllumoGuest/Application.h>
#include <IllumoGuest/Clipboard.h>
#include <IllumoGuest/Console.h>
#include <IllumoGuest/Dialog.h>
#include <IllumoGuest/FontProvider.h>
#include <IllumoGuest/InputProvider.h>
#include <IllumoGuest/SnapshotWindow.h>

static void
require(bool condition, const char* message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

static GuestServices
exchange(GuestServiceQueue& queue, const GuestServices& completions = {})
{
  GuestWireWriter input;
  completions.write(input);
  std::vector<std::byte> output;
  require(queue.exchange(input.data(), output), "SDK completion delivery");
  GuestServices requests;
  require(GuestServices::read(output, requests, true), "SDK request encoding");
  return requests;
}

static void
inputContract()
{
  InputManager manager(nullptr);
  InputContext context;
  context.bindAction("menu", { KeyCode::F1, InputAction::Press });
  const long id = manager.registerInputContext(context);
  require(manager.setActiveInputContext(id), "Input context registration");
  GuestInput input;
  input.keys[static_cast<std::size_t>(GuestKey::F1)] = GuestKeyAction::Press;
  input.keys[static_cast<std::size_t>(GuestKey::MouseLeft)] =
    GuestKeyAction::Hold;
  input.modifiers = 3;
  input.mouseX = 120.5;
  input.mouseY = 40.25;
  input.scroll = -2;
  input.events = { { GuestKey::F1, GuestKeyAction::Press, 3 } };
  input.characters = { 'a', 0x1f642 };
  GuestInputProvider::accept(manager, input);
  require(manager.isActionActive("menu") &&
            manager.isMouseButtonPressed(KeyCode::MouseLeft) &&
            manager.isControlPressed() && manager.isShiftPressed() &&
            manager.getMousePosition()[0] == 120.5 &&
            *manager.getMouseScrollOffset() == -2 &&
            manager.getCharQueue().size() == 2 &&
            manager.getKeyQueue().front().action == InputAction::Press,
          "Input translation preserves actions, modifiers, pointer and queues");
  input.keys[static_cast<std::size_t>(GuestKey::F1)] = GuestKeyAction::Hold;
  input.events.clear();
  input.characters.clear();
  GuestInputProvider::accept(manager, input);
  require(!manager.isActionActive("menu") &&
            manager.isKeyPressed(KeyCode::F1) && manager.getKeyQueue().empty(),
          "Held key does not repeat an edge action");
  manager.suppressKeyForFrame(KeyCode::F1);
  require(!manager.isKeyPressed(KeyCode::F1), "Overlay suppression");
  require(manager.unregisterInputContext(id) && !manager.isActionActive("menu"),
          "Retired context is neutral");
}

static void
recordingContract()
{
  GuestServiceQueue queue;
  GuestRecordingBackend backend(queue, 2048);
  GuestSnapshotWindow window;
  Renderer renderer(&window, nullptr, nullptr, &backend, false);
  backend.setRenderer(renderer);
  renderer.ensureBuiltinStyles();
  GuestFontProvider fonts(queue, backend);
  GameVisual visual;
  visual.setWindow(&window);
  visual.setRenderer(&renderer);
  visual.addText("Retry", 0, 0, 32, { 255, 255, 255, 255 });
  for (std::size_t index = 0; index < GuestServices::MaximumRecords; ++index) {
    require(queue.enqueue(GuestService::Log, {}) != 0, "Fill request queue");
  }
  renderer.BeginFrame();
  backend.setFrame(1280, 720);
  visual.AppendCommands(&renderer);
  renderer.EndFrame();
  require(backend.takeFrame().batches.empty(), "Pending text has no geometry");
  GuestServices pending = exchange(queue);
  for (GuestServiceRecord& record : pending.records) {
    record.status = GuestServiceStatus::Complete;
  }
  exchange(queue, pending);
  renderer.BeginFrame();
  backend.setFrame(1280, 720);
  visual.AppendCommands(&renderer);
  renderer.EndFrame();
  backend.takeFrame();
  pending = exchange(queue);
  require(pending.records.size() == 1 &&
            pending.records[0].operation == GuestService::LoadFont,
          "Unchanged visual retries font acquisition after queue pressure");
  Font::clearCache();
  GuestFont font;
  font.atlas = { 1, GuestResourceKind::Texture, 1, 1 };
  font.metrics = { 32, 24, -8, 32, 16 };
  GuestWireWriter data;
  font.write(data);
  pending.records[0].payload = data.take();
  pending.records[0].status = GuestServiceStatus::Complete;
  exchange(queue, pending);
  fonts.pump();
  backend.pump();
  pending = exchange(queue);
  require(pending.records.size() == 1 &&
            pending.records[0].operation == GuestService::ReleaseTexture,
          "Clearing font cache drains pending atlas acquisition");
  pending.records[0].payload.clear();
  pending.records[0].status = GuestServiceStatus::Complete;
  exchange(queue, pending);
  backend.pump();

  std::vector<TextureHandle> textures;
  for (std::uint32_t index = 1; index <= 64; ++index) {
    textures.push_back(
      backend.importTexture({ 1, GuestResourceKind::Texture, index, 1 }));
  }
  for (TextureHandle handle : textures) {
    require(backend.DestroyTexture(handle), "Texture retirement");
  }
  for (int batch = 0; batch < 2; ++batch) {
    backend.pump();
    pending = exchange(queue);
    require(pending.records.size() == 32,
            "Retirements obey exchange backpressure");
    for (GuestServiceRecord& record : pending.records) {
      record.status = GuestServiceStatus::Complete;
    }
    exchange(queue, pending);
  }
  backend.pump();
  require(exchange(queue).records.empty(), "All retirements drained");
  renderer.BeginFrame();
  backend.setFrame(1280, 720);
  for (std::size_t index = 0; index < 2050; ++index) {
    renderer.pushSetMesh({});
  }
  renderer.EndFrame();
  bool rejected = false;
  try {
    backend.takeFrame();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "Overflow cannot publish a command prefix");
  backend.Shutdown();
}

static void
displayContract()
{
  GuestServiceQueue queue;
  GuestDisplay display(queue);
  display.apply({ true, false, 120, 2 });
  display.pump();
  GuestServices first = exchange(queue);
  require(first.records.size() == 1, "Display request published");
  display.apply({ false, true, 60, 1 });
  display.pump();
  require(exchange(queue).records.empty(), "Pending display work coalesces");
  GuestWireWriter state;
  GuestDisplayState{ true, false, 120, 2 }.write(state);
  first.records[0].status = GuestServiceStatus::Complete;
  first.records[0].payload = state.take();
  exchange(queue, first);
  display.pump();
  GuestServices next = exchange(queue);
  GuestDisplayRequest request;
  require(next.records.size() == 1 &&
            GuestDisplayRequest::read(next.records[0].payload, request) &&
            request.apply && !request.state.fullscreen && request.state.vsync,
          "Later display request carries absolute latest state");
  require(!display.idle(), "First completion does not complete later intent");
  next.records[0].status = GuestServiceStatus::Rejected;
  next.records[0].payload.clear();
  exchange(queue, next);
  display.pump();
  require(display.idle() && !display.error().empty(),
          "Display denial is observable");
}

static void
clipboardContract()
{
  GuestServiceQueue queue;
  GuestClipboard clipboard(queue);
  clipboard.set("first");
  clipboard.pump();
  GuestServices pending = exchange(queue);
  require(pending.records.size() == 1, "Clipboard request published");
  clipboard.set("latest");
  clipboard.pump();
  require(exchange(queue).records.empty(), "Pending clipboard work coalesces");
  GuestWireWriter text;
  GuestClipboardRequest{ false, "latest" }.write(text);
  pending.records[0].status = GuestServiceStatus::Complete;
  pending.records[0].payload = text.take();
  exchange(queue, pending);
  clipboard.pump();
  GuestServices next = exchange(queue);
  GuestClipboardRequest request;
  require(next.records.size() == 1 &&
            GuestClipboardRequest::read(next.records[0].payload, request) &&
            request.set && request.text == "latest",
          "Later clipboard request carries absolute latest text");
  next.records[0].status = GuestServiceStatus::Complete;
  GuestWireWriter echoed;
  GuestClipboardRequest{ false, "latest" }.write(echoed);
  next.records[0].payload = echoed.take();
  exchange(queue, next);
  clipboard.pump();
  require(clipboard.idle() && clipboard.text() == "latest",
          "Clipboard completion stores host text");
}

static void
consoleContract()
{
  GuestServiceQueue queue;
  GuestConsole console(queue);
  console.add("ruleset", "ruleset [name]", "Show or change the ruleset");
  console.pump();
  GuestServices pending = exchange(queue);
  require(pending.records.size() == 2, "Register and listen are published");
  pending.records[0].status = GuestServiceStatus::Complete;
  pending.records[1].status = GuestServiceStatus::Complete;
  GuestConsoleRequest invocation;
  invocation.action = GuestConsoleAction::Listen;
  invocation.name = "ruleset";
  invocation.arguments = { "WIREWORLD" };
  GuestWireWriter payload;
  invocation.write(payload);
  pending.records[1].payload = payload.take();
  exchange(queue, pending);
  console.pump();
  GuestConsoleRequest received;
  require(console.take(received) && received.name == "ruleset" &&
            received.arguments.front() == "WIREWORLD",
          "Listen completion becomes a guest invocation");
}

static void
dialogContract()
{
  GuestServiceQueue queue;
  GuestDialog dialog(queue);
  dialog.load("CSim Simulation", "MyCanvas.illumo", "*.ILLUMO");
  dialog.pump();
  GuestServices pending = exchange(queue);
  require(pending.records.size() == 1, "Dialog request published");
  dialog.save("CSim Simulation", "MyCanvas.illumo", "*.ILLUMO");
  dialog.pump();
  require(exchange(queue).records.empty(), "Pending dialog work coalesces");
  GuestDialogRequest request;
  require(GuestDialogRequest::read(pending.records[0].payload, request) &&
            !request.save,
          "First dialog request remains the in-flight load");
  pending.records[0].status = GuestServiceStatus::Complete;
  GuestWireWriter granted;
  GuestDialogResult{ GuestFileOutcome::Success, false, 4, "sel-1" }.write(
    granted);
  pending.records[0].payload = granted.take();
  exchange(queue, pending);
  dialog.pump();
  GuestServices next = exchange(queue);
  require(next.records.size() == 1 &&
            GuestDialogRequest::read(next.records[0].payload, request) &&
            request.save,
          "Later dialog request carries the latest save intent");
}

class SdkContract final : public GuestApplication
{
public:
  GuestDescriptor describe() const override
  {
    return { GuestRole::Game,
             static_cast<std::uint32_t>(GuestCapability::Render),
             "illumo.sdk-test",
             {} };
  }
  bool start(std::span<const std::byte>) override
  {
    inputContract();
    displayContract();
    clipboardContract();
    consoleContract();
    dialogContract();
    recordingContract();
    return true;
  }
  void update(const GuestInput&) override {}
  bool close() override { return true; }
  void shutdown() override {}
  GuestFrame frame() override
  {
    GuestFrame frame;
    GuestBatch batch;
    batch.vertices = { { { 0, 0, 0 } }, { { 1, 0, 0 } }, { { 0, 1, 0 } } };
    batch.indices = { 0, 1, 2 };
    frame.batches.push_back(std::move(batch));
    return frame;
  }
};
std::unique_ptr<GuestApplication>
CreateGuestApplication()
{
  return std::make_unique<SdkContract>();
}
