#pragma once
#include <Illumo/Rendering/RenderCommand.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// Tokens recorded once and run in place by ExecuteList tokens, frame after
// frame, until the owner records again. The list owns every matrix its tokens
// point to, so the pointer-lifetime rule holds across submissions. Lists
// cannot execute other lists. Main-thread only; not movable, because queued
// ExecuteList tokens hold its address.
class RecordedCommandList
{
public:
  static constexpr size_t DefaultCeiling = 65536;

  explicit RecordedCommandList(size_t ceiling = DefaultCeiling)
    : m_ceiling(ceiling)
  {
  }
  RecordedCommandList(const RecordedCommandList&) = delete;
  RecordedCommandList& operator=(const RecordedCommandList&) = delete;
  RecordedCommandList(RecordedCommandList&&) = delete;
  RecordedCommandList& operator=(RecordedCommandList&&) = delete;

  // Empties the list for a new recording. Matrix slots are kept for reuse,
  // so re-recording a list of the same shape allocates nothing.
  void clear()
  {
    m_commands.clear();
    m_matrixCount = 0;
    m_failed = false;
    ++m_revision;
  }

  // False, and the list is marked failed, past the ceiling or for a nested
  // ExecuteList. A failed list is refused by backends until cleared.
  bool append(const RenderCommand& command)
  {
    if (m_commands.size() >= m_ceiling ||
        command.commandType == CommandType::ExecuteList) {
      m_failed = true;
      return false;
    }
    m_commands.push_back(command);
    return true;
  }

  // A copy of a 4x4 matrix whose address stays valid until clear().
  const float* retainMatrix(const float* value)
  {
    // A deque never moves its elements, so earlier addresses stay valid.
    if (m_matrixCount == m_matrices.size()) {
      m_matrices.emplace_back();
    }
    std::array<float, 16>& retained = m_matrices[m_matrixCount++];
    for (size_t index = 0; index < retained.size(); ++index) {
      retained[index] = value[index];
    }
    return retained.data();
  }

  size_t size() const { return m_commands.size(); }
  const RenderCommand& at(size_t index) const { return m_commands[index]; }
  // For the owner's per-frame patches of fields such as instance counts.
  RenderCommand& at(size_t index) { return m_commands[index]; }
  bool failed() const { return m_failed; }
  uint64_t revision() const { return m_revision; }

private:
  std::vector<RenderCommand> m_commands;
  std::deque<std::array<float, 16>> m_matrices;
  size_t m_matrixCount = 0;
  size_t m_ceiling;
  bool m_failed = false;
  uint64_t m_revision = 0;
};
