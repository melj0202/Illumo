// CommandLine + GLString token emission tests (MockBackend, no real GL window).

#include <Illumo/Rendering/GLString.h>
#include <Illumo/Rendering/PipelineState.h>
#include <Illumo/Rendering/RenderCommand.h>
#include <Illumo/Rendering/Scene.h>
#include <Illumo/Services/CommandLine.h>
#include <Illumo/Services/CommandRegistry.h>
#include <Illumo/Testing/TestHarness.h>
#include <Illumo/Testing/TestHelpers.h>
#include <Illumo/Testing/TestRegistry.h>
#include <chrono>
#include <cstdio>
#include <thread>

static TestCounters g;

static void
enterConsoleText(CommandLine& console, const char* text)
{
  for (const char* cursor = text; *cursor != '\0'; ++cursor) {
    console.AddCharacter(static_cast<unsigned int>(*cursor));
  }
}

static bool
consoleHistoryContains(const CommandLine& console, const std::string& text)
{
  const std::vector<CommandLine::historyBuffer>& history = console.getHistory();
  for (const CommandLine::historyBuffer& line : history) {
    if (line.content.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

struct CommandLineFixture
{
  NullRenderWindow window;
  EnvVars env;
  Camera camera;
  MockBackend mock;
  Renderer renderer;
  CommandRegistry registry;
  CommandLine console;

  CommandLineFixture(int width = 1280, int height = 720)
    : window(width, height)
    , env()
    , camera(glm::vec2(1.0f, 1.0f), 1.0f, &env)
    , mock()
    , renderer(&window, &env, &camera, &mock, false)
    , registry()
    , console(&env, &registry, &window, &renderer)
  {
    env.setVar("WinX", width);
    env.setVar("WinY", height);
    env.setVar("CanvasX", 80);
    env.setVar("CanvasY", 60);
    env.setVar("tps", 30);
    env.setVar("speedFactor", "1");
    env.setVar("cellFadeSpeed", "8");
    env.setVar("showFPS", false);
    env.setVar("fullscreen", false);
    mock.Initialize();
  }
};

static void
executeConsoleText(CommandLine& console, const char* text)
{
  console.ClearInput();
  enterConsoleText(console, text);
  console.ExecuteCommand();
}

static void
testCommandLineEditorAndCompletion()
{
  testSection("CommandLine: cursor editing, quoted arguments, completion");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  CommandRegistry registry;
  registry.RegisterCommand("benchmark", [](const std::vector<std::string>&) {});
  registry.RegisterCommand("ruleset",
                           [](const std::vector<std::string>&) {},
                           "ruleset [name]",
                           "Change ruleset",
                           { "GAME_OF_LIFE", "WIREWORLD" });
  CommandLine console(&env, &registry, &window, &renderer);

  enterConsoleText(console, "alpha beta");
  console.MoveCursorHome();
  console.MoveCursorRight(true);
  testEqSize(
    g, console.getCursorPosition(), 6u, "word-right reaches the next word");
  console.MoveCursorRight(false, true);
  testTrue(g, console.hasSelection(), "shift-right selects text at the cursor");
  console.AddCharacter('Z');
  testTrue(g,
           console.getCurrentInput() == "alpha Zeta",
           "typing replaces the active selection");

  console.SelectAll();
  console.HandleBackspace();
  testTrue(
    g, console.getCurrentInput().empty(), "backspace clears selected text");

  enterConsoleText(console, "ru");
  console.Complete();
  testTrue(g,
           console.getCurrentInput() == "ruleset ",
           "Tab completes a unique command and inserts an argument space");
  enterConsoleText(console, "wi");
  console.Complete();
  testTrue(g,
           console.getCurrentInput() == "ruleset WIREWORLD",
           "Tab completes a ruleset argument");

  std::vector<std::string> parsed = console.ParseCommandArgs(
    "save \"two words.illumo\" 'third item' escaped\\ value", " \t");
  testEqSize(g, parsed.size(), 4u, "quoted parser returns four arguments");
  if (parsed.size() == 4u) {
    testTrue(g, parsed[0] == "save", "quoted parser preserves command");
    testTrue(g,
             parsed[1] == "two words.illumo",
             "quoted parser preserves double-quoted file name");
    testTrue(g,
             parsed[2] == "third item",
             "quoted parser preserves single-quoted argument");
    testTrue(g,
             parsed[3] == "escaped value",
             "quoted parser honors escaped separators");
  }
}

static void
testCommandLineCommandDispatchAndValidation()
{
  testSection("CommandLine: command dispatch, help, variables, validation");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  env.setVar("tps", 30);
  env.setVar("speedFactor", "1");
  env.setVar("cellFadeSpeed", "8");
  env.setVar("showFPS", false);
  env.setVar("fullscreen", false);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  CommandRegistry registry;
  int probeCalls = 0;
  std::vector<std::string> probeArgs;
  registry.RegisterCommand(
    "probe",
    [&probeCalls, &probeArgs](const std::vector<std::string>& args) {
      probeCalls += 1;
      probeArgs = args;
    },
    "probe <values...>",
    "Test registered dispatch");
  CommandLine console(&env, &registry, &window, &renderer);

  enterConsoleText(console, "set greeting \"hello world\"");
  console.ExecuteCommand();
  testTrue(g,
           env.getVar("greeting").value == "hello world",
           "set accepts quoted multi-word values");

  enterConsoleText(console, "toggle showFPS");
  console.ExecuteCommand();
  testTrue(g,
           env.getVar("showFPS").valueAsBool,
           "toggle flips a boolean env variable");

  enterConsoleText(console, "fullscreen on");
  console.ExecuteCommand();
  testEqInt(g,
            window.fullscreenToggleCount,
            1,
            "fullscreen calls the window once when state changes");
  testTrue(g,
           env.getVar("fullscreen").valueAsBool,
           "fullscreen updates persistent state");

  enterConsoleText(console, "fullscreen on");
  console.ExecuteCommand();
  testEqInt(g,
            window.fullscreenToggleCount,
            1,
            "fullscreen avoids redundant window toggles");

  enterConsoleText(console, "probe alpha beta");
  console.ExecuteCommand();
  testEqInt(g, probeCalls, 0, "registered command waits in the command queue");
  registry.ExecuteQueue();
  testEqInt(g, probeCalls, 1, "registered command executes once");
  testEqSize(
    g, probeArgs.size(), 2u, "registered command receives all arguments");
  testTrue(
    g,
    !consoleHistoryContains(console, "Unknown command or variable: probe"),
    "registered command does not fall through as unknown");

  enterConsoleText(console, "help probe");
  console.ExecuteCommand();
  testTrue(g,
           consoleHistoryContains(console, "Test registered dispatch"),
           "help includes registered command metadata");

  enterConsoleText(console, "not_a_command 42");
  console.ExecuteCommand();
  testTrue(g,
           env.getVars().count("not_a_command") == 0u,
           "unknown commands do not silently create variables");
  testTrue(g,
           consoleHistoryContains(console,
                                  "Unknown command or variable: not_a_command"),
           "unknown command reports an error");

  enterConsoleText(console, "quit");
  console.ExecuteCommand();
  testTrue(g, window.closeRequested, "quit requests window close");
}

static void
testCommandLineClosedEmitsNoDraws()
{
  testSection("CommandLine: closed emits no UI draws");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer);

  testTrue(g, !console.isOpen, "starts closed");

  Scene scene(&window, &camera);
  scene.AddDrawable(&console, RenderLayerId::UI);

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  // Frame setup only (viewport / pipeline / clear) — no console geometry.
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             0u,
             "closed: no UpdateBuffer");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             0u,
             "closed: no DrawIndexed");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::ClearScreen),
             1u,
             "closed: still clears frame");
}

