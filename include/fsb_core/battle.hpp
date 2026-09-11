#pragma once
#include "battle_rules.hpp"
#include "recovered_battle.hpp"
#include "raster.hpp"
#include <map>

namespace fsb::core {
class Runtime;
class Battle {
public:
    explicit Battle(Runtime& runtime);
    void reset_snapshot();
    Address snapshot_actor(unsigned id);
    void trigger_scripted(unsigned group);
    void begin_scripted();
    void tick();
    void tick_player(Address actor);
    void tick_callback(Address callback,Address actor);
    void tick_banner(Address object);
    void tick_shop();
    void tick_map_transition();
    void tick_worldmap();
    void tick_game_over();
    bool handles_callback(Address callback)const{return RecoveredBattle::has_entry(callback);}
    BattleRules rules;
    RecoveredBattle recovered;
private:
    Runtime& runtime_;
    void tick_intro();
    bool service(Address entry,RecoveredBattle& code);
    void show_banner(unsigned action);
    void initialize_ui();
    void open_menu_assets(bool field);
    void close_menu_assets();
    bool ui_service(Address entry,RecoveredBattle& code);
    bool field_service(Address entry,RecoveredBattle& code);
    struct TextContext {unsigned font=0,color=0,background_mode=1,background_color=0;std::optional<Rect> clip;};
    std::map<Address,TextContext> text_contexts_;
    std::map<Address,Rect> text_regions_;
    Address worldmap_result_=0;
};
} // namespace fsb::core
