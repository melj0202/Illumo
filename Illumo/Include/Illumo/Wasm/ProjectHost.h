#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A recent project as the host lists it: the id is valid until the next
// list(), the label is the folder's name. Host paths stay on the host.
struct ProjectHostEntry
{
  std::uint32_t id = 0;
  std::string label;
};

// The host side of the Project service (Project capability): recent project
// folders for one application, folder dialogs that add one, and a restart
// of the runtime on a listed project. Main thread only; the dialogs block
// the caller like the file dialogs do.
class IProjectHost
{
public:
  IProjectHost() = default;
  virtual ~IProjectHost() = default;
  IProjectHost(const IProjectHost&) = delete;
  IProjectHost& operator=(const IProjectHost&) = delete;
  IProjectHost(IProjectHost&&) = delete;
  IProjectHost& operator=(IProjectHost&&) = delete;

  virtual bool available() const = 0;
  // The recent list, newest first, without folders that are gone.
  virtual std::vector<ProjectHostEntry> list() = 0;
  // Folder dialogs. False with *cancelled set when the user cancelled, false
  // with `error` otherwise; the entry is listed first on success.
  virtual bool choose(ProjectHostEntry* chosen,
                      bool* cancelled,
                      std::string& error) = 0;
  virtual bool create(ProjectHostEntry* created,
                      bool* cancelled,
                      std::string& error) = 0;
  // Prepares the runtime's restart on a listed project (the guest then asks
  // to restart); false with `error` for an unknown id or a folder that is
  // gone.
  virtual bool open(std::uint32_t id, std::string& error) = 0;
  virtual void clear() = 0;
};
