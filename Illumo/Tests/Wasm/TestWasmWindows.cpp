// Surface windows (Windows capability): the Window service, frame schema v5
// surfaces, input v2 and the host's WasmPanelWindows, headless through a
// fake window platform and MockBackend readbacks.

#include <Illumo/Platform/SurfaceWindow.h>
#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Renderer.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/EnvVars.h>
#include <Illumo/Testing/MockBackend.h>
#include <Illumo/Testing/TestAccess.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Wasm/WasmFrameRenderer.h>
#include <Illumo/Wasm/WasmGameServices.h>
#include <Illumo/Wasm/WasmPanelWindows.h>
#include <IllumoGuest/Frame.h>
#include <IllumoGuest/Input.h>
#include <IllumoGuest/PanelSurfaces.h>
#include <IllumoGuest/Protocol.h>
#include <IllumoGuest/RecordingBackend.h>
#include <IllumoGuest/SnapshotWindow.h>
#include <IllumoGuest/Windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

// A window the test drives by hand: size, origin, focus, close, events.
class FakeSurfaceWindow final : public ISurfaceWindow
{
public:
  std::string title;
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool close = false;
  bool hasFocus = false;
  bool repaint = false;
  double cursorX = 0.0;
  double cursorY = 0.0;
  std::vector<SurfaceWindowEvent> queued;
  int presents = 0;
  std::vector<std::uint8_t> lastImage;
  int lastWidth = 0;
  int lastHeight = 0;
  bool* destroyed = nullptr;

  ~FakeSurfaceWindow() override
  {
    if (destroyed != nullptr) {
      *destroyed = true;
    }
  }
  bool closeRequested() const override { return close; }
  void clientSize(int* w, int* h) const override
  {
    *w = width;
    *h = height;
  }
  void clientOrigin(int* left, int* top) const override
  {
    *left = x;
    *top = y;
  }
  bool takeRepaintRequest() override
  {
    const bool requested = repaint;
    repaint = false;
    return requested;
  }
  std::vector<SurfaceWindowEvent> takeEvents() override
  {
    std::vector<SurfaceWindowEvent> events;
    events.swap(queued);
    return events;
  }
  bool present(const std::vector<std::uint8_t>& rgba, int w, int h) override
  {
    ++presents;
    lastImage = rgba;
    lastWidth = w;
    lastHeight = h;
    return true;
  }
  void setTitle(const std::string& value) override { title = value; }
  void focus() override { hasFocus = true; }
  bool focused() const override { return hasFocus; }
  void cursor(double* left, double* top) const override
  {
    *left = cursorX;
    *top = cursorY;
  }
};

class FakeSurfaceWindows final : public ISurfaceWindowFactory
{
public:
  bool supported = true;
  int mainX = 100;
  int mainY = 50;
  std::vector<FakeSurfaceWindow*> created;
  std::array<bool, 16> destroyed{};
  int minimumWidth = 0;
  int minimumHeight = 0;

  bool available() const override { return supported; }
  std::unique_ptr<ISurfaceWindow> create(const std::string& title,
                                         int x,
                                         int y,
                                         int width,
                                         int height,
                                         int minWidth,
                                         int minHeight,
                                         std::string* error) override
  {
    if (!supported || created.size() >= destroyed.size()) {
      *error = "unsupported";
      return nullptr;
    }
    std::unique_ptr<FakeSurfaceWindow> window =
      std::make_unique<FakeSurfaceWindow>();
    window->title = title;
    window->x = x;
    window->y = y;
    window->width = width;
    window->height = height;
    window->destroyed = &destroyed[created.size()];
    minimumWidth = minWidth;
    minimumHeight = minHeight;
    created.push_back(window.get());
    return window;
  }
  bool mainOrigin(IRenderWindow&, int* x, int* y) const override
  {
    *x = mainX;
    *y = mainY;
    return true;
  }
  bool mainFocused(IRenderWindow&) const override { return true; }
};

static GuestWindowRequest
openRequest(std::uint32_t surface, std::int32_t x = 20, std::int32_t y = 30)
{
  GuestWindowRequest request;
  request.action = GuestWindowAction::Open;
  request.surface = surface;
  request.title = "Inspector";
  request.x = x;
  request.y = y;
  request.width = 300;
  request.height = 400;
  return request;
}

static bool
decodes(const GuestWindowRequest& request)
{
  GuestWireWriter bytes;
  request.write(bytes);
  GuestWindowRequest decoded;
  return GuestWindowRequest::read(bytes.data(), decoded);
}

static GuestBatch
surfaceQuad()
{
  GuestBatch batch;
  batch.layer = GuestLayer::Ui;
  batch.vertices = { { { 0, 0, 0 }, 0xff0000ffu, { 0, 0 } },
                     { { 50, 0, 0 }, 0xff00ff00u, { 1, 0 } },
                     { { 0, 50, 0 }, 0xffff0000u, { 0, 1 } } };
  batch.indices = { 0, 1, 2 };
  batch.clipped = true;
  batch.clip = { 0, 0, 150, 200 };
  return batch;
}

static GuestFrame
frameWithSurface(std::uint32_t surface, std::uint64_t revision, bool same)
{
  GuestFrame frame;
  frame.width = 640;
  frame.height = 480;
  GuestSurfaceFrame content;
  content.surface = surface;
  content.width = 300;
  content.height = 400;
  content.revision = revision;
  content.same = same;
  if (!same) {
    content.batches.push_back(surfaceQuad());
  }
  frame.surfaces.push_back(std::move(content));
  return frame;
}

