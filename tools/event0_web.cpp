// Browser host. The core never learns it is in a browser: this file is the
// only place that knows about requestAnimationFrame, DOM key codes or a canvas.
// It mirrors event0_sdl.cpp's loop, minus the parts SDL supplies.
#include "fsb_core/runtime.hpp"
#include "fsb_core/font_raster.hpp"
#include "fsb_core/playback.hpp"
#include "fsb_core/presentation.hpp"
#include "fsb_core/symbols.hpp"
#include "runtime_assets.hpp"
// The worklet mixes with the original's own millibel curve. Deriving a second
// one in JS would be a different curve in the last bits of every sample.
#include "../src/audio_gain.hpp"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

namespace {
using namespace fsb::core;

// The engine wants a Windows virtual-key code and the PC/XT scancode the
// original read from the keyboard controller. DOM gives us neither, but its
// KeyboardEvent.code names the physical key, which is what both derive from.
struct KeyMap { const char* code; unsigned virtual_key, scancode; };
constexpr KeyMap keys[] = {
    {"Enter",13,0x1c},{"NumpadEnter",13,0x9c},{"Space",32,0x39},{"Escape",27,0x01},
    {"ShiftLeft",16,0x2a},{"ShiftRight",16,0x36},{"ControlLeft",17,0x1d},{"ControlRight",17,0x9d},
    {"AltLeft",18,0x38},{"AltRight",18,0xb8},{"Pause",19,0xc5},{"End",35,0xcf},
    {"ArrowLeft",37,0xcb},{"ArrowUp",38,0xc8},{"ArrowRight",39,0xcd},{"ArrowDown",40,0xd0},
    {"Numpad5",101,0x4c},{"Numpad1",97,0x4f},
};
constexpr unsigned letter_scancodes[] = {
    0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
    0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};

bool translate(const char* code, unsigned& virtual_key, unsigned& scancode) {
    for (const auto& entry : keys)
        if (!std::strcmp(code, entry.code)) { virtual_key = entry.virtual_key; scancode = entry.scancode; return true; }
    if (!std::strncmp(code, "Key", 3) && code[3] >= 'A' && code[3] <= 'Z' && !code[4]) {
        virtual_key = unsigned(code[3]); scancode = letter_scancodes[code[3] - 'A']; return true;
    }
    if (!std::strncmp(code, "Digit", 5) && code[5] >= '0' && code[5] <= '9' && !code[6]) {
        virtual_key = unsigned(code[5]);
        scancode = code[5] == '0' ? 0x0b : unsigned(code[5] - '1') + 2; return true;
    }
    if (code[0] == 'F' && code[1] >= '1' && code[1] <= '5' && !code[2]) {
        virtual_key = unsigned(code[1] - '1') + 112; scancode = unsigned(code[1] - '1') + 0x3b; return true;
    }
    return false;
}

struct Host {
    Runtime runtime;
    FontRaster fonts;
    PlaybackClock playback{1};
    ImageRgba presented{};
    double previous_ms = 0;
    double unspent_ms = 0;
    bool initial_tick = true, active = true;
    unsigned width = 640, height = 480;
    Host(const std::vector<std::uint8_t>& executable, unsigned logical_width, unsigned logical_height)
        : runtime(executable, logical_width, logical_height), fonts(runtime.memory) {}
};
std::unique_ptr<Host> host;

// The worklet is the device. It holds the PCM, keeps its own sample positions
// and mixes on the audio clock, so what comes out never follows the game clock
// and a speed change is not something it can hear: no seek, no handover, no
// second mixer to stay level with. This forwards the same commands the native
// host's device mixer consumes and nothing else.
struct WorkletSink : AudioSink {
    void reset(const std::array<AudioVoice, 17>& voices) override {
        EM_ASM({ fsbVoiceReset(); });
        for (unsigned slot = 0; slot < voices.size(); ++slot)
            if (voices[slot].pcm) apply(AudioEvent::Kind::Load, slot, voices[slot]);
    }
    void apply(AudioEvent::Kind kind, unsigned slot, const AudioVoice& voice) override {
        double message[13] = {double(unsigned(kind)), double(slot), double(voice.handle), double(voice.volume),
                              double(voice.loop), double(voice.playing), double(voice.phase), 0, 0, 0, 0, 0, 0};
        if (voice.pcm) {
            const auto* pcm = voice.pcm.get();
            auto known = keys_.find(pcm);
            const bool first = known == keys_.end();
            if (first) {
                // Holding the wave keeps its address unique for the run, which
                // is what makes the address usable as the key on the far side.
                retained_.push_back(voice.pcm);
                known = keys_.emplace(pcm, ++next_key_).first;
            }
            message[7] = double(known->second);
            message[8] = pcm->rate; message[9] = pcm->channels; message[10] = double(pcm->frames());
            if (first) { message[11] = double(std::uintptr_t(pcm->samples.data())); message[12] = double(pcm->samples.size()); }
        }
        EM_ASM({ fsbVoice($0); }, message);
    }
private:
    std::map<const Pcm*, unsigned> keys_;
    std::vector<std::shared_ptr<const Pcm>> retained_;
    unsigned next_key_ = 0;
};
WorkletSink worklet;

// Hold-to-fast-forward, the same16x the native host gives the backtick key.
// PlaybackClock still executes every1ms tick, so the game runs faster without
// skipping any of it.
void set_fast_forward(bool held) {
    auto& h = *host;
    if (h.playback.fast_forward_held() == held) return;
    h.playback.set_fast_forward_held(held);
    // Real time measured across the switch was earned at the old rate; banking
    // it would spend it at the new one, exactly what the native host avoids by
    // restarting its previous_real stamp here.
    h.previous_ms = emscripten_get_now();
    h.unspent_ms = 0;
    EM_ASM({ if (window.fsbSpeed) fsbSpeed($0); }, h.playback.speed());
}

// One rAF step. SDL_GetTicks hands the native host whole milliseconds that
// carry their own remainder forward; emscripten_get_now returns a double, so
// the fraction has to be banked explicitly. Dropping it costs 0.67ms of every
// 16.67ms frame, which ran the game — and its audio — 4% slow.
void tick_unguarded() {
    auto& h = *host;
    const double real = emscripten_get_now();
    if (h.previous_ms) h.unspent_ms += real - h.previous_ms;
    h.previous_ms = real;
    auto elapsed = std::uint32_t(h.unspent_ms);
    h.unspent_ms -= elapsed;
    const bool running = h.active && (h.runtime.memory.read(globals::runtime_mode_flags) & 3) == 1;
    h.playback.elapse(elapsed, running);
    // The native host automates ordinary text confirmation whenever the rate is
    // above1x; choices, HSM waits and animation waits still need the player.
    h.runtime.dialogue.set_auto_advance(h.playback.speed() > 1);
    bool dirty = false;
    if (!running && !h.initial_tick) dirty |= h.runtime.advance(h.playback.now(), false).rendered;
    unsigned consumed = 0;
    while (h.initial_tick || h.playback.pending()) {
        if (h.initial_tick) h.initial_tick = false; else if (!h.playback.step()) break;
        // No PCM from the game clock, at any speed: advance_silently keeps the
        // original's voice positions and completion times on virtual time while
        // the worklet plays on the device's.
        auto step = h.runtime.advance(h.playback.now(), false);
        dirty |= step.rendered;
        if (h.runtime.quit_requested()) { emscripten_cancel_main_loop(); return; }
        // Hand the browser back its thread on the same schedule the native host
        // returns to event polling, so input stays responsive under load.
        if (++consumed >= 256 || (step.rendered && emscripten_get_now() - real >= 8)) break;
        if ((h.runtime.memory.read(globals::runtime_mode_flags) & 3) != 1) { h.playback.elapse(0, false); break; }
    }
    if (dirty) {
        h.presented = present_image(h.runtime.frame(), h.width, h.height, PresentationMode::enhanced, true);
        EM_ASM({ fsbPresent($0, $1, $2); }, h.presented.pixels.data(), h.width, h.height);
    }
}

// An unimplemented service reaches JS as a bare "CppException", so the loop
// reports the fault itself and stops rather than throwing every frame.
void tick() {
    try {
        tick_unguarded();
    } catch (const Fault& fault) {
        emscripten_cancel_main_loop();
        EM_ASM({ fsbFailed('Fault 0x' + ($0 >>> 0).toString(16) + ': ' + UTF8ToString($1)); },
               fault.address, fault.what());
    } catch (const std::exception& error) {
        emscripten_cancel_main_loop();
        EM_ASM({ fsbFailed(UTF8ToString($0)); }, error.what());
    }
}

EM_BOOL on_key(int type, const EmscriptenKeyboardEvent* event, void*) {
    unsigned virtual_key = 0, scancode = 0;
    if (!host) return EM_FALSE;
    // KeyboardEvent.code names the physical key, which is what the native host
    // binds through the scancode; the printed character changes on a Korean
    // layout but this key does not. The game never receives it.
    if (!std::strcmp(event->code, "Backquote")) {
        if (type != EMSCRIPTEN_EVENT_KEYDOWN) set_fast_forward(false);
        else if (!event->repeat && host->active
                 && !(event->shiftKey || event->ctrlKey || event->altKey || event->metaKey))
            set_fast_forward(true);
        return EM_TRUE;
    }
    if (!translate(event->code, virtual_key, scancode)) return EM_FALSE;
    host->runtime.post_input(keyboard_message(virtual_key, scancode, type == EMSCRIPTEN_EVENT_KEYDOWN,
                                              event->shiftKey, event->ctrlKey, event->altKey, event->repeat));
    return EM_TRUE; // Arrow keys and space would scroll the page otherwise.
}
} // namespace