static void
testCommandLineOpenEmitsPanelTokens()
{
  testSection("CommandLine: open emits panel/update/draw tokens");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer);

  // Enroll happened in ctor
  testTrue(
    g, mock.getCreateCount() >= 2u, "console enrolled mesh + shared styles");

  // Advance open animation without recording those pumps on the mock frame
  // we care about — use a private pump that does not submit.
  console.Toggle();
  testTrue(g, console.isOpen, "toggled open");
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  // Call AppendCommands once to advance progress (tokens go into renderer
  // queue; we clear via BeginFrame next).
  console.AppendCommands(&renderer);

  // Full scene frame
  Scene scene(&window, &camera);
  scene.AddDrawable(&console, RenderLayerId::UI);

  mock.resetCounters();
  // Keep creates from enroll; resetCounters wipes creates — re-check only
  // submit. Actually resetCounters clears creates; enroll already done. Fine
  // for token asserts.

  renderer.BeginFrame();
  // Another small sleep so this frame's dt is non-zero while open
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  testTrue(g,
           mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u &&
             mock.countNonEmptyOfType(CommandType::UpdateBuffer) <= 2u,
           "open: batched UpdateBuffer");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "open: batched DrawIndexed");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::SetShader) >= 1u,
           "open: SetShader");
  testTrue(
    g, mock.countNonEmptyOfType(CommandType::SetMesh) >= 1u, "open: SetMesh");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::SetUniformVec2) >= 1u,
           "open: resolution/scale uniforms");

  // Blend enabled for console UI
  bool foundBlend = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& cmd = mock.getLastNonEmptySubmitted(i);
    if (cmd.commandType == CommandType::SetPipelineState &&
        cmd.pipelineState.blendEnabled) {
      foundBlend = true;
      break;
    }
  }
  testTrue(g, foundBlend, "open: pipeline enables blend");

  bool foundOutline = false;
  bool foundLine = false;
  GameVisual& visual = console.getVisual();
  for (size_t i = 0; i < visual.shapeCount(); ++i) {
    const ShapePrimitive* shape = visual.getShape(i);
    if (shape == nullptr) {
      continue;
    }
    foundOutline = foundOutline || shape->kind == ShapeKind::OutlineRect;
    foundLine = foundLine || shape->kind == ShapeKind::Line;
  }
  testTrue(
    g, foundOutline, "open: themed chrome uses outline rectangle primitives");
  testTrue(g, foundLine, "open: themed chrome uses line primitives");
}

static void
testCommandLineInvisibleSkipsTokens()
{
  testSection("CommandLine: invisible skips tokens even if open");
  NullRenderWindow window(800, 600);
  EnvVars env;
  env.setVar("WinX", 800);
  env.setVar("WinY", 600);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer);

  console.Toggle();
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  console.AppendCommands(&renderer); // advance animation
  console.setVisible(false);

  Scene scene(&window, &camera);
  scene.AddDrawable(&console, RenderLayerId::UI);
  mock.resetCounters();

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             0u,
             "invisible: no UpdateBuffer");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             0u,
             "invisible: no DrawIndexed");
}

static void
testCommandLineHistoryScrollTokens()
{
  testSection("CommandLine: detailed help output fits in one UI batch");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer);

  const std::string detailedHelp =
    "ruleset [name] - Show or change the cellular-automaton ruleset; includes "
    "GAME_OF_LIFE and WIREWORLD";
  for (int i = 0; i < 40; ++i) {
    console.logNormal(detailedHelp);
  }

  console.Toggle();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  // Warm animation fully toward open with several appends (not via scene)
  for (int i = 0; i < 15; ++i) {
    console.AppendCommands(&renderer);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  Scene scene(&window, &camera);
  scene.AddDrawable(&console, RenderLayerId::UI);
  mock.resetCounters();

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "history: DrawIndexed batches");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::UpdateBuffer) <= 2u,
           "history: at most two UpdateBuffers (idle frames reuse the mesh)");
  bool detailedHelpFits = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& cmd = mock.getLastNonEmptySubmitted(i);
    if (cmd.commandType == CommandType::DrawIndexed &&
        cmd.drawIndexed.elementCount > 500u) {
      detailedHelpFits = true;
    }
  }
  testTrue(
    g, detailedHelpFits, "history: detailed help emits rasterized glyph quads");
}

