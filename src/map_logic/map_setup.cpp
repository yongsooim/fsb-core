#include "fsb_core/map_logic/map_setup.hpp"
#include "fsb_core/map_logic/map_objects.hpp"
#include "fsb_core/map.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core::map_logic {
namespace {
#include "map_setup_tables.inc"
}
const MapInstaller* find_installer(Address entry){
    const auto found=std::lower_bound(std::begin(installers),std::end(installers),entry,
        [](const MapInstaller& installer,Address wanted){return installer.entry<wanted;});
    return found!=std::end(installers)&&found->entry==entry?found:nullptr;
}
bool install_map_overlays(Memory& memory,Map& map,Address entry){
    const auto* installer=find_installer(entry);
    if(!installer)return false;
    replay_registrations(memory,map,installer->records,installer->count);
    return true;
}
void replay_registrations(Memory& memory,Map& map,const OverlayRegistration* records,unsigned count){
    for(unsigned i=0;i<count;++i){
        const auto& record=records[i];
        const auto slot=map.register_overlay({record.left,record.top,record.right,record.bottom},record.callback,record.flags);
        if(record.publish)memory.write(globals::current_overlay_effect,slot);
        // A scratch word the installer leaves alone keeps whatever the record
        // already held, so only the recorded writes are replayed.
        const std::pair<unsigned,std::uint32_t> scratch[]={
            {overlay_offset::patch_id,record.patch_id},{overlay_offset::scratch,record.scratch},
            {overlay_offset::scratch_frame,record.scratch_frame},{overlay_offset::gate_flag,record.gate_flag}};
        for(unsigned field=0;field<4;++field)
            if(record.written&(1u<<field))memory.write(slot+scratch[field].first,scratch[field].second);
    }
}
} // namespace fsb::core::map_logic
