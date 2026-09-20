#include <IllumoGuest/Clipboard.h>

void
GuestClipboard::pump()
{
  if (m_request != 0) {
    GuestServiceRecord result;
    if (!m_services.take(m_request, result)) {
      return;
    }
    m_request = 0;
    if (result.status != GuestServiceStatus::Complete) {
      m_error = "Clipboard request was rejected";
    } else {
      GuestClipboardRequest actual;
      if (!GuestClipboardRequest::read(result.payload, actual) || actual.set) {
        throw std::runtime_error("Invalid clipboard completion");
      }
      m_text = std::move(actual.text);
      m_error.clear();
    }
  }
  if (m_pending) {
    GuestWireWriter writer;
    m_next.write(writer);
    m_request = m_services.enqueue(GuestService::Clipboard, writer.take());
    m_pending = m_request == 0;
  }
}
