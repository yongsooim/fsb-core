#pragma once
#include "markup.hpp"
#include "arena.hpp"
#include <functional>

namespace fsb::core {
class DialogGraphics;
class Audio;
class Dialogue {
public:
    Dialogue(Memory& memory, HsmQueue& queue) : memory_(memory), queue_(queue) {}
    void configure_timing(int free_text_percent, int event_text_percent, int event_delay_percent);
    Handle create(std::uint32_t actor_id, Address text, int facing = -1);
    Handle create_at(Address anchor_x,Address anchor_y,Address text,unsigned style_variant);
    Handle spawn_markup(std::uint32_t actor_id, Address text, std::uint32_t channel);
    void clear_sequence(Address slot);
    void release_reference(Address slot);
    void initialize_chunk(Address state, Address text);
    void apply_style(Address state);
    void enqueue_message(Handle controller, std::uint32_t target, std::uint32_t code);
    bool process_waiting_message(Handle controller);
    void tick(Address controller);
    void set_auto_advance(bool enabled){auto_advance_=enabled;}
    DialogGraphics* graphics = nullptr;
    Audio* audio = nullptr;
    Address state(Handle handle) const;
    static unsigned style_variant(std::uint32_t packed);
    static unsigned style_flags(std::uint32_t packed);
private:
    Memory& memory_;
    HsmQueue& queue_;
    bool auto_advance_=false; // Only ordinary text confirmation, never choices/HSM waits.
    bool rebuild(Address state,unsigned mode);
    void delay(Address state,std::uint32_t duration);
    bool segment_wait(Address state,Address cursor);
    bool advance_line(Address state,bool explicit_line);
    bool bypass_delay(Address state)const;
    bool confirm_advance();
    void start_selection(Address controller,Address state);
    bool update_selection(Address controller,Address state);
    void notify_selection(Address controller,unsigned index);
    void glyph(Address state,Address cursor,unsigned bytes);
    void refresh_gaps(Address state);
};
} // namespace fsb::core
