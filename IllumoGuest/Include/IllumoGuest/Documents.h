#pragma once

#include <IllumoGuest/Dialog.h>
#include <IllumoGuest/Files.h>
#include <deque>
#include <functional>

// A document the user chose. `location` is opaque: "selected:<grant>" for a
// file granted by a dialog or at launch, otherwise a private storage name.
// `label` is what the UI shows (the file's base name); never a host path.
struct GuestDocumentLocation
{
  std::string location;
  std::string label;
  bool empty() const { return location.empty(); }
};

enum class GuestDocumentDialog
{
  Open, // read-only selection
  Edit, // readable and writable, for save-in-place editors
  Save  // a new or replaced file
};

// Dialog and file plumbing for document products (editors, viewers).
// Requests queue immediately; completions run from pump() on a later update,
// never inside the requesting call. Destruction cancels outstanding files.
class GuestDocuments
{
public:
  using ChooseCallback = std::function<void(const GuestDocumentLocation&)>;
  using ReadCallback = std::function<
    void(bool success, const std::string& bytes, const std::string& error)>;
  using WriteCallback =
    std::function<void(bool success, const std::string& error)>;

  GuestDocuments(GuestServiceQueue& services, GuestFiles& files);
  ~GuestDocuments();
  GuestDocuments(const GuestDocuments&) = delete;
  GuestDocuments& operator=(const GuestDocuments&) = delete;
  GuestDocuments(GuestDocuments&&) = delete;
  GuestDocuments& operator=(GuestDocuments&&) = delete;

  // An empty location in the callback reports cancellation or failure.
  void choose(GuestDocumentDialog mode,
              std::string description,
              std::string defaultName,
              std::string pattern,
              ChooseCallback done);
  void read(const std::string& location, ReadCallback done);
  void write(const std::string& location,
             std::string bytes,
             WriteCallback done);
  void pump();

  // The --open document, as a location this class can read (and write, when
  // the runtime granted it editable).
  static GuestDocumentLocation launch(const GuestLaunch& launch);

private:
  struct Choice
  {
    GuestDocumentDialog mode = GuestDocumentDialog::Open;
    std::string description;
    std::string defaultName;
    std::string pattern;
    ChooseCallback done;
  };
  struct Transfer
  {
    std::uint64_t task = 0;
    std::string location;
    std::string bytes; // writes only, until submitted
    ReadCallback read;
    WriteCallback written;
  };
  std::uint64_t submit(Transfer& transfer);

  GuestFiles& m_files;
  GuestDialog m_dialog;
  std::deque<Choice> m_choices;
  bool m_dialogActive = false;
  std::deque<Transfer> m_pending;
  std::deque<Transfer> m_transfers;
};
