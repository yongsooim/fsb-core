#pragma once
#include "actors.hpp"
#include "audio.hpp"
#include "camera.hpp"
#include "dialogue.hpp"
#include "dialog_graphics.hpp"
#include "draw_queue.hpp"
#include "object_pump.hpp"
#include "viewport.hpp"
#include "vm.hpp"
#include "input.hpp"
#include "transition.hpp"
#include "field.hpp"
#include "battle.hpp"
#include "field_menu.hpp"
#include "debug_rewards.hpp"
#include "effect_script.hpp"
#include <deque>

namespace fsb::core {
struct RuntimeStep { bool rendered=false,event0_finished=false,run_finished=false; unsigned jobs=0; std::vector<std::int16_t> pcm; };
struct EventCompletion { unsigned id; Address exit_pc; std::uint32_t ms; };
class Runtime {
public:
    explicit Runtime(const std::vector<std::uint8_t>& executable,unsigned width=640,unsigned height=480);
    Runtime(const Runtime&)=delete;Runtime& operator=(const Runtime&)=delete;
    void start_event0(unsigned through_event=0); // Follow original event chaining up to this boundary.
    void start_intro(unsigned through_event=full_campaign);
    bool intro_active()const{return intro_active_;}
    void start_saved_game(const std::vector<std::uint8_t>& bytes,unsigned through_event=full_campaign);
    static constexpr unsigned full_campaign=0xffffffffu; // No inspection stop at an event number.
    void start_field_fixture(unsigned map_id,unsigned through_event); // Isolated inspection fixture; not a restored campaign save.
    void post_input(InputMessage message) { inputs_.push_back(message); }
    std::size_t pending_input_count()const{return inputs_.size();}
    void set_active(bool active);
    RuntimeStep advance(std::uint32_t now,bool produce_pcm=true);
    const Image8& frame() const { return surfaces.get(primary_); }
    Handle root() const { return root_; }
    bool event0_finished() const { return !completed_events_.empty()&&completed_events_.front().id==0; }
    bool finished() const { return root_finished_; }
    bool game_over()const{return memory.read(0x804a64)==8&&memory.read(0x804a70)==2&&!palette.busy();}
    bool quit_requested()const{return close_requested_||memory.read(0x80465c)==0xffffffffu;}
    unsigned current_event() const { return current_event_; }
    const std::vector<EventCompletion>& completed_events() const { return completed_events_; }
    Address root_pc() const { const auto object=resolve_compact(memory,root_);return object?memory.read(*object+0x30):root_exit_pc_; }
    // Core services are public for byte-resource registration and diagnostics.
    Memory memory;
    Arena arena; Actors actors; Surfaces surfaces; Palette palette; Sprites sprites;Transition transition;
    Viewport viewport; Map map; Audio audio; EffectScript effects; HsmQueue messages; Dialogue dialogue;
    DialogGraphics graphics; Camera camera; DrawQueue draw; Field field; DebugRewards debug_rewards; Battle battle; FieldMenu field_menu; VmEnvironment environment;
private:
    ObjectPump pump_;
    FrameClock clock_;
    std::deque<InputMessage> inputs_;
    Address primary_=0,back_=0; Handle root_=0;
    unsigned width_,height_;bool started_=false;
    bool intro_active_=false;
    bool close_requested_=false;
    int title_pointer_down_=-1;
    bool root_finished_=false;Address root_exit_pc_=0;
    bool boundary_field_cleanup_=false;
    unsigned current_event_=0,through_event_=0;
    std::vector<EventCompletion> completed_events_;
    Address event_entry(unsigned id)const;
    void initialize_session(unsigned through_event);
    void begin_new_game();
    void tick_title();
    InputMessage title_pointer(InputMessage input);
    void dispatch(Address callback,Address object,unsigned jobs);
    void game_frame();
    void end_frame();
    void due_timers(std::uint32_t now);
};
} // namespace fsb::core