static bool
readsFrame(const GuestFrame& frame, GuestFrame* decoded = nullptr)
{
  GuestWireWriter bytes;
  frame.write(bytes);
  GuestFrame ignored;
  return GuestFrame::read(bytes.data(),
                          decoded != nullptr ? *decoded : ignored);
}

static int
testWindowServiceDecoder()
{
  TestCounters counters;
  GuestWireWriter bytes;
  openRequest(7).write(bytes);
  GuestWindowRequest decoded;
  testTrue(counters,
           GuestWindowRequest::read(bytes.data(), decoded) &&
             decoded.action == GuestWindowAction::Open &&
             decoded.surface == 7 && decoded.title == "Inspector" &&
             decoded.x == 20 && decoded.y == 30 && decoded.width == 300 &&
             decoded.height == 400,
           "an open request round-trips");
  GuestWindowRequest zero = openRequest(0);
  GuestWindowRequest tooLarge = openRequest(1);
  tooLarge.width = GuestWindowRequest::MaximumSize + 1;
  GuestWindowRequest tooSmall = openRequest(1);
  tooSmall.height = GuestWindowRequest::MinimumHeight - 1;
  GuestWindowRequest farAway = openRequest(1, 20000, 0);
  GuestWindowRequest control = openRequest(1);
  control.title = "bad\ntitle";
  GuestWindowRequest longTitle = openRequest(1);
  longTitle.title = std::string(GuestWindowRequest::MaximumTitleBytes + 1, 'a');
  GuestWindowRequest invalidUtf8 = openRequest(1);
  invalidUtf8.title = "\xc3";
  GuestWindowRequest closeGeometry = openRequest(1);
  closeGeometry.action = GuestWindowAction::Close;
  closeGeometry.title.clear();
  GuestWindowRequest closeTitle;
  closeTitle.action = GuestWindowAction::Close;
  closeTitle.surface = 1;
  closeTitle.title = "x";
  GuestWindowRequest retitleGeometry = openRequest(1);
  retitleGeometry.action = GuestWindowAction::SetTitle;
  testTrue(counters,
           !decodes(zero) && !decodes(tooLarge) && !decodes(tooSmall) &&
             !decodes(farAway) && !decodes(control) && !decodes(longTitle) &&
             !decodes(invalidUtf8) && !decodes(closeGeometry) &&
             !decodes(closeTitle) && !decodes(retitleGeometry),
           "surface 0, sizes, offsets, titles and stray geometry are refused");
  GuestWindowRequest close;
  close.action = GuestWindowAction::Close;
  close.surface = 3;
  GuestWindowRequest retitle;
  retitle.action = GuestWindowAction::SetTitle;
  retitle.surface = 3;
  retitle.title = "Hierarchy";
  testTrue(counters,
           decodes(close) && decodes(retitle),
           "close and retitle requests decode");
  std::vector<std::byte> wrongVersion = bytes.data();
  wrongVersion[0] = std::byte{ 2 };
  std::vector<std::byte> trailing = bytes.data();
  trailing.push_back(std::byte{ 0 });
  std::vector<std::byte> action = bytes.data();
  action[4] = std::byte{ 9 };
  testTrue(counters,
           !GuestWindowRequest::read(wrongVersion, decoded) &&
             !GuestWindowRequest::read(trailing, decoded) &&
             !GuestWindowRequest::read(action, decoded),
           "versions, trailing bytes and unknown actions are refused");
  GuestWireWriter opened;
  GuestWindowOpened{ 300, 400 }.write(opened);
  GuestWindowOpened size;
  GuestWireWriter empty;
  GuestWindowOpened{ 0, 400 }.write(empty);
  testTrue(counters,
           GuestWindowOpened::read(opened.data(), size) && size.width == 300 &&
             !GuestWindowOpened::read(empty.data(), size),
           "open completions carry a bounded size");
  return counters.failures;
}

static GuestInput
inputV2()
{
  GuestInput input;
  input.version = 2;
  input.events.push_back({ GuestKey::A, GuestKeyAction::Press, 0 });
  input.events.push_back({ GuestKey::B, GuestKeyAction::Press, 1 });
  input.eventSurfaces = { 0, 5 };
  input.characters = { 'x' };
  input.characterSurfaces = { 5 };
  input.originX = -1200;
  input.originY = 40;
  input.focusedSurface = 5;
  GuestSurfaceInput surface;
  surface.surface = 5;
  surface.width = 300;
  surface.height = 400;
  surface.originX = 10;
  surface.originY = 20;
  surface.mouseX = -15.5;
  surface.mouseY = 420.0;
  surface.buttons = GuestSurfaceInput::LeftButton;
  surface.scroll = 1.0;
  surface.focused = true;
  input.surfaces.push_back(surface);
  input.windowEvents.push_back({ GuestWindowEventKind::Resized, 5, 300, 400 });
  input.windowEvents.push_back({ GuestWindowEventKind::Closed, 9, 0, 0 });
  return input;
}

static bool
readsInput(const GuestInput& input, GuestInput* decoded = nullptr)
{
  GuestWireWriter bytes;
  input.write(bytes);
  GuestInput ignored;
  return GuestInput::read(bytes.data(),
                          decoded != nullptr ? *decoded : ignored);
}

