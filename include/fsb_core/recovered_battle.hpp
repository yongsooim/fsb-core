#pragma once
#include "primitives.hpp"
#include <functional>
#include <initializer_list>

namespace fsb::core {
class EffectScript;
namespace map_logic { struct MapObjectServices; }
// Fixed C++ translations of the original integer battle routines. The build
// contains every basic block; no EXE instruction decoding or execution occurs
// at runtime. Guest-width arithmetic preserves overflow, flags and call order.
// Platform-facing and already ported callees are explicit service boundaries.
class RecoveredBattle {
public:
    explicit RecoveredBattle(Memory& memory):memory_(memory),stack_(65536){}
    using Service=std::function<bool(Address,RecoveredBattle&)>;
    Service service;
    EffectScript* effect_scripts=nullptr; // Temporary bridge for unmigrated callers.
    // Observe an original call after its return; used to propagate host I/O
    // failures ignored by the legacy fclose path, without rewriting game code.
    std::function<void(Address,RecoveredBattle&)> after_call;
    static bool has_entry(Address entry);
    static bool is_import(Address entry);
    static std::uint64_t x87_truncate(double value);
    static double x87_square_root(double value);
    std::uint32_t invoke(Address entry,const std::vector<std::uint32_t>& args={});
    // Native services can call back into original code while an outer call is
    // suspended. Preserve its registers/flags/FPU and use a nested guest frame.
    std::uint32_t callback(Address entry,const std::vector<std::uint32_t>& args={});
    std::uint8_t invoke_byte(Address entry,const std::vector<std::uint32_t>& args={}){return std::uint8_t(invoke(entry,args));}
    std::uint32_t read(Address address,unsigned width=4)const;
    void write(Address address,std::uint32_t value,unsigned width=4);
    std::uint32_t argument(unsigned index)const{return read(r[4]+4+index*4);}
    std::string format_text(Address format,Address arguments)const;
    void result(std::uint32_t value,unsigned popped_bytes=0);
    std::array<std::uint32_t,8> r{}; // EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI.
private:
    Memory& memory_;
    std::vector<std::uint8_t> stack_; // Guest call storage must not consume the host/WASM stack.
    bool carry_=false,zero_=false,sign_=false,overflow_=false,parity_=false;
    unsigned depth_=0;
    std::vector<double> fp_; // Menu ornament math uses exactly representable binary factors.
    std::uint32_t get(unsigned reg,unsigned width=4,unsigned shift=0)const;
    void put(unsigned reg,std::uint32_t value,unsigned width=4,unsigned shift=0);
    void push(std::uint32_t value);
    std::uint32_t pop();
    void flags(std::uint32_t value,unsigned width);
    std::uint32_t add(std::uint32_t a,std::uint32_t b,unsigned width=4,bool carry=false);
    std::uint32_t sub(std::uint32_t a,std::uint32_t b,unsigned width=4,bool borrow=false);
    std::uint32_t logic(std::uint32_t value,unsigned width=4);
    std::uint32_t inc(std::uint32_t value,unsigned width,int delta);
    std::uint32_t shift(std::uint32_t value,std::uint32_t count,unsigned width,unsigned kind);
    std::uint32_t multiply(std::uint32_t a,std::uint32_t b,unsigned width);
    void divide(std::uint32_t divisor,bool is_signed);
    bool condition(unsigned id)const;
    void dispatch(Address entry);
    bool dispatch_native(Address entry);
    bool dispatch_effect_script(Address entry);
    bool dispatch_event_activation(Address entry);
    bool dispatch_handler_lifecycle(Address entry);
    bool dispatch_attached_effects(Address entry);
    bool dispatch_followup(Address entry);
    bool dispatch_turn_control(Address entry);
    bool dispatch_battle_engine(Address entry);
    bool dispatch_entrance(Address entry);
    bool dispatch_anchor_effects(Address entry);
    bool dispatch_target_geometry(Address entry);
    bool dispatch_static_dialog_template(Address entry);
    bool dispatch_a_actors(Address entry);
    bool dispatch_c_maps(Address entry);
    map_logic::MapObjectServices legacy_map_services();
    bool dispatch_c_maps_installers(Address entry);
    bool dispatch_b_combat(Address entry);
    bool dispatch_d_effects(Address entry);
    bool dispatch_d_effect_objects(Address entry);
    std::span<const std::uint8_t> read_buffer(Address address,std::size_t size)const;
    std::span<std::uint8_t> write_buffer(Address address,std::size_t size);
    void float_to_integer();
    void float_square_root();
    void float_arctangent();
    // Declaration list is generated together with the fixed function bodies.
#include "recovered_battle_entries.inc"
};
} // namespace fsb::core
