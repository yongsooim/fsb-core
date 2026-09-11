#pragma once
#include "primitives.hpp"

namespace fsb::core {
namespace battle_status {
inline constexpr unsigned down=0x200000,unavailable=0x600000,action_blocked=0x600a00,
    poison=0x100,sleep=0x200,silence=0x400,paralysis=0x800,curse=0x1000,
    lucky_march=0x2000,damage_guard=0x4000,reflect=0x8000,power_samba=0x10000,hiphop=0x20000;
}
// Original battle data/rule routines. All state stays in guest records; no
// renderer, input device or wall-clock dependency belongs in this layer.
class BattleRules {
public:
    explicit BattleRules(Memory& memory):memory_(memory){}
    static Address party_record(unsigned id){return 0x607a08+id*0xbc;}
    static Address enemy_record(unsigned slot){return 0x806b60+slot*36;}
    static Address monster_record(unsigned id){return 0x609ca0+id*124;}
    bool equipped(unsigned party_id,unsigned item)const;
    unsigned precheck()const; // 0 ongoing,1 enemy defeat,2 party defeat (checked first).
    void decay_status();
    void advance_gauges();
    std::optional<unsigned> next_context();
    void compute_rewards();
    void prepare_vitality();
    void commit_vitality();
    // 44d389/44d40a are separate original entries; each marks only its own panel
    // dirty, so the halves stay callable on their own.
    void snapshot_party_panel(bool use_active);
    void snapshot_enemy_panel(bool use_active);
    void snapshot_panels(bool use_active);
    unsigned status(unsigned context)const;
    std::uint32_t take_poison_delta(unsigned context);
    void apply_delta(unsigned context,std::uint32_t delta);
    unsigned default_action(unsigned context,bool secondary)const;
    unsigned default_handler(unsigned context,bool secondary)const;
    void prepare_handler(unsigned handler,unsigned facing);
    void clear_cursor(int x,int y,int radius,unsigned keep);
    void mark_reachable(int max_cost);
    // Original AI span union, clipped to map cells before writing its byte grid.
    // nullopt combines every registered region (452255); one region is452168.
    void rasterize_ai_regions(std::optional<unsigned> region);
    void project_action(int x,int y,bool shifted);
    bool shift_footprint(unsigned direction);
    std::vector<std::uint32_t> collect_targets(Address actor,unsigned object_mask,unsigned excluded,std::optional<unsigned> required=std::nullopt)const;
    int relation(Address attacker,Address target)const;
    static int relation_hit_bonus(unsigned relation);
    static int relation_damage_percent(unsigned relation);
    static int counter_chance(unsigned row,unsigned relation);
    static int guard_percent(unsigned attacker_flags,unsigned target_flags);
    static int accuracy_bonus(unsigned attacker_status,unsigned target_status);
private:
    Memory& memory_;
    void preview(unsigned frames);
};
} // namespace fsb::core