static int
testInputV2()
{
  TestCounters counters;
  GuestInput decoded;
  testTrue(
    counters,
    readsInput(inputV2(), &decoded) && decoded.version == 2 &&
      decoded.eventSurfaces.size() == 2 && decoded.eventSurfaces[1] == 5 &&
      decoded.characterSurfaces[0] == 5 && decoded.originX == -1200 &&
      decoded.focusedSurface == 5 && decoded.surfaces.size() == 1 &&
      decoded.surfaces[0].mouseX == -15.5 && decoded.surfaces[0].buttons == 1 &&
      decoded.surfaces[0].focused && decoded.windowEvents.size() == 2 &&
      decoded.windowEvents[1].kind == GuestWindowEventKind::Closed,
    "input v2 round-trips surfaces, tags, focus and window events");
  GuestInput version1;
  version1.events.push_back({ GuestKey::A, GuestKeyAction::Press, 0 });
  testTrue(counters,
           readsInput(version1, &decoded) && decoded.version == 1 &&
             decoded.surfaces.empty(),
           "input v1 still decodes");
  GuestInput unknownTag = inputV2();
  unknownTag.eventSurfaces[1] = 6;
  GuestInput unknownFocus = inputV2();
  unknownFocus.focusedSurface = 6;
  GuestInput mainSurface = inputV2();
  mainSurface.surfaces[0].surface = 0;
  GuestInput duplicate = inputV2();
  duplicate.surfaces.push_back(duplicate.surfaces[0]);
  GuestInput buttons = inputV2();
  buttons.surfaces[0].buttons = 8;
  GuestInput kind = inputV2();
  kind.windowEvents[0].kind = static_cast<GuestWindowEventKind>(6);
  GuestInput mainEvent = inputV2();
  mainEvent.windowEvents[0].surface = 0;
  testTrue(counters,
           !readsInput(unknownTag) && !readsInput(unknownFocus) &&
             !readsInput(mainSurface) && !readsInput(duplicate) &&
             !readsInput(buttons) && !readsInput(kind) &&
             !readsInput(mainEvent),
           "unknown windows, the main surface, duplicates, buttons and event "
           "kinds are refused");
  GuestInput mismatched = inputV2();
  mismatched.eventSurfaces.pop_back();
  bool threw = false;
  try {
    GuestWireWriter bytes;
    mismatched.write(bytes);
  } catch (const std::length_error&) {
    threw = true;
  }
  testTrue(counters, threw, "an event without a window tag cannot be sent");
  return counters.failures;
}

static int
testFrameV5Surfaces()
{
  TestCounters counters;
  GuestFrame decoded;
  testTrue(counters,
           readsFrame(frameWithSurface(4, 1, false), &decoded) &&
             decoded.surfaces.size() == 1 && decoded.surfaces[0].surface == 4 &&
             decoded.surfaces[0].revision == 1 &&
             decoded.surfaces[0].batches.size() == 1 &&
             decoded.surfaces[0].width == 300,
           "a surface with batches round-trips");
  testTrue(counters,
           readsFrame(frameWithSurface(4, 2, true), &decoded) &&
             decoded.surfaces[0].same && decoded.surfaces[0].batches.empty(),
           "an unchanged surface travels without batches");
  GuestFrame zero = frameWithSurface(0, 1, false);
  GuestFrame revision = frameWithSurface(4, 0, false);
  GuestFrame duplicate = frameWithSurface(4, 1, false);
  duplicate.surfaces.push_back(duplicate.surfaces[0]);
  GuestFrame many = frameWithSurface(1, 1, true);
  for (std::uint32_t id = 2; id <= GuestFrame::MaximumSurfaces + 1; ++id) {
    many.surfaces.push_back(many.surfaces[0]);
    many.surfaces.back().surface = id;
  }
  GuestFrame world = frameWithSurface(4, 1, false);
  world.surfaces[0].batches[0].layer = GuestLayer::World;
  GuestFrame depth = frameWithSurface(4, 1, false);
  depth.surfaces[0].batches[0].depthTest = true;
  GuestFrame size = frameWithSurface(4, 1, false);
  size.surfaces[0].width = 0;
  testTrue(counters,
           !readsFrame(zero) && !readsFrame(revision) &&
             !readsFrame(duplicate) && !readsFrame(world) &&
             !readsFrame(depth) && !readsFrame(size),
           "surface 0, revision 0, duplicates, world, depth-tested and empty "
           "surfaces are refused");
  bool manyRefused = false;
  try {
    manyRefused = !readsFrame(many);
  } catch (const std::length_error&) {
    manyRefused = true;
  }
  testTrue(counters, manyRefused, "more than eight surfaces are refused");
  GuestFrame shared = frameWithSurface(4, 1, false);
  shared.batches.push_back(surfaceQuad());
  GuestWireWriter sharedBytes;
  shared.write(sharedBytes);
  GuestFrameLimits limits;
  limits.batches = 1;
  testTrue(counters,
           !GuestFrame::read(sharedBytes.data(), decoded, limits),
           "the batch quota spans the main frame and its surfaces");
  return counters.failures;
}

struct HostFixture
{
  NullRenderWindow window{ 640, 480 };
  EnvVars env;
  Camera camera{ glm::vec2(0, 0), 1, &env };
  MockBackend mock;
  Renderer renderer{ &window, &env, &camera, &mock, false };
  FakeSurfaceWindows platform;
  HostFixture()
  {
    mock.Initialize();
    renderer.ensureBuiltinStyles();
  }
};

