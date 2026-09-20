#pragma once

#include <IllumoGuest/ResourceId.h>
#include <algorithm>
#include <memory>
#include <vector>

// Main-thread affine. Revocation removes authority immediately; accepted
// frames retain leases until submission completes. Retired-but-leased entries
// still count against the quota, so releasing IDs cannot evade it.
template<typename Resource, GuestResourceKind Kind>
class WasmResourceTable
{
public:
  explicit WasmResourceTable(std::uint64_t owner, std::uint32_t maximum = 4096)
    : m_owner(owner)
    , m_maximum(maximum)
  {
  }
  ~WasmResourceTable() = default;
  WasmResourceTable(const WasmResourceTable&) = delete;
  WasmResourceTable& operator=(const WasmResourceTable&) = delete;
  WasmResourceTable(WasmResourceTable&&) = delete;
  WasmResourceTable& operator=(WasmResourceTable&&) = delete;

  bool hasCapacity()
  {
    std::erase_if(m_leases, [](const std::weak_ptr<const Resource>& resource) {
      return resource.expired();
    });
    return !m_retired && m_owner != 0 && m_leases.size() < m_maximum;
  }

  GuestResourceId insert(std::shared_ptr<const Resource> resource)
  {
    if (!resource || !hasCapacity()) {
      return {};
    }
    std::size_t index = 0;
    while (
      index < m_slots.size() &&
      (m_slots[index].resource || m_slots[index].generation == UINT32_MAX)) {
      ++index;
    }
    if (index >= UINT32_MAX) {
      return {};
    }
    if (index == m_slots.size()) {
      m_slots.emplace_back();
    }
    // Allocate the bookkeeping before granting authority.
    m_leases.push_back(resource);
    Slot& slot = m_slots[index];
    ++slot.generation;
    slot.resource = std::move(resource);
    return {
      m_owner, Kind, static_cast<std::uint32_t>(index + 1), slot.generation
    };
  }

  std::shared_ptr<const Resource> resolve(const GuestResourceId& id) const
  {
    if (m_retired || id.owner != m_owner || id.kind != Kind || id.slot == 0 ||
        id.slot > m_slots.size()) {
      return {};
    }
    const Slot& slot = m_slots[id.slot - 1];
    return slot.generation == id.generation ? slot.resource : nullptr;
  }

  bool release(const GuestResourceId& id)
  {
    if (!resolve(id)) {
      return false;
    }
    m_slots[id.slot - 1].resource.reset();
    return true;
  }

  void retire()
  {
    m_retired = true;
    m_slots.clear();
    m_leases.clear();
  }

private:
  struct Slot
  {
    std::uint32_t generation = 0;
    std::shared_ptr<const Resource> resource;
  };
  std::uint64_t m_owner;
  std::uint32_t m_maximum;
  bool m_retired = false;
  std::vector<Slot> m_slots;
  std::vector<std::weak_ptr<const Resource>> m_leases;
};
