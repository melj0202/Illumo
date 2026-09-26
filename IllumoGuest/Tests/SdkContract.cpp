#include <Illumo/Rendering/Camera.h>
#include <Illumo/Rendering/Primitives/GameVisual.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <IllumoGuest/Application.h>
#include <IllumoGuest/Clipboard.h>
#include <IllumoGuest/Console.h>
#include <IllumoGuest/Dialog.h>
#include <IllumoGuest/FontProvider.h>
#include <IllumoGuest/InputProvider.h>
#include <IllumoGuest/RenderWorld.h>
#include <IllumoGuest/SnapshotWindow.h>
#include <algorithm>
#include <array>
#include <functional>

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
  // The visual's dynamic meshes also request host copies (frame schema v4);
  // completing them without an id leaves them drawing inline.
  std::size_t releases = 0;
  bool onlyMeshes = true;
  for (GuestServiceRecord& record : pending.records) {
    releases += record.operation == GuestService::ReleaseTexture ? 1u : 0u;
    onlyMeshes =
      onlyMeshes && (record.operation == GuestService::ReleaseTexture ||
                     record.operation == GuestService::CreateMesh);
    record.payload.clear();
    record.status = GuestServiceStatus::Complete;
  }
  require(releases == 1 && onlyMeshes,
          "Clearing font cache drains pending atlas acquisition");
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

  GuestServiceQueue cursorQueue;
  GuestDisplay cursorDisplay(cursorQueue);
  cursorDisplay.apply({ false, true, 60, 1, true });
  cursorDisplay.pump();
  GuestServices cursorRequest = exchange(cursorQueue);
  GuestDisplayRequest decoded;
  require(
    cursorRequest.records.size() == 1 &&
      GuestDisplayRequest::read(cursorRequest.records[0].payload, decoded) &&
      decoded.version == kGuestDisplayVersion && decoded.state.hideSystemCursor,
    "A system-cursor request travels in the current display version");
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