static GuestServices
windowServices(std::uint64_t request, const GuestWindowRequest& value)
{
  GuestServices services;
  GuestWireWriter payload;
  value.write(payload);
  services.records.push_back({ request,
                               GuestService::Window,
                               GuestServiceStatus::Request,
                               payload.take() });
  return services;
}

static bool
processes(WasmGameServices& services,
          const GuestServices& requests,
          GuestServices& results)
{
  GuestWireWriter bytes;
  requests.write(bytes);
  std::vector<std::byte> completion;
  if (!services.process(bytes.data(), completion)) {
    std::printf("service exchange failed: %s\n", services.error().c_str());
    return false;
  }
  return GuestServices::read(completion, results, false);
}

static int
testWindowDeny()
{
  TestCounters counters;
  HostFixture fixture;
  WasmFrameRenderer frames(fixture.renderer, 71);
  WasmPanelWindows windows(fixture.renderer, fixture.window, fixture.platform);
  WasmGameServices denied(frames, 0, ILLUMO_ENGINE_ASSETS);
  denied.setWindows(&windows);
  GuestServices results;
  testTrue(counters,
           processes(denied, windowServices(1, openRequest(2)), results) &&
             results.records.size() == 1 &&
             results.records[0].status == GuestServiceStatus::Rejected &&
             fixture.platform.created.empty(),
           "without the Windows grant nothing opens");
  WasmGameServices unwired(frames,
                           static_cast<std::uint32_t>(GuestCapability::Windows),
                           ILLUMO_ENGINE_ASSETS);
  testTrue(counters,
           processes(unwired, windowServices(1, openRequest(2)), results) &&
             results.records[0].status == GuestServiceStatus::Rejected,
           "a host without surface windows rejects the request");
  WasmGameServices granted(frames,
                           static_cast<std::uint32_t>(GuestCapability::Windows),
                           ILLUMO_ENGINE_ASSETS);
  granted.setWindows(&windows);
  GuestServices malformed = windowServices(1, openRequest(2));
  malformed.records[0].payload.push_back(std::byte{ 1 });
  GuestWireWriter malformedBytes;
  malformed.write(malformedBytes);
  std::vector<std::byte> completion;
  testTrue(counters,
           !granted.process(malformedBytes.data(), completion) &&
             fixture.platform.created.empty(),
           "a malformed window request fails the exchange");
  fixture.platform.supported = false;
  WasmGameServices unsupported(
    frames,
    static_cast<std::uint32_t>(GuestCapability::Windows),
    ILLUMO_ENGINE_ASSETS);
  unsupported.setWindows(&windows);
  testTrue(counters,
           processes(unsupported, windowServices(2, openRequest(2)), results) &&
             results.records[0].status == GuestServiceStatus::Rejected,
           "an unsupported platform refuses to open");
  return counters.failures;
}

static const GuestSurfaceInput*
surfaceInput(const GuestInput& input, std::uint32_t id)
{
  return input.surface(id);
}