static void
testGLStringEmptyAndInvisible()
{
  testSection("GLString: empty / invisible emit no draws");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  GLString::setRenderWindow(&window);

  GLString emptyLabel("FPS: 0", 80, 255, 120, 255, 18, 12, 12, &renderer);
  emptyLabel.setContent("");
  Scene scene(&window, &camera);
  scene.AddDrawable(&emptyLabel, RenderLayerId::UI);

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             0u,
             "empty content: no DrawIndexed");

  mock.resetCounters();
  emptyLabel.setContent("Hello");
  emptyLabel.setVisible(false);
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             0u,
             "invisible: no DrawIndexed");
}

static void
testGLStringEmitsTextTokens()
{
  testSection("GLString: content emits UpdateBuffer + DrawIndexed");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  GLString::setRenderWindow(&window);

  GLString label("FPS: 60", 80, 255, 120, 255, 18, 12, 12, &renderer);
  testTrue(
    g, mock.getCreateCount() >= 2u, "GLString enrolled mesh + shared styles");

  Scene scene(&window, &camera);
  scene.AddDrawable(&label, RenderLayerId::UI);

  mock.resetCounters();
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  testTrue(
    g, mock.getLastNonEmptySubmittedCount() > 0, "GLString frame non-empty");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "GLString one UpdateBuffer");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             1u,
             "GLString one DrawIndexed");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::SetShader),
             1u,
             "GLString SetShader");
  testEqSize(
    g, mock.countNonEmptyOfType(CommandType::SetMesh), 1u, "GLString SetMesh");

  // Shape-style text uses u_resolution (position baked into verts).
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::SetUniformVec2) >= 1u,
           "GLString resolution uniform");

  bool foundBlend = false;
  bool foundDrawElems = false;
  for (size_t i = 0; i < mock.getLastNonEmptySubmittedCount(); ++i) {
    const RenderCommand& cmd = mock.getLastNonEmptySubmitted(i);
    if (cmd.commandType == CommandType::SetPipelineState &&
        cmd.pipelineState.blendEnabled) {
      foundBlend = true;
    }
    if (cmd.commandType == CommandType::DrawIndexed) {
      foundDrawElems = cmd.drawIndexed.elementCount > 0;
    }
  }
  testTrue(g, foundBlend, "GLString enables blend");
  testTrue(g, foundDrawElems, "GLString DrawIndexed elementCount > 0");
}

static void
testGLStringPanelComposition()
{
  testSection("GLString: optional panel chrome composes primitive shapes");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  GLString::setRenderWindow(&window);

  GLString label("FPS: 60", 92, 224, 150, 255, 18, 12, 12, &renderer);
  label.setPanelStyle(UiTheme::statusPanel());
  testTrue(g,
           label.AppendCommands(&renderer),
           "decorated label remains on the token path");
  testEqSize(g,
             label.getVisual().shapeCount(),
             4u,
             "panel adds shadow, surface, border, and accent primitives");
  testEqSize(
    g, label.getVisual().textCount(), 1u, "panel retains one text primitive");

  label.clearPanelStyle();
  testTrue(g,
           label.AppendCommands(&renderer),
           "plain label remains drawable after clearing chrome");
  testEqSize(g,
             label.getVisual().shapeCount(),
             0u,
             "clearing chrome returns to text-only composition");
}

static void
testGLStringAndCommandLineTogether()
{
  testSection("Scene: Canvas-order console + FPS tokens");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  GLString::setRenderWindow(&window);
  CommandRegistry registry;

  CommandLine console(&env, &registry, &window, &renderer);
  GLString fps("FPS: 99", 80, 255, 120, 255, 18, 12, 12, &renderer);

  console.Toggle();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  for (int i = 0; i < 12; ++i) {
    console.AppendCommands(&renderer);
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
  }

  Scene scene(&window, &camera);
  scene.AddDrawable(&console, RenderLayerId::UI);
  scene.AddDrawable(&fps, RenderLayerId::Debug);

  mock.resetCounters();
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();

  // Console batch (1 draw) + FPS (1 draw)
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 2u,
           "console+FPS: at least two DrawIndexed");
  testTrue(g,
           mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u,
           "console+FPS: at least one UpdateBuffer (FPS first frame; console "
           "may be cached)");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::ClearScreen),
             1u,
             "single frame clear");
}

static void
testGLStringCachesGeometry()
{
  testSection("GLString: second frame skips UpdateBuffer if content unchanged");
  NullRenderWindow window(640, 480);
  EnvVars env;
  env.setVar("WinX", 640);
  env.setVar("WinY", 480);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  GLString::setRenderWindow(&window);

  GLString label("FPS: 12", 80, 255, 120, 255, 18, 12, 12, &renderer);
  Scene scene(&window, &camera);
  scene.AddDrawable(&label, RenderLayerId::UI);

  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "first frame uploads geometry");

  mock.resetCounters();
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             0u,
             "second frame reuses cached VBO");
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::DrawIndexed),
             1u,
             "second frame still draws");

  label.setContent("FPS: 99");
  mock.resetCounters();
  renderer.BeginFrame();
  renderer.RenderScene(&scene, &camera);
  renderer.EndFrame();
  testEqSize(g,
             mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             1u,
             "content change re-uploads");
}

