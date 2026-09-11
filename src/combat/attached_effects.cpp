#include "fsb_core/combat/attached_effects.hpp"

namespace fsb::core::combat {
namespace {
using namespace attached_effects;
template<class F> const F& required(const F& service,Address at){
    if(!service)throw Fault(at,"attached effect service is not connected");
    return service;
}
unsigned slot(unsigned row,std::int32_t kind){return row*kinds_per_owner+std::uint32_t(kind)-1u;}
}
std::optional<unsigned> AttachedEffects::find_owner(Address owner)const{
    for(unsigned row=0;row<owner_capacity;++row)if(memory_.read(owners+row*word_bytes)==owner)return row;
    return std::nullopt;
}
bool AttachedEffects::attach(Address owner,std::int32_t kind){
    if(kind<1 || kind>std::int32_t(kinds_per_owner)){
        required(services_.report,0x401ad8)(invalid_kind_message);return false;
    }
    auto row=find_owner(owner);
    if(!row)row=find_owner(0);
    if(!row)return false;
    const auto index=slot(*row,kind),pointer=objects+index*word_bytes;
    if(memory_.read(pointer))return false;
    const auto callback=memory_.read(callbacks+(std::uint32_t(kind)-1u)*word_bytes);
    const auto object=required(services_.spawn,0x45d89c)(callback);
    // No invented success on allocator failure: the original writes the owner
    // through the returned object before publishing any registry entry.
    memory_.write(object+object_owner,owner);
    memory_.write(pointer,object);
    memory_.write(counts+*row*word_bytes,memory_.read(counts+*row*word_bytes)+1u);
    memory_.write(owners+*row*word_bytes,owner);
    memory_.write(kinds+index*word_bytes,std::uint32_t(kind));
    return true;
}
bool AttachedEffects::detach(Address owner,std::int32_t kind){
    const auto row=find_owner(owner);if(!row)return false;
    // Unlike attach, the original has no kind-range guard here. The wrapped
    // index can address adjacent words; preserve that boundary behavior.
    const auto index=slot(*row,kind),pointer=objects+index*word_bytes;
    const auto object=memory_.read(pointer);if(!object)return false;
    memory_.write(kinds+index*word_bytes,0);
    memory_.write(pointer,0);
    const auto remaining=memory_.read(counts+*row*word_bytes)-1u;
    memory_.write(counts+*row*word_bytes,remaining);
    if(!remaining)memory_.write(owners+*row*word_bytes,0);
    required(services_.release,0x45d91d)(object);
    return true;
}
void AttachedEffects::clear_row(unsigned row){
    for(unsigned kind=0;kind<kinds_per_owner;++kind){
        const auto offset=(row*kinds_per_owner+kind)*word_bytes;
        const auto object=memory_.read(objects+offset);
        // Group teardown releases first. Do not merge this with detach's
        // clear-before-release ordering or precollect objects across callbacks.
        if(object)required(services_.release,0x45d91d)(object);
        memory_.write(kinds+offset,0);
        memory_.write(objects+offset,0);
    }
    memory_.write(owners+row*word_bytes,0);
    memory_.write(counts+row*word_bytes,0);
}
bool AttachedEffects::clear_owner(Address owner){
    const auto row=find_owner(owner);if(!row)return false;
    clear_row(*row);return true;
}
void AttachedEffects::clear_all(){
    for(unsigned row=0;row<owner_capacity;++row)clear_row(row);
}
} // namespace fsb::core::combat