static int
testPanelWindowsLifecycle()
{
  TestCounters counters;
  HostFixture fixture;
  WasmFrameRenderer frames(fixture.renderer, 72);
  WasmPanelWindows windows(fixture.renderer, fixture.window, fixture.platform);
  WasmGameServices services(
    frames,
    static_cast<std::uint32_t>(GuestCapability::Windows),
    ILLUMO_ENGINE_ASSETS);
  services.setWindows(&windows);
  GuestServices results;
  testTrue(counters,
           processes(services, windowServices(1, openRequest(5)), results) &&
             results.records.size() == 1 &&
             results.records[0].status == GuestServiceStatus::Complete &&
             fixture.platform.created.size() == 1,
           "an open request creates a window");
  FakeSurfaceWindow& window = *fixture.platform.created[0];
  GuestWindowOpened opened;
  testTrue(counters,
           GuestWindowOpened::read(results.records[0].payload, opened) &&
             opened.width == 300 && opened.height == 400,
           "the completion reports the client size");
  testTrue(counters,
           window.x == 120 && window.y == 80 && window.title == "Inspector" &&
             fixture.platform.minimumWidth ==
               static_cast<int>(GuestWindowRequest::MinimumWidth),
           "the window opens relative to the main window with the minimum "
           "size");
  testTrue(counters,
           processes(services, windowServices(2, openRequest(5)), results) &&
             results.records[0].status == GuestServiceStatus::Rejected,
           "a surface opens only once");

  windows.present(frames);
  testTrue(counters,
           window.presents == 0 && windows.replays() == 0,
           "nothing is shown before the guest draws the surface");
  GuestWireWriter first;
  frameWithSurface(5, 1, false).write(first);
  testTrue(counters, frames.accept(first.data()), "a v5 frame is accepted");
  windows.present(frames);
  testTrue(counters,
           windows.replays() == 1 && window.presents == 1 &&
             window.lastWidth == 300 && window.lastHeight == 400 &&
             window.lastImage.size() == 300u * 400u * 4u &&
             window.lastImage[0] == 28 && window.lastImage[3] == 255,
           "the surface replays offscreen and its readback is presented");
  windows.present(frames);
  GuestWireWriter same;
  frameWithSurface(5, 1, true).write(same);
  testTrue(counters, frames.accept(same.data()), "an unchanged surface");
  windows.present(frames);
  testTrue(counters,
           windows.replays() == 1 && window.presents == 1,
           "unchanged content is not replayed or presented again");
  window.repaint = true;
  windows.present(frames);
  testTrue(counters,
           window.presents == 2 && windows.replays() == 1,
           "an exposed window is repainted from its last image");
  GuestWireWriter second;
  frameWithSurface(5, 2, false).write(second);
  testTrue(counters, frames.accept(second.data()), "a changed surface");
  windows.present(frames);
  testTrue(counters,
           windows.replays() == 2 && window.presents == 3,
           "a new revision replays");
  GuestWireWriter stale;
  frameWithSurface(5, 2, false).write(stale);
  testTrue(counters,
           !frames.accept(stale.data()),
           "a changed surface must advance its revision");

  GuestInput input;
  input.events.push_back({ GuestKey::A, GuestKeyAction::Press, 0 });
  SurfaceWindowEvent key;
  key.kind = SurfaceWindowEvent::Kind::Key;
  key.key = KeyCode::B;
  key.action = InputAction::Press;
  SurfaceWindowEvent character;
  character.kind = SurfaceWindowEvent::Kind::Character;
  character.codepoint = 'q';
  SurfaceWindowEvent press;
  press.kind = SurfaceWindowEvent::Kind::MouseButton;
  press.key = KeyCode::MouseLeft;
  press.action = InputAction::Press;
  SurfaceWindowEvent wheel;
  wheel.kind = SurfaceWindowEvent::Kind::Scroll;
  wheel.scroll = -2.0;
  window.queued = { key, character, press, wheel };
  window.hasFocus = true;
  window.cursorX = -40.0;
  window.cursorY = 12.0;
  window.width = 320;
  window.x = 130;
  windows.collectInput(input);
  const GuestSurfaceInput* surface = surfaceInput(input, 5);
  bool focusEvent = false;
  bool resizeEvent = false;
  bool moveEvent = false;
  for (const GuestWindowEvent& event : input.windowEvents) {
    focusEvent = focusEvent || event.kind == GuestWindowEventKind::FocusGained;
    resizeEvent = resizeEvent || (event.kind == GuestWindowEventKind::Resized &&
                                  event.x == 320 && event.y == 400);
    moveEvent = moveEvent ||
                (event.kind == GuestWindowEventKind::Moved && event.x == 130);
  }
  testTrue(counters,
           input.version == 2 && input.events.size() == 2 &&
             input.eventSurfaces == std::vector<std::uint32_t>{ 0, 5 } &&
             input.characterSurfaces == std::vector<std::uint32_t>{ 5 } &&
             input.focusedSurface == 5 && input.originX == 100,
           "window keys and characters are tagged with their surface");
  testTrue(counters,
           surface != nullptr && surface->width == 320 &&
             surface->buttons == GuestSurfaceInput::LeftButton &&
             surface->scroll == -2.0 && surface->mouseX == -40.0 &&
             surface->focused,
           "the surface reports size, buttons, wheel and a captured cursor");
  testTrue(counters,
           focusEvent && resizeEvent && moveEvent,
           "focus, resize and move become window events");
  GuestWireWriter encoded;
  input.write(encoded);
  GuestInput decoded;
  testTrue(counters,
           GuestInput::read(encoded.data(), decoded),
           "the collected input is valid v2");
  windows.present(frames);
  testTrue(counters,
           windows.replays() == 3 && window.lastWidth == 320,
           "a resized window replays at its new size");

  window.close = true;
  GuestInput closing;
  windows.collectInput(closing);
  GuestInput again;
  windows.collectInput(again);
  bool closed = false;
  for (const GuestWindowEvent& event : closing.windowEvents) {
    closed = closed || event.kind == GuestWindowEventKind::Closed;
  }
  bool closedAgain = false;
  for (const GuestWindowEvent& event : again.windowEvents) {
    closedAgain = closedAgain || event.kind == GuestWindowEventKind::Closed;
  }
  testTrue(counters,
           closed && !closedAgain && !fixture.platform.destroyed[0],
           "a user close is reported once and the guest decides");
  GuestWindowRequest close;
  close.action = GuestWindowAction::Close;
  close.surface = 5;
  testTrue(counters,
           processes(services, windowServices(3, close), results) &&
             results.records[0].status == GuestServiceStatus::Complete &&
             fixture.platform.destroyed[0] && windows.openCount() == 0,
           "a close request destroys the window");
  testTrue(counters,
           processes(services, windowServices(4, close), results) &&
             results.records[0].status == GuestServiceStatus::Rejected,
           "closing an unknown window is refused");

  std::uint64_t request = 5;
  bool opened8 = true;
  for (std::uint32_t id = 10; id < 10 + WasmPanelWindows::kMaximumWindows;
       ++id) {
    opened8 = opened8 &&
              processes(services,
                        windowServices(request++, openRequest(id)),
                        results) &&
              results.records[0].status == GuestServiceStatus::Complete;
  }
  testTrue(counters,
           opened8 &&
             processes(
               services, windowServices(request++, openRequest(99)), results) &&
             results.records[0].status == GuestServiceStatus::Rejected,
           "at most eight windows open");
  windows.closeAll();
  bool allDestroyed = true;
  for (std::size_t index = 1; index <= WasmPanelWindows::kMaximumWindows;
       ++index) {
    allDestroyed = allDestroyed && fixture.platform.destroyed[index];
  }
  testTrue(counters,
           allDestroyed && windows.openCount() == 0,
           "closeAll destroys every window");
  GuestWireWriter gone;
  GuestFrame empty;
  empty.write(gone);
  testTrue(counters,
           frames.accept(gone.data()) && frames.surfaces().empty(),
           "a frame without surfaces drops their content");
  return counters.failures;
}

