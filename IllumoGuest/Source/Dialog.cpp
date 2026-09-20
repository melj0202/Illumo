#include <IllumoGuest/Dialog.h>

void
GuestDialog::apply(bool save,
                   std::string description,
                   std::string defaultName,
                   std::string pattern)
{
  GuestDialogRequest request{
    save, std::move(description), std::move(defaultName), std::move(pattern)
  };
  GuestWireWriter bytes;
  request.write(bytes);
  GuestDialogRequest checked;
  if (!GuestDialogRequest::read(bytes.data(), checked)) {
    throw std::invalid_argument("Invalid file dialog request");
  }
  m_next = std::move(request);
  m_pending = true;
}
void
GuestDialog::pump()
{
  if (m_request != 0) {
    GuestServiceRecord result;
    if (!m_services.take(m_request, result)) {
      return;
    }
    m_request = 0;
    if (result.status != GuestServiceStatus::Complete) {
      m_error = "File dialog request was rejected";
      m_result = {};
    } else if (!GuestDialogResult::read(result.payload, m_result)) {
      throw std::runtime_error("Invalid file dialog completion");
    } else {
      m_error.clear();
    }
  }
  if (m_pending) {
    GuestWireWriter writer;
    m_next.write(writer);
    m_request = m_services.enqueue(GuestService::Dialog, writer.take());
    m_pending = m_request == 0;
  }
}
