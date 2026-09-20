#include <IllumoGuest/Display.h>

void
GuestDisplay::pump()
{
  if (m_request != 0) {
    GuestServiceRecord result;
    if (!m_services.take(m_request, result)) {
      return;
    }
    m_request = 0;
    if (result.status != GuestServiceStatus::Complete) {
      m_error = "Display settings request was rejected";
    } else {
      GuestWireReader reader(result.payload);
      GuestDisplayState actual;
      if (!GuestDisplayState::read(reader, actual) || !reader.finished()) {
        throw std::runtime_error("Invalid display completion");
      }
      m_actual = actual;
      m_ready = true;
      m_error.clear();
    }
  }
  if (m_pending) {
    GuestWireWriter writer;
    m_next.write(writer);
    m_request = m_services.enqueue(GuestService::Display, writer.take());
    m_pending = m_request == 0;
  }
}
