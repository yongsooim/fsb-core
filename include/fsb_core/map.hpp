#pragma once
#include "raster.hpp"
#include <map>
#include <functional>

namespace fsb::core {
class Sprites;
class Actors;
struct MapAssets {
    // Order matches asset_names(): P PCX, P MAT, S PCX, S MAT, P MAP, P MFO.
    std::array<std::vector<std::uint8_t>, 6> files;
};
struct TileCommand {
    unsigned layer = 0, pass = 0, tile = 0;
    std::uint32_t flags = 0;
    int x = 0, y = 0, sort_key = 0;
    std::optional<std::uint8_t> checker_fill;
};

class Map {
public:
    explicit Map(Memory& memory) : memory_(memory) {}
    static std::array<std::string, 6> asset_names(const Memory&, unsigned map_id);
    // Imports original assets/tables. The full E0 transition also requires
    // actor cleanup/rebuild, focus, map callbacks, and event lifecycle elsewhere.
    void load_assets(unsigned map_id, const MapAssets& assets);
    void register_assets(unsigned map_id, MapAssets assets);
    // Host byte supply on map entry. Static registrations override this;
    // supplied bundles are consumed immediately, not retained per map.
    std::function<MapAssets(unsigned)> asset_provider;
    std::function<void(Address)> setup_callback;
    void load_registered(unsigned map_id);
    void rebuild_animation_lookup();
    bool setup_field_overlays(Address callback);
    void switch_map(unsigned map_id, std::uint32_t flags);
    void enter_field(unsigned map_id,unsigned pcpos); // Normal field resource rebuild, preserving MFO entry pose.
    void restore_entry(unsigned pcpos);
    void apply_patch(unsigned patch_id,bool event_flag);
    Address register_overlay(Rect rectangle,Address callback,unsigned flags);
    void attach_sprites(Sprites& sprites) { sprites_=&sprites; }
    void attach_actors(Actors& actors) { actor_service_=&actors; }
    void tick_tile_animation();
    void update_occupancy();
    void update_scroll(); // 0x45544f; uses the camera and active layer in Memory.
    std::vector<TileCommand> background_commands(unsigned layer,std::uint32_t effect_mask=0);
    void draw_background(Image8& target, const std::vector<TileCommand>& commands) const;
    const Image8& sheet(unsigned plane) const;
private:
    Memory& memory_;
    Sprites* sprites_=nullptr;
    Actors* actor_service_=nullptr;
    std::array<Image8, 2> sheets_;
    std::map<unsigned,MapAssets> registered_;
    void load_mat(unsigned plane, const std::vector<std::uint8_t>&);
    void load_layout(const std::vector<std::uint8_t>&);
    void load_event_mfo(const std::vector<std::uint8_t>&);
    void rebuild(unsigned map_id,std::uint32_t flags,std::optional<unsigned> entry);
};
} // namespace fsb::core
