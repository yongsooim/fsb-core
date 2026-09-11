#include "fsb_core/combat/object_motion.hpp"
#include "fsb_core/recovered_battle.hpp"
namespace fsb::core {
bool RecoveredBattle::dispatch_target_geometry(Address entry) {
    switch(entry){case 0x462f5b:case 0x463038:case 0x4630ed:break;default:return false;}
    combat::ObjectMotion motion(memory_);
    if(entry==0x462f5b)result(motion.distance_to_target(argument(0)),4);
    else result(motion.heading_to_target(argument(0),entry==0x4630ed),4);
    return true;
}
}