extern "C" {
// Called from JS once the asset package is mounted. through_event is the
// inspection boundary the engine stops at; -1 is Runtime::full_campaign, which
// follows the original event chain the way the native launcher does. Anything
// past2 also decides which asset catalogues have to be mounted already.
EMSCRIPTEN_KEEPALIVE void fsb_start(const char* assets, unsigned through_event, unsigned width, unsigned height) {
    // A Fault here crossing into JS arrives as a bare "CppException", so the
    // message has to be read on this side of the boundary.
    try {
        const std::filesystem::path root(assets);
        // Presentation dimensions only scale the finished frame. As in the SDL
        // host, the EXE supplies the logical framebuffer (800x600), so authored
        // viewports and actor-relative dialogue placement see the original area.
        const auto executable = fsb::lab::read(root / "FLYINGSB.EXE");
        const auto defaults = Memory::from_pe32(executable);
        host = std::make_unique<Host>(executable, defaults.read(globals::framebuffer_width),
                                     defaults.read(globals::framebuffer_height));
        host->width = width; host->height = height;
        // The title screen can load a save on any map, so its assets are the
        // ones a boundary of9 asks for even when the caller wants less. Same
        // rule the native launcher applies for --intro.
        fsb::lab::register_runtime_assets(host->runtime, host->fonts, root, std::max(9u, through_event));
        EM_ASM({ fsbVoiceGain($0, $1); }, audio_gain_q24, std::size(audio_gain_q24));
        host->runtime.audio.set_output(&worklet);
        host->runtime.start_intro(through_event);
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_key);
        emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, nullptr, EM_TRUE, on_key);
        emscripten_set_main_loop(tick, 0, 0); // 0 fps = requestAnimationFrame
    } catch (const Fault& fault) {
        EM_ASM({ fsbFailed('Fault 0x' + ($0 >>> 0).toString(16) + ': ' + UTF8ToString($1)); },
               fault.address, fault.what());
    } catch (const std::exception& error) {
        EM_ASM({ fsbFailed(UTF8ToString($0)); }, error.what());
    }
}
// Logical milliseconds executed so far. Frames presented and samples produced
// both stay tied to real time at any rate, so this is the only figure that
// shows a hold is actually running the engine faster.
EMSCRIPTEN_KEEPALIVE unsigned fsb_logical_ms() { return host ? host->playback.now() : 0; }
// The page reports focus so a backgrounded tab does not bank simulation time.
EMSCRIPTEN_KEEPALIVE void fsb_set_active(int active) {
    if (!host) return;
    host->active = active != 0;
    // A blurred page never delivers the keyup, so the hold has to end with the
    // focus, as it does on the native host.
    if (!host->active) set_fast_forward(false);
    host->runtime.set_active(host->active);
    host->playback.elapse(0, false);
    host->previous_ms = emscripten_get_now();
    host->unspent_ms = 0;
}
}

int main() { return 0; } // Startup waits for fsb_start; assets arrive asynchronously.