// Frame schema v4: a dynamic mesh draws inline until its host copy exists,
// then by reference with only its changed spans travelling; a change after a
// by-reference draw in the same frame turns that draw back into inline data.
static void
dynamicMeshContract()
{
  GuestServiceQueue queue;
  GuestRecordingBackend backend(queue, 2048);
  GuestSnapshotWindow window;
  Renderer renderer(&window, nullptr, nullptr, &backend, false);
  backend.setRenderer(renderer);
  renderer.ensureBuiltinStyles();
  const std::array<std::uint32_t, 3> indices{ 0, 1, 2 };
  const MeshHandle mesh = renderer.enrollDynamicMesh(
    3 * 16, indices.data(), sizeof(indices), MeshVertexLayout::Pos3Color4U8);
  require(mesh.isValid(), "Dynamic mesh enrollment");
  // Token payloads are borrowed until submission: one array per update.
  const std::array<float, 12> vertices{ 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 };
  const std::array<float, 12> shifted{ 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, 0 };
  const std::array<float, 4> moved{ 5, 0, 0, 0 };
  const float* uploaded = vertices.data();
  const std::function<GuestFrame(bool)> record = [&](bool updateAfterDraw) {
    renderer.BeginFrame();
    backend.setFrame(1280, 720);
    backend.pump();
    renderer.bindStyle(RenderStyleId::Shape);
    renderer.pushUpdateBuffer(mesh, 0, sizeof(vertices), uploaded);
    renderer.pushSetMesh(mesh);
    renderer.pushDrawIndexed(3, 0);
    if (updateAfterDraw) {
      renderer.pushUpdateBuffer(mesh, 0, sizeof(moved), moved.data());
      renderer.pushDrawIndexed(3, 0);
    }
    renderer.EndFrame();
    return backend.takeFrame();
  };
  GuestFrame inlineFrame = record(false);
  require(inlineFrame.batches.size() == 1 &&
            !inlineFrame.batches[0].retained() &&
            inlineFrame.meshWrites.empty(),
          "Dynamic mesh draws inline before its host copy exists");
  GuestServices pending = exchange(queue);
  require(pending.records.size() == 1 &&
            pending.records[0].operation == GuestService::CreateMesh,
          "Dynamic mesh requests one host copy");
  GuestMeshRequest request;
  require(GuestMeshRequest::read(pending.records[0].payload, request) &&
            request.dynamic && request.style == 1 && request.vertexBytes == 48,
          "Dynamic mesh request encoding");
  const GuestResourceId host{ 7, GuestResourceKind::Mesh, 1, 1 };
  GuestWireWriter created;
  host.write(created);
  pending.records[0].payload = created.take();
  pending.records[0].status = GuestServiceStatus::Complete;
  exchange(queue, pending);
  const GuestFrame first = record(false);
  require(first.batches.size() == 1 && first.batches[0].retained() &&
            first.meshWrites.size() == 2,
          "First referenced frame sends the whole mesh");
  const GuestFrame unchanged = record(false);
  require(unchanged.batches.size() == 1 && unchanged.batches[0].retained() &&
            unchanged.meshWrites.empty(),
          "Rewriting identical bytes sends nothing");
  uploaded = shifted.data();
  const GuestFrame changed = record(false);
  require(changed.batches.size() == 1 && changed.batches[0].retained() &&
            changed.meshWrites.size() == 1 && !changed.meshWrites[0].indices &&
            changed.meshWrites[0].offset == 16 &&
            changed.meshWrites[0].bytes.size() == 16,
          "Only the changed vertex travels");
  const GuestFrame reordered = record(true);
  require(reordered.batches.size() == 2 && !reordered.batches[0].retained() &&
            !reordered.batches[1].retained() &&
            reordered.batches[0].vertices[0].position[0] == 0.0f &&
            reordered.batches[1].vertices[0].position[0] == 5.0f,
          "Change after a referenced draw keeps painter order inline");
  backend.Shutdown();
}

// Every shaped static mesh gets one host copy, however small: it draws inline
// while that copy uploads, then by reference with no geometry in the frame.
static void
staticMeshContract()
{
  GuestServiceQueue queue;
  GuestRecordingBackend backend(queue, 2048);
  GuestSnapshotWindow window;
  Renderer renderer(&window, nullptr, nullptr, &backend, false);
  backend.setRenderer(renderer);
  renderer.ensureBuiltinStyles();
  const std::array<float, 12> vertices{ 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 };
  const std::array<std::uint32_t, 3> indices{ 0, 1, 2 };
  const MeshHandle mesh = renderer.enrollMesh(vertices.data(),
                                              sizeof(vertices),
                                              indices.data(),
                                              sizeof(indices),
                                              MeshVertexLayout::Pos3Color4U8,
                                              false);
  require(mesh.isValid(), "Static mesh enrollment");
  const std::function<GuestFrame()> record = [&]() {
    renderer.BeginFrame();
    backend.setFrame(1280, 720);
    backend.pump();
    renderer.bindStyle(RenderStyleId::Shape);
    renderer.pushSetMesh(mesh);
    renderer.pushDrawIndexed(3, 0);
    renderer.EndFrame();
    return backend.takeFrame();
  };
  const GuestFrame before = record();
  require(before.batches.size() == 1 && !before.batches[0].retained() &&
            before.batches[0].vertices.size() == 3,
          "Small static mesh draws inline before its host copy exists");
  GuestServices pending = exchange(queue);
  GuestMeshRequest request;
  require(pending.records.size() == 1 &&
            pending.records[0].operation == GuestService::CreateMesh &&
            GuestMeshRequest::read(pending.records[0].payload, request) &&
            !request.dynamic && request.vertexBytes == sizeof(vertices),
          "Small static mesh requests one host copy");
  const GuestResourceId host{ 7, GuestResourceKind::Mesh, 1, 1 };
  GuestWireWriter created;
  host.write(created);
  pending.records[0].payload = created.take();
  pending.records[0].status = GuestServiceStatus::Complete;
  exchange(queue, pending);
  const GuestFrame uploading = record();
  require(uploading.batches.size() == 1 && !uploading.batches[0].retained(),
          "Small static mesh stays inline while its bytes upload");
  pending = exchange(queue);
  require(pending.records.size() == 2 &&
            pending.records[0].operation == GuestService::WriteMesh &&
            pending.records[1].operation == GuestService::WriteMesh,
          "Vertices and indices upload once");
  for (GuestServiceRecord& entry : pending.records) {
    entry.payload.clear();
    entry.status = GuestServiceStatus::Complete;
  }
  exchange(queue, pending);
  const GuestFrame referenced = record();
  require(referenced.batches.size() == 1 && referenced.batches[0].retained() &&
            referenced.batches[0].vertices.empty() &&
            referenced.batches[0].mesh.slot == host.slot,
          "Uploaded static mesh draws by reference");
  backend.Shutdown();
}

