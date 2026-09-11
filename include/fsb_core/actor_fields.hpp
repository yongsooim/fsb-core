#pragma once
#include "primitives.hpp"

namespace fsb::core {
Address lookup_actor(const Memory& memory, std::uint32_t id_or_object);
void set_actor_tile_position(Memory& memory, Address object, std::int32_t x, std::int32_t y, std::int32_t z);
void set_actor_raw_position(Memory& memory, Address object, std::uint32_t x, std::uint32_t y);
std::uint32_t fixed_sin(const Memory& memory, std::uint32_t angle);
std::uint32_t fixed_cos(const Memory& memory, std::uint32_t angle);
void set_actor_tile_state(Memory& memory, std::uint32_t id_or_object, unsigned state);
} // namespace fsb::core