// The guest side, compiled natively: its own window, recording renderer and
// service queue, as GuestModuleApplication composes them.
struct GuestSide
{
  GuestServiceQueue queue;
  GuestSnapshotWindow window;
  EnvVars env;
  Camera camera{ glm::vec2(0, 0), 1, &env };
  GuestRecordingBackend backend{ queue };
  Renderer renderer{ &window, &env, &camera, &backend, false };
  Scene main{ &window, &camera };
  GuestPanelSurfaces panels{ queue, window, camera };
  GuestSide()
  {
    backend.setRenderer(renderer);
    renderer.ensureBuiltinStyles();
    panels.setGranted(true);
  }
};

// One service round trip: host completions in, guest requests out.
static void
exchangeServices(GuestSide& guest,
                 WasmGameServices& services,
                 std::vector<std::byte>& completions)
{
  std::vector<std::byte> requests;
  if (!guest.queue.exchange(completions, requests)) {
    throw std::runtime_error("Guest queue rejected host completions");
  }
  if (!services.process(requests, completions)) {
    throw std::runtime_error(services.error());
  }
}

// Records one frame the way GuestModuleApplication::frame does.
static GuestFrame
recordFrame(GuestSide& guest, GameVisual* panel)
{
  guest.renderer.BeginFrame();
  const std::array<int, 2> dimensions = guest.window.getWindowDimensions();
  guest.backend.setFrame(static_cast<float>(dimensions[0]),
                         static_cast<float>(dimensions[1]));
  guest.backend.pump();
  guest.main.ClearDrawables();
  guest.panels.clearScenes();
  Scene* surface = guest.panels.scene(3);
  if (surface != nullptr && panel != nullptr) {
    surface->AddDrawable(panel, RenderLayerId::UI);
  }
  guest.renderer.RenderScene(&guest.main, &guest.camera);
  guest.panels.record(guest.renderer, guest.backend, guest.window);
  guest.renderer.EndFrame();
  if (!guest.renderer.frameError().empty()) {
    throw std::runtime_error("guest frame: " + guest.renderer.frameError());
  }
  GuestFrame frame = guest.backend.takeFrame();
  guest.panels.finish(frame);
  return frame;
}

static const GuestSurfaceFrame*
surfaceOf(const GuestFrame& frame, std::uint32_t id)
{
  for (const GuestSurfaceFrame& surface : frame.surfaces) {
    if (surface.surface == id) {
      return &surface;
    }
  }
  return nullptr;
}