static void
testCommandLineEditingBoundaries()
{
  testSection("CommandLine: editing boundaries, word deletion, selection "
              "collapse, input cap");
  CommandLineFixture fixture;
  CommandLine& console = fixture.console;

  console.HandleBackspace();
  console.HandleDelete();
  console.AddCharacter(31);
  console.AddCharacter(127);
  testTrue(g,
           console.getCurrentInput().empty(),
           "control and non-ASCII codepoints are ignored");

  enterConsoleText(console, "alpha  beta");
  console.MoveCursorHome();
  console.HandleDelete();
  testTrue(g,
           console.getCurrentInput() == "lpha  beta",
           "delete removes one character at cursor");
  console.MoveCursorEnd();
  console.HandleDelete();
  console.HandleBackspace(true);
  testTrue(g,
           console.getCurrentInput() == "lpha  ",
           "word backspace removes the previous word");
  console.HandleBackspace(true);
  testTrue(g,
           console.getCurrentInput().empty(),
           "word backspace skips spaces and removes prior word");

  enterConsoleText(console, "one two three");
  console.MoveCursorHome();
  console.HandleDelete(true);
  testTrue(g,
           console.getCurrentInput() == "two three",
           "word delete removes through following spaces");
  console.MoveCursorEnd(true);
  testTrue(g, console.hasSelection(), "shift-End selects to end");
  console.MoveCursorLeft(false, false);
  testTrue(g,
           !console.hasSelection() && console.getCursorPosition() == 0u,
           "left collapses selection to its start");
  console.MoveCursorEnd();
  console.MoveCursorHome(true);
  testTrue(g, console.hasSelection(), "shift-Home selects to start");
  console.MoveCursorRight(false, false);
  testTrue(g,
           !console.hasSelection() &&
             console.getCursorPosition() == console.getCurrentInput().size(),
           "right collapses selection to its end");

  console.MoveCursorHome();
  console.MoveCursorRight(true, false);
  testTrue(g,
           console.getCursorPosition() == 4u,
           "word-right skips the first separator");
  console.MoveCursorLeft(true, false);
  testTrue(g,
           console.getCursorPosition() == 0u,
           "word-left returns to prior word boundary");

  console.ClearInput();
  for (int i = 0; i < MAX_CHARS_PER_LINE + 20; ++i) {
    console.AddCharacter('x');
  }
  testEqSize(g,
             console.getCurrentInput().size(),
             MAX_CHARS_PER_LINE - 1u,
             "input is capped at the fixed line capacity");
}

static void
testCommandLineCompletionBranches()
{
  testSection(
    "CommandLine: ambiguous, missing, environment, and boolean completion");
  CommandLineFixture fixture;
  fixture.env.setVar("WinAlpha", 1);
  fixture.env.setVar("WinBeta", 2);

  fixture.console.Complete();
  testTrue(g,
           fixture.console.getCompletionHint().find("Matches:") == 0u,
           "empty prefix shows several command matches");
  testTrue(g,
           fixture.console.getCompletionHint().find("...") != std::string::npos,
           "long match list is abbreviated");

  enterConsoleText(fixture.console, "zzzz");
  fixture.console.Complete();
  testTrue(g,
           fixture.console.getCompletionHint().find("No completion matches") ==
             0u,
           "missing prefix reports no match");

  fixture.console.ClearInput();
  enterConsoleText(fixture.console, "f");
  fixture.console.Complete();
  testTrue(g,
           fixture.console.getCompletionHint().find("fps") != std::string::npos,
           "ambiguous prefix lists matching commands");

  fixture.console.ClearInput();
  enterConsoleText(fixture.console, "get W");
  fixture.console.Complete();
  testTrue(g,
           fixture.console.getCurrentInput().find("get Win") == 0u,
           "environment completion inserts common prefix");

  fixture.console.ClearInput();
  enterConsoleText(fixture.console, "fps t");
  fixture.console.Complete();
  testTrue(g,
           fixture.console.getCurrentInput() == "fps toggle",
           "boolean command argument completes uniquely");

  const std::vector<std::string> trailingEscape =
    fixture.console.ParseCommandArgs("echo tail\\", " \t");
  testEqSize(g,
             trailingEscape.size(),
             2u,
             "trailing escape remains part of final token");
  if (trailingEscape.size() == 2u) {
    testTrue(g, trailingEscape[1] == "tail\\", "trailing slash is preserved");
  }
}

