#include <Illumo/Platform/SaveLoad.h>
#include <Illumo/Services/Logger.h>

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <windows.h>
// After windows.h.
#include <shobjidl.h>

static std::wstring
wideText(const std::string& text)
{
  if (text.empty() ||
      text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8,
                                       MB_ERR_INVALID_CHARS,
                                       text.data(),
                                       static_cast<int>(text.size()),
                                       nullptr,
                                       0);
  if (size == 0) {
    return {};
  }
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8,
                      MB_ERR_INVALID_CHARS,
                      text.data(),
                      static_cast<int>(text.size()),
                      result.data(),
                      size);
  return result;
}

static std::string
utf8Text(const wchar_t* text)
{
  const int size = WideCharToMultiByte(
    CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  std::string result(static_cast<size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8,
                          WC_ERR_INVALID_CHARS,
                          text,
                          -1,
                          result.data(),
                          size,
                          nullptr,
                          nullptr) == 0) {
    return {};
  }
  result.pop_back();
  return result;
}

// The window a dialog is owned by: the active one (main or a detached
// panel), else this process's foreground window. Owning keeps the dialog
// modal to it and gives Windows somewhere to return activation; without an
// owner the app is left unfocused after the dialog closes.
static HWND
dialogOwner()
{
  HWND owner = GetActiveWindow();
  if (owner == nullptr) {
    const HWND foreground = GetForegroundWindow();
    DWORD process = 0;
    GetWindowThreadProcessId(foreground, &process);
    if (foreground != nullptr && process == GetCurrentProcessId()) {
      owner = foreground;
    }
  }
  return owner;
}

static void
restoreOwner(HWND owner)
{
  if (owner != nullptr && IsWindow(owner)) {
    // A fullscreen GLFW window iconifies when the dialog takes focus.
    if (IsIconic(owner)) {
      ShowWindow(owner, SW_RESTORE);
    }
    SetForegroundWindow(owner);
    SetFocus(owner);
  }
}

static std::string
chooseFile(const SaveLoadDialogSpec& specification, bool save)
{
  const std::wstring description = wideText(
    specification.fileDescription.empty() ? "Illumo File Format"
                                          : specification.fileDescription);
  const std::wstring pattern = wideText(specification.extensionPattern.empty()
                                          ? "*.ILLUMO"
                                          : specification.extensionPattern);
  const std::wstring filename = wideText(specification.defaultFilename.empty()
                                           ? "MyCanvas.illumo"
                                           : specification.defaultFilename);
  std::array<wchar_t, 32768> file{};
  if (description.empty() || pattern.empty() || filename.empty() ||
      filename.size() >= file.size()) {
    Logger::LogError(
      "SaveLoad: invalid UTF-8 dialog specification or filename too long");
    return {};
  }
  std::copy(filename.begin(), filename.end(), file.begin());
  std::wstring filter = description + L" (" + pattern + L")";
  filter.push_back(L'\0');
  filter += pattern;
  filter.push_back(L'\0');
  filter.push_back(L'\0');
  const HWND owner = dialogOwner();
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.hwndOwner = owner;
  dialog.lpstrFile = file.data();
  dialog.nMaxFile = static_cast<DWORD>(file.size());
  dialog.lpstrFilter = filter.c_str();
  dialog.nFilterIndex = 1;
  dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                 (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  const BOOL chosen =
    save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
  restoreOwner(owner);
  if (!chosen) {
    if (CommDlgExtendedError() != 0) {
      Logger::LogError("SaveLoad: native file dialog failed");
    }
    return {};
  }
  return utf8Text(file.data());
}

// The common item dialog in folder mode (the shell's own folder picker).
// COM is initialized for this call when the calling thread has none yet
// (the dialogs run on the main thread, which the engine does not
// initialize for COM).
std::string
SaveLoad::GetFolderLocation(const std::string& title)
{
  const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool initialized = SUCCEEDED(init);
  if (init == RPC_E_CHANGED_MODE) {
    // COM already runs on this thread in another mode; use it as it is.
  } else if (!initialized) {
    Logger::LogError("SaveLoad: COM is unavailable for the folder dialog");
    return {};
  }
  std::string result;
  IFileOpenDialog* dialog = nullptr;
  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog,
                                 nullptr,
                                 CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(&dialog)))) {
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
      dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                         FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    }
    const std::wstring caption =
      wideText(title.empty() ? "Choose Folder" : title);
    if (!caption.empty()) {
      dialog->SetTitle(caption.c_str());
    }
    const HWND owner = dialogOwner();
    const HRESULT shown = dialog->Show(owner);
    restoreOwner(owner);
    if (SUCCEEDED(shown)) {
      IShellItem* item = nullptr;
      if (SUCCEEDED(dialog->GetResult(&item))) {
        wchar_t* path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
          result = utf8Text(path);
          CoTaskMemFree(path);
        }
        item->Release();
      }
    } else if (shown != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      Logger::LogError("SaveLoad: native folder dialog failed");
    }
    dialog->Release();
  } else {
    Logger::LogError("SaveLoad: the folder dialog could not be created");
  }
  if (initialized) {
    CoUninitialize();
  }
  return result;
}

std::string
SaveLoad::GetLoadLocation(const SaveLoadDialogSpec& specification)
{
  return chooseFile(specification, false);
}

std::string
SaveLoad::GetSaveLocation(const SaveLoadDialogSpec& specification)
{
  return chooseFile(specification, true);
}
