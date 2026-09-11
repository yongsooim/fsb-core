#pragma once
#include "primitives.hpp"
#include <functional>

namespace fsb::core {
class Palette;
class Audio;
class Map;
class Viewport;
class Dialogue;
class Sprites;
class Transition;
class Battle;
class Actors;
class RecoveredBattle;
class DebugRewards;
enum class Yield : unsigned { Continue, Normal, Forced };
struct VmEnvironment {
    bool input_pause = false;
    bool dialog_busy = false; // Legacy override only: 0x411d12 returns0. Do not bind this to active dialogue count.
    bool key_down = false;
    bool input_high_blocked = false;
    std::uint32_t idle_frame_counter = 0;
    std::optional<std::uint32_t> local_time_seed; // Explicit host observation for opcode19 seed==0.
    // SYSTEMTIME word order is the original seed observation, not a core clock.
    std::function<std::array<std::uint16_t,8>()> local_time;
    std::function<bool(std::uint32_t)> system_beep;
    Palette* palette = nullptr; // Core subsystem; no host callback that fakes completion.
    Audio* audio = nullptr;
    Map* map = nullptr;
    Viewport* viewport = nullptr;
    Dialogue* dialogue = nullptr;
    Sprites* sprites = nullptr;
    Transition* transition = nullptr;
    Battle* battle = nullptr;
    Actors* actors = nullptr;
    RecoveredBattle* recovered = nullptr; // Fixed original core routines, never a host VM.
    DebugRewards* debug_rewards = nullptr;
    std::function<void(Address object, const Instruction&, bool after)> trace;
    std::function<void(std::uint32_t,std::uint32_t,std::uint32_t)> post_message;
    std::vector<std::pair<Address, std::string>> diagnostics;
};
struct FrameResult {
    unsigned instructions = 0;
    unsigned normal_yields = 0;
    Yield yield = Yield::Continue;
    bool finished = false;
};

// VM objects live in guest memory. All callbacks execute in the portable core;
// no platform host is allowed to "finish" an unimplemented game operation.
class Vm {
public:
    Vm(Memory& memory, HsmQueue& messages, VmEnvironment& environment, Address object)
        : memory_(memory), queue_(messages), env_(environment), object_(object) {}
    Yield step();
    FrameResult run_frame(unsigned original_jobs, unsigned instruction_limit = 100000);
    Address pc() const { return memory_.read(object_ + 0x30); }
    static bool supported(std::uint8_t opcode, std::uint8_t subop);
private:
    Memory& memory_;
    HsmQueue& queue_;
    VmEnvironment& env_;
    Address object_;
    Yield original_command(const Instruction& ins);
    Yield hsm(const Instruction& ins);
    Yield wait(const Instruction& ins);
    Yield control(const Instruction& ins);
    Yield arithmetic(const Instruction& ins);
    Yield object_command(const Instruction& ins);
    Yield ifc_command(const Instruction& ins);
    Yield palette_command(const Instruction& ins);
    Yield camera_command(const Instruction& ins);
    Yield actor_command(const Instruction& ins);
    Yield vector_command(const Instruction& ins);
    Yield sprite_command(const Instruction& ins);
    Yield callback_command(const Instruction& ins);
    Yield audio_command(const Instruction& ins);
    Yield dialogue_command(const Instruction& ins);
    unsigned depth() const;
    void next(const Instruction& ins) { memory_.write(object_ + 0x30, ins.next()); }
    Message cache() const;
    void cache(Message m);
    bool alive(Handle handle) const { return resolve_compact(memory_, handle).has_value(); }
};
} // namespace fsb::core