// Past the host's shadow caster limit, extra casters grow the nearest
// compatible caster instead of vanishing from the shared shadow fit.
static void
shadowCasterContract()
{
  GuestServiceQueue queue;
  GuestRecordingBackend backend(queue, 2048);
  GuestSnapshotWindow window;
  Renderer renderer(&window, nullptr, nullptr, &backend, false);
  backend.setRenderer(renderer);
  renderer.BeginFrame();
  backend.setFrame(1280, 720);
  const std::size_t limit = GuestFrameLimits{}.shadowCasters;
  for (std::size_t index = 0; index <= limit; ++index) {
    Renderer::ShadowCasterDesc caster;
    const float x = static_cast<float>(index) * 4.0f;
    caster.boundsMin = { x, 0, 0 };
    caster.boundsMax = { x + 1, 1, 1 };
    renderer.registerShadowCaster(caster);
  }
  backend.BeginLayer(RenderLayerId::World);
  renderer.EndFrame();
  const GuestFrame frame = backend.takeFrame();
  const float last = static_cast<float>(limit) * 4.0f;
  require(frame.shadowCasters.size() == limit &&
            frame.shadowCasters.back().boundsMin[0] == last - 4.0f &&
            frame.shadowCasters.back().boundsMax[0] == last + 1.0f,
          "Overflow caster merges into its nearest compatible caster");
  backend.Shutdown();
}

// Guests find over-quota frames before sending them; unchanged surfaces do
// not count against the batch quota.
static void
frameLimitContract()
{
  GuestFrame frame;
  frame.batches.resize(2);
  require(frame.exceededLimit() == nullptr, "A small frame is within quota");
  GuestSurfaceFrame unchanged;
  unchanged.same = true;
  unchanged.batches.resize(GuestFrameLimits{}.batches);
  frame.surfaces.push_back(unchanged);
  require(frame.exceededLimit() == nullptr,
          "Unchanged surfaces do not count against the batch quota");
  frame.batches.resize(GuestFrameLimits{}.batches + 1);
  require(frame.exceededLimit() != nullptr,
          "A frame over the batch quota is reported");
}