static int
testGuestPanelSurfaces()
{
  TestCounters counters;
  HostFixture host;
  WasmFrameRenderer frames(host.renderer, 73);
  WasmPanelWindows windows(host.renderer, host.window, host.platform);
  WasmGameServices services(
    frames,
    static_cast<std::uint32_t>(GuestCapability::Render) |
      static_cast<std::uint32_t>(GuestCapability::Windows),
    ILLUMO_ENGINE_ASSETS);
  services.setWindows(&windows);
  GuestSide guest;
  GuestWireWriter empty;
  GuestServices{}.write(empty);
  std::vector<std::byte> completions = empty.take();

  testTrue(counters,
           guest.panels.open(3, "Tools\x01", 40, 60, 320, 240) &&
             guest.panels.state(3) == PanelSurfaceState::Opening &&
             !guest.panels.open(3, "Tools", 40, 60, 320, 240) &&
             !guest.panels.open(0, "Main", 0, 0, 320, 240),
           "open queues a request once; the main surface cannot open");
  exchangeServices(guest, services, completions);
  testTrue(counters,
           host.platform.created.size() == 1 &&
             host.platform.created[0]->title == "Tools " &&
             host.platform.created[0]->x == 140,
           "the host opens the window with a sanitized title");
  exchangeServices(guest, services, completions);
  guest.panels.pump();
  std::vector<PanelSurfaceEvent> events = guest.panels.takeEvents();
  testTrue(counters,
           guest.panels.state(3) == PanelSurfaceState::Open &&
             guest.panels.size(3) == std::array<int, 2>{ 320, 240 } &&
             events.size() == 1 &&
             events[0].kind == PanelSurfaceEvent::Kind::Opened,
           "the completion opens the surface with its size");

  GameVisual panel;
  panel.setWindow(&guest.window);
  panel.setSpace(PrimitiveSpace::Pixels);
  panel.prepare(&guest.renderer);
  panel.addFilledRect(10, 10, 100, 50, ColorRgba{ 200, 60, 40, 255 });
  // Mesh creation for the panel travels as services; draw once, exchange,
  // then draw with the host copies ready.
  GuestFrame warm = recordFrame(guest, &panel);
  exchangeServices(guest, services, completions);
  exchangeServices(guest, services, completions);
  GuestWireWriter warmBytes;
  warm.write(warmBytes);
  testTrue(counters,
           frames.accept(warmBytes.data()) && surfaceOf(warm, 3) != nullptr &&
             surfaceOf(warm, 3)->revision == 1 &&
             surfaceOf(warm, 3)->width == 320 &&
             !surfaceOf(warm, 3)->batches.empty() && warm.batches.empty(),
           "the surface records its own batches, apart from the main frame");
  windows.present(frames);
  testTrue(counters,
           host.platform.created[0]->presents == 1,
           "the host presents the recorded surface");
  GuestFrame again = recordFrame(guest, &panel);
  GuestWireWriter againBytes;
  again.write(againBytes);
  const GuestSurfaceFrame* repeated = surfaceOf(again, 3);
  bool unchanged =
    repeated != nullptr &&
    (repeated->same ? repeated->revision == 1 : repeated->revision == 2);
  testTrue(counters,
           frames.accept(againBytes.data()) && unchanged,
           "a repeated frame is `same` or advances once its meshes settle");
  const std::uint64_t replays = windows.replays();
  GuestFrame steady = recordFrame(guest, &panel);
  GuestWireWriter steadyBytes;
  steady.write(steadyBytes);
  testTrue(counters,
           surfaceOf(steady, 3) != nullptr && surfaceOf(steady, 3)->same &&
             frames.accept(steadyBytes.data()),
           "unchanged content travels as `same`");
  windows.present(frames);
  windows.present(frames);
  testTrue(counters,
           windows.replays() <= replays + 1,
           "an unchanged surface is not replayed again");
  panel.clearPrimitives();
  panel.addFilledRect(10, 10, 120, 50, ColorRgba{ 40, 200, 60, 255 });
  GuestFrame changed = recordFrame(guest, &panel);
  testTrue(counters,
           surfaceOf(changed, 3) != nullptr && !surfaceOf(changed, 3)->same &&
             surfaceOf(changed, 3)->revision > surfaceOf(steady, 3)->revision,
           "changed content advances the revision");
  GuestWireWriter changedBytes;
  changed.write(changedBytes);
  testTrue(counters, frames.accept(changedBytes.data()), "the host accepts it");

  FakeSurfaceWindow& os = *host.platform.created[0];
  os.cursorX = 25.0;
  os.cursorY = 30.0;
  os.hasFocus = true;
  SurfaceWindowEvent press;
  press.kind = SurfaceWindowEvent::Kind::MouseButton;
  press.key = KeyCode::MouseLeft;
  press.action = InputAction::Press;
  os.queued.push_back(press);
  os.close = true;
  GuestInput input;
  windows.collectInput(input);
  GuestWireWriter inputBytes;
  input.write(inputBytes);
  GuestInput received;
  testTrue(counters,
           GuestInput::read(inputBytes.data(), received),
           "the host's input v2 decodes in the guest");
  guest.window.accept(received);
  guest.panels.accept(received);
  events = guest.panels.takeEvents();
  bool closeRequested = false;
  for (const PanelSurfaceEvent& event : events) {
    closeRequested =
      closeRequested || event.kind == PanelSurfaceEvent::Kind::CloseRequested;
  }
  const PanelSurfacePointer pointer = guest.panels.pointer(3);
  testTrue(counters,
           pointer.x == 25.0 && pointer.left && guest.panels.focused() == 3 &&
             guest.panels.origin(0)[0] == host.platform.mainX,
           "the surface pointer, focus and main origin reach the guest");
  testTrue(counters,
           closeRequested && guest.panels.state(3) == PanelSurfaceState::Open,
           "a user close is a request the product answers");
  guest.panels.close(3);
  exchangeServices(guest, services, completions);
  GuestFrame closed = recordFrame(guest, &panel);
  GuestWireWriter closedBytes;
  closed.write(closedBytes);
  testTrue(counters,
           guest.panels.state(3) == PanelSurfaceState::Closed &&
             host.platform.destroyed[0] && closed.surfaces.empty() &&
             frames.accept(closedBytes.data()) && frames.surfaces().empty(),
           "close removes the window and its content");

  testTrue(counters,
           guest.panels.open(4, "Refused", 0, 0, 200, 200),
           "a second surface asks to open");
  host.platform.supported = false;
  exchangeServices(guest, services, completions);
  exchangeServices(guest, services, completions);
  guest.panels.pump();
  events = guest.panels.takeEvents();
  testTrue(counters,
           guest.panels.state(4) == PanelSurfaceState::Failed &&
             events.size() == 1 &&
             events[0].kind == PanelSurfaceEvent::Kind::Failed &&
             guest.panels.scene(4) == nullptr,
           "a refused open fails and draws nothing");
  GuestSide ungranted;
  ungranted.panels.setGranted(false);
  testTrue(counters,
           !ungranted.panels.available() &&
             !ungranted.panels.open(5, "No", 0, 0, 200, 200),
           "without the grant nothing opens");
  return counters.failures;
}

// A panel of 240 rows (a rect, an outline and a rule each) recorded into a
// 400x800 surface: guest record+finish for changed and unchanged content,
// and the host's accept+present (replay into a target plus readback) for
// each. Prints one JSON line.
static void
fillPanel(GameVisual& panel, int variant)
{
  panel.clearPrimitives();
  for (int row = 0; row < 240; ++row) {
    const float y = 4.0f + static_cast<float>(row) * 3.3f;
    const unsigned char shade =
      static_cast<unsigned char>((row * 7 + variant * 31) % 200 + 40);
    panel.addFilledRect(4.0f, y, 392.0f, 3.0f, ColorRgba{ shade, 60, 70, 255 });
    panel.addOutlineRect(
      8.0f, y, 120.0f, 3.0f, ColorRgba{ 90, 90, 100, 255 }, 1.0f);
    panel.addLine(130.0f, y, 390.0f, y, ColorRgba{ 50, 52, 58, 255 }, 1.0f);
  }
}