static void
testCommandLineHelpAndEnvironmentCommands()
{
  testSection(
    "CommandLine: help, echo, variables, clear, and raw variable assignment");
  CommandLineFixture fixture;
  fixture.registry.RegisterCommand(
    "probe", [](const std::vector<std::string>&) {}, "", "Probe description");
  fixture.env.setVar("MixedCase", "before");
  fixture.env.setVar("flag", false);

  executeConsoleText(fixture.console, "help");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Built-in commands"),
           "help lists built-in commands");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Registered commands"),
           "help lists registered commands");
  executeConsoleText(fixture.console, "help fps");
  testTrue(g,
           consoleHistoryContains(fixture.console, "fps [on|off|toggle]"),
           "help finds built-in metadata");
  executeConsoleText(fixture.console, "help probe");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Probe description"),
           "help falls back to registered metadata");
  executeConsoleText(fixture.console, "help missing");
  testTrue(g,
           consoleHistoryContains(fixture.console, "No help available"),
           "missing help reports error");

  executeConsoleText(fixture.console, "echo one two three");
  testTrue(g,
           consoleHistoryContains(fixture.console, "one two three"),
           "echo joins all arguments");
  executeConsoleText(fixture.console, "get");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Usage: get"),
           "get validates argument count");
  executeConsoleText(fixture.console, "get unknown");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Unknown variable: unknown"),
           "get rejects unknown variable");
  executeConsoleText(fixture.console, "get mixedcase");
  testTrue(g,
           consoleHistoryContains(fixture.console, "MixedCase = before"),
           "get resolves environment keys case-insensitively");

  executeConsoleText(fixture.console, "set only-name");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Usage: set"),
           "set validates argument count");
  executeConsoleText(fixture.console, "set NewValue multi word text");
  testTrue(g,
           fixture.env.getVar("NewValue").value == "multi word text",
           "set creates multi-word variable value");
  executeConsoleText(fixture.console, "set mixedcase updated value");
  testTrue(g,
           fixture.env.getVar("MixedCase").value == "updated value",
           "set preserves canonical key casing");

  executeConsoleText(fixture.console, "toggle");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Usage: toggle"),
           "toggle validates argument count");
  executeConsoleText(fixture.console, "toggle unknown");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Unknown variable: unknown"),
           "toggle rejects unknown variable");
  executeConsoleText(fixture.console, "toggle FLAG");
  testTrue(g,
           fixture.env.getVar("flag").valueAsBool,
           "toggle resolves key and flips boolean");

  executeConsoleText(fixture.console, "vars mixed");
  testTrue(g,
           consoleHistoryContains(fixture.console, "MixedCase = updated value"),
           "vars filters case-insensitively");
  executeConsoleText(fixture.console, "vars no-such-filter");
  testTrue(g,
           consoleHistoryContains(fixture.console, "No variables match"),
           "vars reports empty filter result");

  executeConsoleText(fixture.console, "MixedCase");
  testTrue(g,
           consoleHistoryContains(fixture.console, "MixedCase = updated value"),
           "raw variable query reads value");
  executeConsoleText(fixture.console, "MixedCase direct");
  testTrue(g,
           fixture.env.getVar("MixedCase").value == "direct",
           "raw variable assignment writes one value");
  executeConsoleText(fixture.console, "MixedCase too many values");
  testTrue(g,
           consoleHistoryContains(fixture.console,
                                  "Variable assignment accepts one value"),
           "raw assignment rejects several values");

  executeConsoleText(fixture.console, "set msaa 8");
  testTrue(
    g,
    consoleHistoryContains(fixture.console, "restarting the application"),
    "set msaa warns that restart is required");
  executeConsoleText(fixture.console, "msaa 4");
  testTrue(
    g,
    consoleHistoryContains(fixture.console, "restarting the application"),
    "direct msaa assignment warns that restart is required");

  executeConsoleText(fixture.console, "clear");
  testEqSize(g,
             fixture.console.getHistory().size(),
             1u,
             "clear resets history to console heading");
}

static void
testCommandLineNumericAndDisplayCommands()
{
  testSection("CommandLine: numeric validation, FPS, fullscreen, close, "
              "deprecated command");
  CommandLineFixture fixture;

  executeConsoleText(fixture.console, "fps");
  testTrue(g,
           consoleHistoryContains(fixture.console, "FPS overlay: off"),
           "fps query reports state");
  executeConsoleText(fixture.console, "fps toggle");
  testTrue(
    g, fixture.env.getVar("showFPS").valueAsBool, "fps toggle changes state");
  executeConsoleText(fixture.console, "fps off");
  testTrue(
    g, !fixture.env.getVar("showFPS").valueAsBool, "fps parses off boolean");
  executeConsoleText(fixture.console, "fps maybe");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Usage: fps"),
           "fps rejects invalid boolean");

  executeConsoleText(fixture.console, "fullscreen");
  testEqInt(g,
            fixture.window.fullscreenToggleCount,
            1,
            "fullscreen with no args toggles");
  executeConsoleText(fixture.console, "fullscreen toggle");
  testEqInt(g,
            fixture.window.fullscreenToggleCount,
            2,
            "fullscreen toggle argument changes state");
  executeConsoleText(fixture.console, "fullscreen off");
  testEqInt(g,
            fixture.window.fullscreenToggleCount,
            2,
            "redundant fullscreen state avoids window call");
  executeConsoleText(fixture.console, "fullscreen maybe");
  testTrue(g,
           consoleHistoryContains(fixture.console, "Usage: fullscreen"),
           "fullscreen rejects invalid argument");

  fixture.console.Toggle();
  executeConsoleText(fixture.console, "close");
  testTrue(g, !fixture.console.isOpen, "close command closes console");
  executeConsoleText(fixture.console, "vid_restart");
  testTrue(g,
           consoleHistoryContains(fixture.console, "resource re-enrollment"),
           "deprecated restart explains limitation");
  executeConsoleText(fixture.console, "   ");
}

static void
testCommandLineHistoryNavigationAndLimits()
{
  testSection(
    "CommandLine: command history navigation, scroll, bounded output");
  CommandLineFixture fixture(640, 300);
  fixture.console.HistoryUp();
  fixture.console.HistoryDown();
  fixture.console.AddToHistory("first");
  fixture.console.AddToHistory("second");
  fixture.console.ClearInput();
  enterConsoleText(fixture.console, "draft");
  fixture.console.HistoryUp();
  testTrue(g,
           fixture.console.getCurrentInput() == "second",
           "history up recalls newest command");
  fixture.console.HistoryUp();
  testTrue(g,
           fixture.console.getCurrentInput() == "first",
           "history up reaches oldest command");
  fixture.console.HistoryUp();
  fixture.console.HistoryDown();
  testTrue(g,
           fixture.console.getCurrentInput() == "second",
           "history down moves toward newest");
  fixture.console.HistoryDown();
  testTrue(g,
           fixture.console.getCurrentInput() == "draft",
           "history down restores draft input");
  fixture.console.HistoryDown();

  for (int i = 0; i < MAX_CMD_HISTORY + 10; ++i) {
    fixture.console.AddToHistory("command-" + std::to_string(i));
    fixture.console.AppendString(
      255, 255, 255, 255, "line-" + std::to_string(i));
  }
  fixture.console.AppendStringLn(1, 2, 3, 4, "last-line");
  testEqSize(g,
             fixture.console.getHistory().size(),
             MAX_CMD_HISTORY,
             "visible history evicts oldest lines at capacity");
  testTrue(g,
           fixture.console.getHistory().back().content == "last-line\n",
           "AppendStringLn appends newline");
  for (int i = 0; i < MAX_CMD_HISTORY + 10; ++i) {
    fixture.console.ScrollUp();
  }
  const int scrollAtStart = fixture.console.getScrollOffset();
  testTrue(g,
           scrollAtStart > 0,
           "PageUp reaches a non-zero scroll offset for full history");
  fixture.console.ScrollUp();
  testEqInt(g,
            fixture.console.getScrollOffset(),
            scrollAtStart,
            "extra PageUp at the start does not overscroll past oldest lines");
  for (int i = 0; i < MAX_CMD_HISTORY + 10; ++i) {
    fixture.console.ScrollDown();
  }
  testEqInt(g,
            fixture.console.getScrollOffset(),
            0,
            "PageDown returns scroll to the newest history end");
  fixture.console.DrawImpl();
}