// Frame schema v6: GuestRenderWorld holds an instance until its mesh's host
// copy exists, folds later changes into the waiting create, and never emits
// an operation the host would refuse.
static void
renderWorldContract()
{
  GuestServiceQueue queue;
  GuestRecordingBackend backend(queue, 2048);
  GuestSnapshotWindow window;
  Renderer renderer(&window, nullptr, nullptr, &backend, false);
  backend.setRenderer(renderer);
  const std::array<float, 27> vertices{};
  const std::array<std::uint32_t, 3> indices{ 0, 1, 2 };
  const MeshHandle mesh =
    renderer.enrollMesh(vertices.data(),
                        sizeof(vertices),
                        indices.data(),
                        sizeof(indices),
                        MeshVertexLayout::Pos3Norm3Color4U8Uv2,
                        false);
  const MeshHandle shape = renderer.enrollDynamicMesh(
    3 * 16, indices.data(), sizeof(indices), MeshVertexLayout::Pos3Color4U8);
  GuestRenderWorld world(backend);
  std::vector<GuestWorldOperation> operations;

  RenderInstanceDesc desc;
  desc.mesh = mesh;
  desc.indexCount = 3;
  desc.material = 1;
  RenderInstanceDesc unusable = desc;
  unusable.mesh = shape;
  require(world.createMaterial(1, RenderMaterialDesc{}) &&
            !world.createMaterial(1, RenderMaterialDesc{}) &&
            !world.createInstance(9, unusable) &&
            world.createInstance(10, desc) && world.waitingInstances() == 1,
          "An instance waits for its mesh; unusable meshes are refused");
  std::array<float, 16> moved{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
  moved[12] = 4.0f;
  require(world.setInstanceTransform(10, moved) &&
            world.queuedOperations() == 1,
          "Changes to a waiting instance do not travel");
  world.takeOperations(operations, 64);
  require(operations.size() == 1 &&
            operations[0].op == GuestWorldOp::MaterialCreate &&
            world.waitingInstances() == 1,
          "Only the material reaches the host before the mesh is ready");

  // Complete the mesh's host copy: create, then its vertex and index bytes.
  backend.pump();
  GuestServices pending = exchange(queue);
  const GuestResourceId host{ 7, GuestResourceKind::Mesh, 4, 1 };
  GuestWireWriter created;
  host.write(created);
  const std::vector<std::byte> hostBytes = created.take();
  for (GuestServiceRecord& record : pending.records) {
    record.payload = record.operation == GuestService::CreateMesh
                       ? hostBytes
                       : std::vector<std::byte>{};
    record.status = GuestServiceStatus::Complete;
  }
  exchange(queue, pending);
  backend.pump();
  pending = exchange(queue);
  for (GuestServiceRecord& record : pending.records) {
    record.payload.clear();
    record.status = GuestServiceStatus::Complete;
  }
  exchange(queue, pending);
  backend.pump();

  operations.clear();
  world.takeOperations(operations, 64);
  require(operations.size() == 1 &&
            operations[0].op == GuestWorldOp::InstanceCreate &&
            operations[0].id == 10 && operations[0].mesh.slot == host.slot &&
            operations[0].transform[12] == 4.0f &&
            world.waitingInstances() == 0,
          "The ready instance is created with its host mesh and latest "
          "transform");
  require(!world.destroyMaterial(1) && world.destroyInstance(10) &&
            world.destroyMaterial(1),
          "A material is destroyed only once unused");
  operations.clear();
  world.takeOperations(operations, 64);
  require(operations.size() == 2 &&
            operations[0].op == GuestWorldOp::InstanceDestroy &&
            operations[1].op == GuestWorldOp::MaterialDestroy,
          "Destruction travels in order");

  for (RenderMaterialId id = 20; id < 25; ++id) {
    world.createMaterial(id, RenderMaterialDesc{});
  }
  operations.clear();
  world.takeOperations(operations, 2);
  require(operations.size() == 2 && operations[0].id == 20 &&
            world.queuedOperations() == 3,
          "The per-frame quota keeps the rest queued in order");
  backend.Shutdown();
}

// Frame schema v7: GameVisuals travel as host visuals placed by a
// composition. Only changes travel, a dropped frame's changes are sent again,
// and visuals that cannot travel record their own batches in painter order.
static void
visualProxyContract()
{
  GuestServiceQueue queue;
  GuestRecordingBackend backend(queue, 2048);
  GuestSnapshotWindow window;
  Renderer renderer(&window, nullptr, nullptr, &backend, false);
  backend.setRenderer(renderer);
  renderer.ensureBuiltinStyles();
  backend.setVisuals(true, true);
  auto first = std::make_unique<GameVisual>();
  auto styled = std::make_unique<GameVisual>();
  auto last = std::make_unique<GameVisual>();
  for (GameVisual* visual : { first.get(), styled.get(), last.get() }) {
    visual->setWindow(&window);
  }
  first->addFilledRect(10, 10, 40, 20, { 200, 0, 0, 255 });
  first->addFilledRect(60, 10, 40, 20, { 0, 200, 0, 255 });
  styled->addFilledRect(0, 40, 10, 10, { 9, 9, 9, 255 });
  styled->getShape(0)->styleHandle =
    renderer.getBuiltinStyleHandle(RenderStyleId::Shape);
  last->addLine(0, 0, 50, 50, { 1, 2, 3, 255 }, 2.0f);
  GuestFrame frame;
  const auto record = [&](std::initializer_list<GameVisual*> visuals) {
    renderer.BeginFrame();
    backend.setFrame(1280, 720);
    for (GameVisual* visual : visuals) {
      visual->AppendCommands(&renderer);
    }
    renderer.EndFrame();
    backend.takeFrame(frame);
  };
  const auto ops = [&](GuestVisualOp op) {
    return std::count_if(frame.visualOperations.begin(),
                         frame.visualOperations.end(),
                         [op](const GuestVisualOperation& operation) {
                           return operation.op == op;
                         });
  };

  record({ first.get() });
  require(
    frame.batches.empty() && frame.visualOperations.size() == 4 &&
      ops(GuestVisualOp::Create) == 1 && ops(GuestVisualOp::Set) == 1 &&
      ops(GuestVisualOp::ItemSet) == 2 && frame.compositions.size() == 1 &&
      frame.compositions[0].entries.size() == 2 &&
      frame.compositions[0].entries[0].kind == GuestCompositionKind::World &&
      frame.compositions[0].entries[1].kind == GuestCompositionKind::Visual &&
      frame.compositions[0].width == 1280.0f &&
      frame.exceededLimit() == nullptr,
    "A new visual is created with its items and placed after the world");
  const std::uint32_t firstId = frame.compositions[0].entries[1].first;
  backend.commitVisuals();

  record({ first.get() });
  require(frame.visualOperations.empty() && frame.compositions.size() == 1 &&
            frame.compositions[0].same,
          "An unchanged visual sends nothing and a `same` composition");
  backend.commitVisuals();

  first->getShape(1)->color = { 0, 0, 200, 255 };
  record({ first.get() });
  require(frame.visualOperations.size() == 1 &&
            frame.visualOperations[0].op == GuestVisualOp::ItemSet &&
            frame.visualOperations[0].index == 1,
          "One changed item sends one ItemSet");
  backend.dropVisuals();
  record({ first.get() });
  require(frame.visualOperations.size() == 1 &&
            frame.visualOperations[0].index == 1,
          "A dropped frame's change is sent again");
  backend.commitVisuals();

  // Immediate-mode rebuild: identical items travel as nothing, a shorter
  // list as one removal.
  first->clearPrimitives();
  first->addFilledRect(10, 10, 40, 20, { 200, 0, 0, 255 });
  record({ first.get() });
  require(frame.visualOperations.size() == 1 &&
            frame.visualOperations[0].op == GuestVisualOp::ItemRemove &&
            frame.visualOperations[0].index == 1 &&
            frame.visualOperations[0].count == 1,
          "A rebuilt visual sends only its shortened tail");
  backend.commitVisuals();

  record({ first.get(), styled.get(), last.get() });
  const std::vector<GuestCompositionEntry>& entries =
    frame.compositions[0].entries;
  require(frame.batches.size() == 1 && entries.size() == 4 &&
            entries[1].kind == GuestCompositionKind::Visual &&
            entries[1].first == firstId &&
            entries[2].kind == GuestCompositionKind::Batches &&
            entries[2].first == 0 && entries[2].count == 1 &&
            entries[3].kind == GuestCompositionKind::Visual &&
            entries[3].first != firstId,
          "A custom-styled visual records its batch between the proxied ones");
  const std::uint32_t lastId = entries[3].first;
  backend.commitVisuals();

  last.reset();
  record({ first.get() });
  require(ops(GuestVisualOp::Destroy) == 1 &&
            frame.visualOperations[0].id == lastId,
          "A destroyed visual is destroyed on the host");
  backend.dropVisuals();
  record({ first.get() });
  require(ops(GuestVisualOp::Destroy) == 1, "An undelivered destroy is resent");
  backend.commitVisuals();
  record({ first.get() });
  require(ops(GuestVisualOp::Destroy) == 0,
          "A delivered destroy is not resent");

  backend.setVisuals(false, true);
  record({ first.get() });
  require(frame.compositions.empty() && frame.batches.size() == 1,
          "Disabled visuals record their own batches");
  first.reset();
  styled.reset();
  backend.Shutdown();
}

// Frame schema v7: a SkyboxVisual becomes the render world's sky. It follows
// the frame, only changes travel, and it records nothing.
static void
skyboxContract()
{
  GuestServiceQueue queue;
  GuestRecordingBackend backend(queue, 2048);
  GuestSnapshotWindow window;
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, nullptr);
  Renderer renderer(&window, nullptr, &camera, &backend, false);
  backend.setRenderer(renderer);
  renderer.ensureBuiltinStyles();
  GuestRenderWorld world(backend);
  backend.setRenderWorld(&world);
  std::array<unsigned char, 4> pixel{ 255, 255, 255, 255 };
  std::array<const unsigned char*, 6> faces{};
  faces.fill(pixel.data());
  const TextureHandle cubemap = renderer.enrollCubemap(faces, 1, 1, 4);
  SkyboxVisual sky(cubemap);
  std::vector<GuestWorldOperation> operations;
  GuestFrame frame;
  const auto record = [&](bool drawSky) {
    world.beginFrame();
    renderer.BeginFrame();
    backend.setFrame(1280, 720);
    backend.setLayer(GuestLayer::World);
    if (drawSky) {
      sky.AppendCommands(&renderer);
    }
    renderer.EndFrame();
    backend.takeFrame(frame);
    operations.clear();
    world.takeOperations(operations, 64);
  };

  record(true);
  require(operations.empty(), "A cubemap without a host copy shows no sky");
  // Complete the cubemap's acquisition with a host id.
  backend.pump();
  GuestServices pending = exchange(queue);
  const GuestResourceId host{ 7, GuestResourceKind::Texture, 5, 1 };
  GuestWireWriter created;
  host.write(created);
  for (GuestServiceRecord& record : pending.records) {
    record.payload = record.operation == GuestService::CreateCubemap
                       ? created.data()
                       : std::vector<std::byte>{};
    record.status = GuestServiceStatus::Complete;
  }
  exchange(queue, pending);
  backend.pump();

  record(true);
  require(frame.batches.empty() && operations.size() == 1 &&
            operations[0].op == GuestWorldOp::Skybox &&
            operations[0].texture.slot == host.slot,
          "A ready sky is sent once as a Skybox operation, with no batch");
  record(true);
  require(operations.empty(), "An unchanged sky sends nothing");
  sky.setTint(glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
  record(true);
  require(operations.size() == 1 && operations[0].tint[0] == 0.5f,
          "A new tint is sent");
  record(false);
  require(operations.size() == 1 && operations[0].texture.owner == 0,
          "A frame without the sky removes it");
  backend.setRenderWorld(nullptr);
  record(true);
  require(operations.empty() && frame.batches.size() == 1,
          "Without a render world the sky records its cube");
  backend.Shutdown();
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
    dynamicMeshContract();
    staticMeshContract();
    shadowCasterContract();
    frameLimitContract();
    renderWorldContract();
    visualProxyContract();
    skyboxContract();
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
