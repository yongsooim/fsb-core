#pragma once
#include "../primitives.hpp"

namespace fsb::core {
class Map;
namespace map_logic {
// One overlay record a map installer publishes. The original installers are
// straight-line code that fills a rectangle, hands it to the registrar and then
// writes the callback's scratch words into the record it just took, so the whole
// family is the same routine over different constants.
struct OverlayRegistration {
    std::int32_t left,top,right,bottom;
    Address callback;
    std::uint32_t flags;
    bool publish;                       // Stores the slot in globals::current_overlay_effect.
    std::uint32_t patch_id,scratch,scratch_frame,gate_flag;
    std::uint8_t written;               // One bit per scratch word the installer actually wrote.
};
struct MapInstaller {
    Address entry;
    const OverlayRegistration* records;
    unsigned count;
};
// Null when this original installer is not one of the recovered straight-line
// bodies; the caller then still owns it.
const MapInstaller* find_installer(Address entry);
// False when the entry is unknown, so a caller can fall back without guessing.
bool install_map_overlays(Memory& memory,Map& map,Address entry);
// Registers one recovered run of records; the installers that also spawn objects
// use this for the plain registrations around the spawn.
void replay_registrations(Memory& memory,Map& map,const OverlayRegistration* records,unsigned count);
} // namespace map_logic
} // namespace fsb::core