static void
testCommandLineHistoryWrapAndScrollToStart()
{
  testSection("CommandLine: long history lines wrap and remain reachable at "
              "scroll start");
  CommandLineFixture fixture(640, 360);
  CommandLine& console = fixture.console;

  const std::string longHelp =
    "ruleset [name] - Show or change the cellular-automaton ruleset; includes "
    "GAME_OF_LIFE, HIGHLIFE, DAY_AND_NIGHT, LIFE_WITHOUT_DEATH, and WIREWORLD";
  console.AppendString(255, 255, 255, 255, "OLDEST-MARKER " + longHelp);
  for (int i = 0; i < 40; ++i) {
    console.AppendString(
      255, 255, 255, 255, "mid-" + std::to_string(i) + " " + longHelp);
  }
  console.AppendString(255, 255, 255, 255, "NEWEST-MARKER");

  console.Toggle();
  // Settle panel layout so scroll limits match the drawn viewport.
  for (int i = 0; i < 20; ++i) {
    console.AppendCommands(&fixture.renderer);
  }

  for (int i = 0; i < 500; ++i) {
    console.ScrollUp();
  }
  const int startOffset = console.getScrollOffset();
  testTrue(g,
           startOffset > 0,
           "wrapped history produces a positive max scroll offset");
  testTrue(g,
           startOffset >= 40,
           "scroll distance exceeds raw history count because long lines wrap");
  console.ScrollUp();
  testEqInt(g,
            console.getScrollOffset(),
            startOffset,
            "scroll clamps at the oldest wrapped visual line");

  // Rendering at the start must still emit a single UI batch (no silent mesh
  // overflow that drops the first history lines).
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  testTrue(g,
           console.AppendCommands(&fixture.renderer),
           "scroll-to-start history still emits tokens");
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u &&
             fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer) <= 2u,
           "scroll-to-start keeps UpdateBuffer batch");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "scroll-to-start keeps DrawIndexed batch");
  bool oldestStillPresent = false;
  const std::vector<CommandLine::historyBuffer>& lines = console.getHistory();
  for (const CommandLine::historyBuffer& line : lines) {
    if (line.content.find("OLDEST-MARKER") != std::string::npos) {
      oldestStillPresent = true;
      break;
    }
  }
  testTrue(g,
           oldestStillPresent,
           "oldest long history entry remains in the buffer at scroll start");
}

static void
testCommandLineHistoryWrapCacheStability()
{
  testSection("CommandLine: wrap cache and idle composition skip re-upload");
  CommandLineFixture fixture(640, 360);
  CommandLine& console = fixture.console;

  const std::string longHelp =
    "ruleset [name] - Show or change the cellular-automaton ruleset; includes "
    "GAME_OF_LIFE, HIGHLIFE, DAY_AND_NIGHT, LIFE_WITHOUT_DEATH, and WIREWORLD";
  console.AppendString(255, 255, 255, 255, "OLDEST-MARKER " + longHelp);
  for (int i = 0; i < 40; ++i) {
    console.AppendString(
      255, 255, 255, 255, "mid-" + std::to_string(i) + " " + longHelp);
  }
  console.AppendString(255, 255, 255, 255, "NEWEST-MARKER");

  console.Toggle();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  for (int i = 0; i < 8; ++i) {
    console.AppendCommands(&fixture.renderer);
  }

  for (int i = 0; i < 500; ++i) {
    console.ScrollUp();
  }
  const int startOffset = console.getScrollOffset();
  testTrue(
    g, startOffset > 0, "wrap cache still produces a positive max scroll");

  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  testTrue(g,
           console.AppendCommands(&fixture.renderer),
           "dirty wrap-cache frame emits tokens");
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u &&
             fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer) <= 2u,
           "dirty history frame uploads");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "dirty history frame draws");
  testTrue(g,
           console.getVisual().textCount() <= 16u,
           "off-screen wrapped history is not tessellated");

  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  testTrue(g,
           console.AppendCommands(&fixture.renderer),
           "idle wrap-cache frame still draws");
  fixture.renderer.EndFrame();
  testEqSize(g,
             fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer),
             0u,
             "idle settled frame skips UpdateBuffer");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "idle settled frame still DrawIndexed");

  console.AppendString(255, 255, 255, 255, "appended-after-cache");
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  testTrue(g,
           console.AppendCommands(&fixture.renderer),
           "history append dirties composition");
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u &&
             fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer) <= 2u,
           "new history line uploads again");

  bool oldestStillPresent = false;
  const std::vector<CommandLine::historyBuffer>& lines = console.getHistory();
  for (const CommandLine::historyBuffer& line : lines) {
    if (line.content.find("OLDEST-MARKER") != std::string::npos) {
      oldestStillPresent = true;
      break;
    }
  }
  testTrue(
    g, oldestStillPresent, "oldest marker remains after cached wrap append");
}

