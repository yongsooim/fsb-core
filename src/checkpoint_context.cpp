#include "fsb_core/checkpoint_context.hpp"
#include "fsb_core/actor_core/map_transition.hpp"
namespace fsb::core {
bool can_restore_secret_arena_return(std::span<const std::uint8_t> save,unsigned origin_map) {
    using namespace checkpoint_context;
    constexpr unsigned saved_map_offset=0xbf0;
    if(save.size()<saved_map_offset+4)return false;
    std::uint32_t map=0;for(unsigned i=0;i<4;++i)map|=std::uint32_t(save[saved_map_offset+i])<<(8*i);
    return (origin_map==west_tower_floor||origin_map==east_tower_floor)&&
           (map==secret_arena_lobby||map==secret_arena_ring);
}
bool restore_secret_arena_return(Memory& memory,unsigned origin_map) {
    using namespace checkpoint_context;
    if(origin_map!=west_tower_floor&&origin_map!=east_tower_floor)return false;
    const auto map=memory.read(actor_core::loaded_map);
    if(map!=secret_arena_lobby&&map!=secret_arena_ring)return false;
    memory.write(arena_return_map,origin_map);return true;
}
}