static int
benchPanelSurface()
{
  HostFixture host;
  WasmFrameRenderer frames(host.renderer, 91);
  WasmPanelWindows windows(host.renderer, host.window, host.platform);
  WasmGameServices services(
    frames,
    static_cast<std::uint32_t>(GuestCapability::Render) |
      static_cast<std::uint32_t>(GuestCapability::Windows),
    ILLUMO_ENGINE_ASSETS);
  services.setWindows(&windows);
  GuestSide guest;
  GuestWireWriter empty;
  GuestServices{}.write(empty);
  std::vector<std::byte> completions = empty.take();
  guest.panels.open(3, "Bench", 40, 60, 400, 800);
  exchangeServices(guest, services, completions);
  exchangeServices(guest, services, completions);
  guest.panels.pump();
  guest.panels.takeEvents();
  GameVisual panel(4096u);
  panel.setWindow(&guest.window);
  panel.setSpace(PrimitiveSpace::Pixels);
  panel.prepare(&guest.renderer);
  fillPanel(panel, 0);
  for (int warm = 0; warm < 3; ++warm) {
    GuestFrame frame = recordFrame(guest, &panel);
    exchangeServices(guest, services, completions);
    exchangeServices(guest, services, completions);
    GuestWireWriter bytes;
    frame.write(bytes);
    frames.accept(bytes.data());
    windows.present(frames);
  }
  const int frameCount = 200;
  double recordSeconds[2] = { 0.0, 0.0 };
  double hostSeconds[2] = { 0.0, 0.0 };
  std::size_t surfaceBytes = 0;
  for (int pass = 0; pass < 2; ++pass) {
    const bool changing = pass == 0;
    for (int index = 0; index < frameCount; ++index) {
      if (changing) {
        fillPanel(panel, index + 1);
      }
      const std::chrono::steady_clock::time_point start =
        std::chrono::steady_clock::now();
      GuestFrame frame = recordFrame(guest, &panel);
      const std::chrono::steady_clock::time_point recorded =
        std::chrono::steady_clock::now();
      exchangeServices(guest, services, completions);
      GuestWireWriter bytes;
      frame.write(bytes);
      const std::chrono::steady_clock::time_point hostStart =
        std::chrono::steady_clock::now();
      if (!frames.accept(bytes.data())) {
        std::fprintf(stderr, "Host refused a bench frame\n");
        return 1;
      }
      windows.present(frames);
      const std::chrono::steady_clock::time_point done =
        std::chrono::steady_clock::now();
      recordSeconds[pass] +=
        std::chrono::duration<double>(recorded - start).count();
      hostSeconds[pass] +=
        std::chrono::duration<double>(done - hostStart).count();
      if (changing) {
        surfaceBytes = std::max(surfaceBytes, bytes.data().size());
      }
    }
  }
  const double micro = 1.0e6 / static_cast<double>(frameCount);
  std::printf("{\"benchmark\":\"Illumo.Wasm.Bench.PanelSurface\","
              "\"rows\":240,\"frames\":%d,"
              "\"recordChangedUs\":%.1f,\"recordSameUs\":%.1f,"
              "\"hostChangedUs\":%.1f,\"hostSameUs\":%.1f,"
              "\"frameBytes\":%zu,\"replays\":%llu,\"presents\":%llu}\n",
              frameCount,
              recordSeconds[0] * micro,
              recordSeconds[1] * micro,
              hostSeconds[0] * micro,
              hostSeconds[1] * micro,
              surfaceBytes,
              static_cast<unsigned long long>(windows.replays()),
              static_cast<unsigned long long>(windows.presents()));
  return 0;
}

int
main(int argc, char** argv)
{
  if (argc == 2 && std::string(argv[1]) == "--list") {
    std::puts(
      "Illumo.Wasm.WindowServiceDecoder\nIllumo.Wasm.InputV2\n"
      "Illumo.Wasm.FrameV5Surfaces\nIllumo.Wasm.WindowDeny\n"
      "Illumo.Wasm.PanelWindowsLifecycle\nIllumo.Wasm.GuestPanelSurfaces\n"
      "Illumo.Wasm.Bench.PanelSurface");
    return 0;
  }
  if (argc != 3 || std::string(argv[1]) != "--run") {
    return 2;
  }
  const std::string name(argv[2]);
  int failures = -1;
  if (name == "Illumo.Wasm.WindowServiceDecoder") {
    failures = testWindowServiceDecoder();
  } else if (name == "Illumo.Wasm.InputV2") {
    failures = testInputV2();
  } else if (name == "Illumo.Wasm.FrameV5Surfaces") {
    failures = testFrameV5Surfaces();
  } else if (name == "Illumo.Wasm.WindowDeny") {
    failures = testWindowDeny();
  } else if (name == "Illumo.Wasm.PanelWindowsLifecycle") {
    failures = testPanelWindowsLifecycle();
  } else if (name == "Illumo.Wasm.Bench.PanelSurface") {
    try {
      failures = benchPanelSurface();
    } catch (const std::exception& exception) {
      std::printf("FAIL: %s\n", exception.what());
      failures = 1;
    }
  } else if (name == "Illumo.Wasm.GuestPanelSurfaces") {
    try {
      failures = testGuestPanelSurfaces();
    } catch (const std::exception& exception) {
      std::printf("FAIL: %s\n", exception.what());
      failures = 1;
    }
  }
  if (failures < 0) {
    return 2;
  }
  std::printf("%s: %d failure(s)\n", name.c_str(), failures);
  return failures == 0 ? 0 : 1;
}