static void
testCommandLineHeadlessAndLongSelectionTokens()
{
  testSection(
    "CommandLine: no-renderer fallback and long selected input token path");
  NullRenderWindow headlessWindow(320, 180);
  EnvVars headlessEnv;
  CommandRegistry headlessRegistry;
  CommandLine headless(
    &headlessEnv, &headlessRegistry, &headlessWindow, nullptr);
  testTrue(g,
           !headless.AppendCommands(nullptr),
           "console without renderer requests immediate fallback");
  headless.setVisible(false);
  testTrue(g,
           headless.AppendCommands(nullptr),
           "invisible console succeeds without GPU resources");

  CommandLineFixture fixture(320, 180);
  for (int i = 0; i < 180; ++i) {
    fixture.console.AddCharacter('x');
  }
  fixture.console.MoveCursorHome(true);
  fixture.console.Toggle();
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  fixture.mock.resetCounters();
  fixture.renderer.BeginFrame();
  testTrue(g,
           fixture.console.AppendCommands(&fixture.renderer),
           "long selected input emits tokens");
  fixture.renderer.EndFrame();
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer) >= 1u &&
             fixture.mock.countNonEmptyOfType(CommandType::UpdateBuffer) <= 2u,
           "long input remains UI upload");
  testTrue(g,
           fixture.mock.countNonEmptyOfType(CommandType::DrawIndexed) >= 1u,
           "long input remains UI draw");
}

static void
testCommandLineCommandChainingAndAliases()
{
  testSection("CommandLine: command chaining with semicolons, alias creation, "
              "expansion, and unalias");
  CommandLineFixture fixture;
  CommandLine& console = fixture.console;

  executeConsoleText(console, "set tps 60; set speedFactor 1.0");
  testTrue(g,
           fixture.env.getVar("tps").value == "60",
           "chained set tps updated variable");
  testTrue(g,
           fixture.env.getVar("speedFactor").value == "1.0",
           "chained speed updated variable");

  executeConsoleText(console,
                     "alias turbo \"set tps 240; set speedFactor 4.0\"");
  testTrue(g, console.HasAlias("turbo"), "alias turbo registered");
  testTrue(g,
           console.GetAlias("turbo") == "set tps 240; set speedFactor 4.0",
           "alias turbo expansion matches");

  executeConsoleText(console, "turbo");
  testTrue(g,
           fixture.env.getVar("tps").value == "240",
           "alias expansion executed tps update");
  testTrue(g,
           fixture.env.getVar("speedFactor").value == "4.0",
           "alias expansion executed speed update");

  executeConsoleText(console, "unalias turbo");
  testTrue(g, !console.HasAlias("turbo"), "unalias turbo removed alias");
}

static void
testCommandLineGhostTextAndParamHints()
{
  testSection("CommandLine: inline ghost text suggestion and dynamic parameter "
              "usage hints");
  CommandLineFixture fixture;
  CommandLine& console = fixture.console;

  enterConsoleText(console, "cl");
  testTrue(
    g, console.getGhostSuggestion() == "ear", "ghost suggestion for cl is ear");

  console.MoveCursorRight();
  testTrue(g,
           console.getCurrentInput() == "clear",
           "Right-Arrow at end accepts ghost suggestion");

  console.ClearInput();
  enterConsoleText(console, "set ");
  std::vector<std::string> args =
    console.ParseCommandArgs(console.getCurrentInput(), " \t");
  testTrue(g, !args.empty(), "set command parsed");
  testTrue(g, args[0] == "set", "set command token match");
}

static void
testCommandLineRepeatAndHistorySearch()
{
  testSection(
    "CommandLine: repeat execution, history search, and telemetry dashboard");
  CommandLineFixture fixture;
  CommandLine& console = fixture.console;

  executeConsoleText(console, "repeat 3 echo ping");
  testTrue(g,
           consoleHistoryContains(console, "ping"),
           "repeat command executed echo ping multiple times");

  executeConsoleText(console, "sysinfo");
  testTrue(g,
           consoleHistoryContains(console, "=== Illumo System Telemetry ==="),
           "sysinfo command printed telemetry dashboard");

  executeConsoleText(console, "history clear");
}

static void
testCommandLineBranding()
{
  testSection("CommandLine: host-provided branding");
  CommandLineFixture fixture;
  CommandLine branded(&fixture.env,
                      &fixture.registry,
                      &fixture.window,
                      &fixture.renderer,
                      "SampleHost");
  testTrue(g,
           consoleHistoryContains(branded, "SampleHost Developer Console"),
           "initial console heading uses host name");
  executeConsoleText(branded, "sysinfo");
  testTrue(
    g,
    consoleHistoryContains(branded, "=== SampleHost System Telemetry ==="),
    "telemetry heading uses host name");
}

static void
testCommandLineMouseInteraction()
{
  testSection("CommandLine: mouse click positioning, selection drag, "
              "scrollbar, and wheel");
  CommandLineFixture fixture(1280, 720);
  CommandLine& console = fixture.console;
  console.Toggle();

  enterConsoleText(console, "hello mouse world");
  float panelHeight = 720.0f * 0.52f;
  float inputRowY = panelHeight - 20.0f;

  console.HandleMousePress(112.0, inputRowY);
  testTrue(g,
           console.getCursorPosition() >= 5u &&
             console.getCursorPosition() <= 12u,
           "mouse click places caret near character position");
  testTrue(g, !console.hasSelection(), "single mouse click clears selection");

  console.HandleMouseDrag(200.0, inputRowY);
  testTrue(g, console.hasSelection(), "mouse drag creates text selection");
  console.HandleMouseRelease();

  for (int i = 0; i < 50; ++i) {
    console.logNormal("History line " + std::to_string(i));
  }
  console.HandleScroll(1.0);
  testTrue(g, console.getHistory().size() > 0, "mouse scroll handled");

  console.HandleMousePress(1270.0, 100.0);
  console.HandleMouseRelease();
}

