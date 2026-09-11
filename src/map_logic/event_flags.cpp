#include "fsb_core/map_logic/event_flags.hpp"
#include "fsb_core/map_logic/map_patch_table.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core::map_logic {
Address EventFlags::word_of(std::int32_t id)const{
    // idiv truncates toward zero, so a negative id addresses backwards from the
    // bitmap base instead of rounding down to the previous word.
    return Address(globals::event_flag_bits+std::uint32_t(id/std::int32_t(bits_per_word))*4u);
}
std::uint32_t EventFlags::mask_of(std::int32_t id){
    // shl reads only the low five bits of CL, so a negative remainder wraps.
    return 1u<<(std::uint32_t(id%std::int32_t(bits_per_word))&(bits_per_word-1));
}
bool EventFlags::test(std::int32_t id)const{return (memory_.read(word_of(id))&mask_of(id))!=0;}
void EventFlags::set(std::int32_t id){const auto at=word_of(id);memory_.write(at,memory_.read(at)|mask_of(id));}
void EventFlags::clear(std::int32_t id){const auto at=word_of(id);memory_.write(at,memory_.read(at)&~mask_of(id));}
void resync_map_patches(Memory& memory,Map& map,std::uint32_t map_id){
    const EventFlags flags(memory);
    for(unsigned patch=0;patch<map_patch_records;++patch)
        if(memory.read(tables::map_patches+patch*map_patch_stride+map_patch_offset::map_id)==map_id)
            map.apply_patch(patch,flags.test(std::int32_t(patch)));
}
} // namespace fsb::core::map_logic
