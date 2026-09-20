#pragma once

#include <IllumoGuest/Wire.h>

enum class GuestResourceKind : std::uint32_t
{
  Mesh = 1,
  Texture = 2,
  Font = 3,
  File = 4
};

struct GuestResourceId
{
  std::uint64_t owner = 0;
  GuestResourceKind kind = GuestResourceKind::Mesh;
  std::uint32_t slot = 0;
  std::uint32_t generation = 0;

  void write(GuestWireWriter& output) const
  {
    output.u64(owner);
    output.u32(static_cast<std::uint32_t>(kind));
    output.u32(slot);
    output.u32(generation);
  }
  static GuestResourceId read(GuestWireReader& input)
  {
    GuestResourceId result;
    result.owner = input.u64();
    result.kind = static_cast<GuestResourceKind>(input.u32());
    result.slot = input.u32();
    result.generation = input.u32();
    return result;
  }
};
