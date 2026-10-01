#include <Illumo/Content/ProgramScene.h>
#include <Illumo/Rendering/Primitives/SkyboxVisual.h>
#include <algorithm>

ProgramScene::ProgramScene() = default;

ProgramScene::~ProgramScene() = default;

void
ProgramScene::command(const std::string& name,
                      CommandFn function,
                      const std::string& usage,
                      const std::string& description,
                      const std::vector<std::string>& completions)
{
  if (m_context == nullptr || m_context->commandRegistry == nullptr) {
    return;
  }
  m_context->commandRegistry->RegisterCommand(
    name, std::move(function), usage, description, completions);
  if (std::find(m_commands.begin(), m_commands.end(), name) ==
      m_commands.end()) {
    m_commands.push_back(name);
  }
}

void
ProgramScene::withdrawCommands()
{
  if (m_context != nullptr && m_context->commandRegistry != nullptr) {
    for (const std::string& name : m_commands) {
      m_context->commandRegistry->UnregisterCommand(name);
    }
  }
  m_commands.clear();
}

void
ProgramScene::dispatchContent(DrawList& frame)
{
  if (!m_content) {
    return;
  }
  m_content->update();
  if (SkyboxVisual* sky = m_content->skybox()) {
    frame.AddDrawable(sky, RenderLayerId::World);
  }
  frame.AddDrawable(&m_content->drawable(), RenderLayerId::World);
}
