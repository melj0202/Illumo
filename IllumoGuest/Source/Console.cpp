#include <IllumoGuest/Console.h>

void
GuestConsole::add(std::string name,
                  std::string usage,
                  std::string description,
                  std::vector<std::string> completions)
{
  GuestConsoleRequest request;
  request.action = GuestConsoleAction::Register;
  request.name = std::move(name);
  request.usage = std::move(usage);
  request.description = std::move(description);
  request.completions = std::move(completions);
  GuestWireWriter bytes;
  request.write(bytes);
  GuestConsoleRequest checked;
  if (!GuestConsoleRequest::read(bytes.data(), checked)) {
    throw std::invalid_argument("Invalid console command registration");
  }
  m_outgoing.push_back(std::move(request));
}
void
GuestConsole::remove(std::string name)
{
  GuestConsoleRequest request;
  request.action = GuestConsoleAction::Unregister;
  request.name = std::move(name);
  GuestWireWriter bytes;
  request.write(bytes);
  GuestConsoleRequest checked;
  if (!GuestConsoleRequest::read(bytes.data(), checked)) {
    throw std::invalid_argument("Invalid console command name");
  }
  m_outgoing.push_back(std::move(request));
}
void
GuestConsole::log(std::uint32_t level, std::string text)
{
  GuestConsoleRequest request;
  request.action = GuestConsoleAction::Log;
  request.level = level;
  request.text = std::move(text);
  GuestWireWriter bytes;
  request.write(bytes);
  GuestConsoleRequest checked;
  if (!GuestConsoleRequest::read(bytes.data(), checked)) {
    throw std::invalid_argument("Invalid console log text");
  }
  m_outgoing.push_back(std::move(request));
}
bool
GuestConsole::enqueue(GuestConsoleRequest request)
{
  GuestWireWriter writer;
  request.write(writer);
  const std::uint64_t id =
    m_services.enqueue(GuestService::Console, writer.take());
  if (id == 0) {
    return false;
  }
  if (request.action == GuestConsoleAction::Listen) {
    m_listen = id;
  } else {
    m_requests.push_back(id);
  }
  return true;
}
void
GuestConsole::pump()
{
  for (std::deque<std::uint64_t>::iterator it = m_requests.begin();
       it != m_requests.end();) {
    GuestServiceRecord result;
    if (!m_services.take(*it, result)) {
      ++it;
      continue;
    }
    if (result.status != GuestServiceStatus::Complete) {
      m_error = "Console request was rejected";
    }
    it = m_requests.erase(it);
  }
  if (m_listen != 0) {
    GuestServiceRecord result;
    if (m_services.take(m_listen, result)) {
      m_listen = 0;
      m_listening = false;
      if (result.status == GuestServiceStatus::Complete) {
        GuestConsoleRequest invocation;
        if (!GuestConsoleRequest::read(result.payload, invocation) ||
            invocation.action != GuestConsoleAction::Listen ||
            invocation.name.empty()) {
          throw std::runtime_error("Invalid console invocation");
        }
        m_invocations.push_back(std::move(invocation));
        m_error.clear();
      } else {
        m_error = "Console listen was rejected";
      }
    }
  }
  while (m_requests.size() < MaximumInFlight && !m_outgoing.empty()) {
    GuestConsoleRequest request = m_outgoing.front();
    if (!enqueue(std::move(request))) {
      break;
    }
    m_outgoing.pop_front();
  }
  if (m_listen == 0 && !m_listening) {
    GuestConsoleRequest listen;
    listen.action = GuestConsoleAction::Listen;
    m_listening = enqueue(std::move(listen));
  }
}
bool
GuestConsole::take(GuestConsoleRequest& invocation)
{
  if (m_invocations.empty()) {
    return false;
  }
  invocation = std::move(m_invocations.front());
  m_invocations.pop_front();
  return true;
}