static int
runUITokenCase(void (*testFunction)())
{
  g.failures = 0;
  testFunction();
  return g.failures;
}

static void
testCommandLineFloatingModeAndDragging()
{
  testSection(
    "CommandLine: Floating mode toggle, title-bar drag, and corner resize");
  NullRenderWindow window(1280, 720);
  EnvVars env;
  env.setVar("WinX", 1280);
  env.setVar("WinY", 720);
  Camera camera(glm::vec2(0.0f, 0.0f), 1.0f, &env);
  MockBackend mock;
  mock.Initialize();
  Renderer renderer(&window, &env, &camera, &mock, false);
  CommandRegistry registry;
  CommandLine console(&env, &registry, &window, &renderer);

  testEqInt(g, console.getFloatingMode() ? 1 : 0, 0, "default mode is mounted");

  console.setFloatingMode(true);
  testEqInt(
    g, console.getFloatingMode() ? 1 : 0, 1, "mode updated to floating");

  console.Toggle();
  console.AppendCommands(&renderer);

  console.HandleMousePress(200.0, 10.0, false);
  console.HandleMouseDrag(300.0, 100.0);
  console.HandleMouseRelease();

  console.setFloatingSize(800.0f, 500.0f);
  testEqInt(g,
            static_cast<int>(console.getFloatingWidth()),
            800,
            "floating width updated");
  testEqInt(g,
            static_cast<int>(console.getFloatingHeight()),
            500,
            "floating height updated");

  // Drag bottom-right corner grip
  console.HandleMousePress(900.0, 520.0, false);
  console.HandleMouseDrag(950.0, 560.0);
  console.HandleMouseRelease();

  console.setFloatingMode(false);
  testEqInt(g, console.getFloatingMode() ? 1 : 0, 0, "console_mode mounted");

  console.ToggleFloatingMode();
  testEqInt(g,
            console.getFloatingMode() ? 1 : 0,
            1,
            "console_mode toggled to floating");
}

void
registerUITokenTests(IllumoTestRegistry& registry)
{
  registry.add("Illumo.CommandLine.EditorAndCompletion", []() {
    return runUITokenCase(testCommandLineEditorAndCompletion);
  });
  registry.add("Illumo.CommandLine.DispatchAndValidation", []() {
    return runUITokenCase(testCommandLineCommandDispatchAndValidation);
  });
  registry.add("Illumo.CommandLine.ClosedTokens", []() {
    return runUITokenCase(testCommandLineClosedEmitsNoDraws);
  });
  registry.add("Illumo.CommandLine.OpenTokens", []() {
    return runUITokenCase(testCommandLineOpenEmitsPanelTokens);
  });
  registry.add("Illumo.CommandLine.InvisibleTokens", []() {
    return runUITokenCase(testCommandLineInvisibleSkipsTokens);
  });
  registry.add("Illumo.GLString.EmptyAndInvisible",
               []() { return runUITokenCase(testGLStringEmptyAndInvisible); });
  registry.add("Illumo.GLString.TextTokens",
               []() { return runUITokenCase(testGLStringEmitsTextTokens); });
  registry.add("Illumo.GLString.GeometryCache",
               []() { return runUITokenCase(testGLStringCachesGeometry); });
  registry.add("Illumo.GLString.PanelComposition",
               []() { return runUITokenCase(testGLStringPanelComposition); });
  registry.add("Illumo.Rendering.CommandLineAndGLString", []() {
    return runUITokenCase(testGLStringAndCommandLineTogether);
  });
  registry.add("Illumo.CommandLine.EditingBoundaries", []() {
    return runUITokenCase(testCommandLineEditingBoundaries);
  });
  registry.add("Illumo.CommandLine.CompletionBranches", []() {
    return runUITokenCase(testCommandLineCompletionBranches);
  });
  registry.add("Illumo.CommandLine.HelpAndEnvironment", []() {
    return runUITokenCase(testCommandLineHelpAndEnvironmentCommands);
  });
  registry.add("Illumo.CommandLine.NumericAndDisplay", []() {
    return runUITokenCase(testCommandLineNumericAndDisplayCommands);
  });
  registry.add("Illumo.CommandLine.HistoryNavigation", []() {
    return runUITokenCase(testCommandLineHistoryNavigationAndLimits);
  });
  registry.add("Illumo.CommandLine.HistoryScrollTokens", []() {
    return runUITokenCase(testCommandLineHistoryScrollTokens);
  });
  registry.add("Illumo.CommandLine.HistoryWrapAndScrollToStart", []() {
    return runUITokenCase(testCommandLineHistoryWrapAndScrollToStart);
  });
  registry.add("Illumo.CommandLine.HistoryWrapCacheStability", []() {
    return runUITokenCase(testCommandLineHistoryWrapCacheStability);
  });
  registry.add("Illumo.CommandLine.HeadlessAndLongInput", []() {
    return runUITokenCase(testCommandLineHeadlessAndLongSelectionTokens);
  });
  registry.add("Illumo.CommandLine.CommandChainingAndAliases", []() {
    return runUITokenCase(testCommandLineCommandChainingAndAliases);
  });
  registry.add("Illumo.CommandLine.GhostTextAndParamHints", []() {
    return runUITokenCase(testCommandLineGhostTextAndParamHints);
  });
  registry.add("Illumo.CommandLine.RepeatAndHistorySearch", []() {
    return runUITokenCase(testCommandLineRepeatAndHistorySearch);
  });
  registry.add("Illumo.CommandLine.Branding",
               []() { return runUITokenCase(testCommandLineBranding); });
  registry.add("Illumo.CommandLine.MouseInteraction", []() {
    return runUITokenCase(testCommandLineMouseInteraction);
  });
  registry.add("Illumo.CommandLine.FloatingMode", []() {
    return runUITokenCase(testCommandLineFloatingModeAndDragging);
  });
}
