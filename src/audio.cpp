#if __has_include(<execinfo.h>) && !defined(__EMSCRIPTEN__)
#include <execinfo.h>
#define FSB_AUDIO_HAS_BACKTRACE 1
#endif
#include <cstdio>
#include <cstdlib>
#include "fsb_core/audio.hpp"
#include <bit>
#include <cstring>
#include "fsb_core/symbols.hpp"
#include "fsb_core/arena.hpp"
#include "audio_gain.hpp"
#include <algorithm>

namespace fsb::core {
Pcm Pcm::wave(const std::vector<std::uint8_t>& bytes) {
    const auto u32 = [&](std::size_t at) {
        if (at > bytes.size() || 4 > bytes.size() - at) throw Fault(Address(at), "truncated WAVE field");
        return bytes[at] | std::uint32_t(bytes[at+1]) << 8 | std::uint32_t(bytes[at+2]) << 16 | std::uint32_t(bytes[at+3]) << 24;
    };
    if (bytes.size() < 12 || u32(0) != 0x46464952 || u32(8) != 0x45564157) throw Fault(0, "invalid RIFF WAVE");
    Pcm result; std::size_t data_at = 0, data_size = 0;
    // Original43564c/4354a8 locate fmt/data without checking the outer RIFF
    // length. DOOR01's declaration exceeds its intact PCM resource by one byte.
    // Keep physical chunk bounds strict; accepting that header is not padding PCM.
    const auto limit = bytes.size();
    for (std::size_t at = 12; at + 8 <= limit;) {
        const auto tag = u32(at), size = u32(at + 4); at += 8;
        if (size > limit - at) throw Fault(Address(at), "WAVE chunk exceeds RIFF");
        if (tag == 0x20746d66) {
            if (size < 16 || (u32(at) & 65535) != 1) throw Fault(Address(at), "only PCM WAVE is implemented");
            result.channels = u32(at) >> 16; result.rate = u32(at + 4);
            const auto layout = u32(at + 12); result.bits = layout >> 16;
            if ((result.channels != 1 && result.channels != 2) || (result.bits != 8 && result.bits != 16) ||
                !result.rate || result.rate > 192000 || (layout & 65535) != result.channels * result.bits / 8 || u32(at + 8) != result.rate * result.channels * result.bits / 8)
                throw Fault(Address(at), "unsupported/inconsistent WAVE format");
        } else if (tag == 0x61746164) {
            data_at = at; data_size = size;
            // Original parsers consume fmt/data and do not validate later
            // metadata. SP_THUNDER02 has an unpadded odd data chunk followed
            // by LIST; rejecting that trailer would reject a real game asset.
            if (result.channels) break;
        }
        at += size + (size & 1);
    }
    if (!result.channels || !data_at || !data_size) throw Fault(0, "missing/incomplete PCM data");
    // MIRO.WAV contains an odd stereo8 data count. Retain original byte-count
    // metadata; the portable mixer emits only complete sample frames (one byte
    // is excluded here). This does not claim DirectSound's trailing-byte DSP.
    const auto frame_bytes=result.channels*result.bits/8;
    const auto usable=data_size-data_size%frame_bytes;
    if(!usable)throw Fault(0,"WAVE has no complete PCM frame");
    result.byte_count = unsigned(data_size);
    const auto count = usable / (result.bits / 8);
    result.samples.resize(count);
    if (result.bits == 8) {
        for (std::size_t i = 0; i < count; ++i)
            result.samples[i] = std::int16_t((int(bytes[data_at + i]) - 128) * 256);
    } else if constexpr (std::endian::native == std::endian::little) {
        // 16-bit WAVE is little-endian signed, which is exactly what we store.
        std::memcpy(result.samples.data(), bytes.data() + data_at, usable);
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            const auto raw = unsigned(bytes[data_at + i * 2]) | unsigned(bytes[data_at + i * 2 + 1]) << 8;
            result.samples[i] = std::int16_t(raw < 32768 ? int(raw) : int(raw) - 65536);
        }
    }
    return result;
}
void Audio::ready() const { if (!initialized_) throw Fault(0x433903, "portable audio is not initialized"); }
void Audio::initialize(std::uint32_t flags) {
    voices_ = {}; events_.clear(); output_sample_ = 0; now_ = 0;
    memory_.write(0x76e8fc, flags);
    for (Address at = 0x7697f8; at < 0x76e7f8; at += 40) {
        for (unsigned i = 0; i < 40; i += 4) memory_.write(at + i, 0);
        memory_.write(at + 36, 0xffffffff);
    }
    for (Address at = 0x769660; at < 0x7697f8; at += 4) memory_.write(at, 0);
    memory_.write(0x76e908, 1); memory_.write(0x76e910, 1);
    memory_.write(0x76e90c, 100); memory_.write(0x76e914, 100); memory_.write(0x76e918, 100);
    memory_.write(0x76ea28, 0); initialized_ = true;if(output_)set_output(output_);
}
void Audio::register_wave(bool bgm, unsigned id, const std::vector<std::uint8_t>& bytes) {
    if (id >= (bgm ? 59u : 362u)) throw Fault(id, "audio id outside catalog");
    (bgm ? bgm_assets_ : cue_assets_)[id] = std::make_shared<Pcm>(Pcm::wave(bytes));
}
std::string Audio::resource_name(const Memory& m, bool bgm, unsigned id) {
    if (id >= (bgm ? 59u : 362u)) throw Fault(id, "audio id outside catalog");
    const auto at = m.read((bgm ? tables::bgm_resources : tables::sound_cue_resources) + id * (bgm ? 4 : 16));
    if (!at) return {};
    std::string result;
    for (unsigned i = 0; i < 260; ++i) { const auto ch = m.read(at + i, 1); if (!ch) return result; if (ch != '%' && ch != '$') result.push_back(char(ch)); }
    throw Fault(at, "unterminated audio resource name");
}
int Audio::volume_millibels(unsigned percent) {
    const int inverse = 100 - int(std::min(percent, 100u));
    const int mb = -(inverse * inverse * 3 / 10);
    return mb == -3000 ? -10000 : mb; // x87 body at0x43416c..434194.
}
void Audio::emit(AudioEvent::Kind kind, unsigned slot) {
    const auto& voice = voices_[slot]; events_.push_back({kind, slot == 16, voice.id, voice.handle, output_sample_, volume_millibels(voice.volume)});
    if(output_)output_->apply(kind,slot,voice);
}
void Audio::create_voice(unsigned slot, unsigned id, std::shared_ptr<const Pcm> pcm) {
    auto& voice = voices_[slot]; if (voice.playing) { voice.playing = false; emit(AudioEvent::Kind::Stop, slot); }
    const auto handle = memory_.read(0x76e904); memory_.write(0x76e904, handle + 1);
    voice = {}; voice.pcm = std::move(pcm); voice.id = id; voice.handle = handle;
    memory_.write(0x769660 + slot * 24, handle); memory_.write(0x769668 + slot * 24, voice.pcm->byte_count);
    emit(AudioEvent::Kind::Load, slot);
}
void Audio::start_voice(unsigned slot, bool loop, unsigned priority) {
    auto& voice = voices_[slot]; if (!voice.pcm) throw Fault(slot, "play without loaded PCM");
    if (voice.phase >= voice.pcm->frames() * 44100ull) voice.phase = 0;
    voice.loop = loop; voice.priority = priority; voice.playing = true;
    memory_.write(0x76966c + slot * 24, priority); memory_.write(0x769670 + slot * 24, loop);
    emit(AudioEvent::Kind::Play, slot);
}
void Audio::load_bgm(unsigned id) {
    ready(); if (id < 1 || id > 58) { stop_bgm(); return; }
    if (memory_.read(0x769654) != id) {
        if (resource_name(memory_, true, id).empty()) return;
        const auto asset = bgm_assets_.find(id); if (asset == bgm_assets_.end()) throw Fault(id, "BGM PCM asset is missing");
        create_voice(16, id, asset->second); memory_.write(0x769654, id); memory_.write(0x769658, 1);
    }
    if (!voices_[16].pcm) throw Fault(id, "BGM logical id has no backend PCM");
    seek_bgm(0);set_bgm_loop(1);voices_[16].loop=true;
}
void Audio::select_bgm(unsigned id,std::uint32_t loop_mode,bool play_now){
    // Keep the original32-bit loop field; nonzero modes must not be normalized
    // in guest memory even though the output sink consumes a boolean loop.
    if(id<1||id>58)stop_bgm();else{load_bgm(id);memory_.write(0x76e91c,loop_mode);if(play_now)play_bgm();}
}
void Audio::play_bgm() { ready(); if (!memory_.read(0x76e910)) return; subvolume(memory_.read(0x76e918)); start_voice(16, memory_.read(0x76e91c) != 0, 0); }
void Audio::seek_bgm(std::uint32_t position) {
    ready();auto& voice=voices_[16];
    //434f04's range diagnostic preserves its previous HRESULT. The buffer
    //query reports8898000d when no buffer exists; neither condition is a VM fault.
    if(position>memory_.read(0x7697e8))return;
    if(!voice.pcm){memory_.write(0x76ea28,0x8898000du);return;}
    const bool playing=voice.playing;
    if(playing)stop_bgm();
    if(position>=voice.pcm->byte_count){memory_.write(0x76ea28,0x80070057u);return;}
    const auto block=voice.pcm->channels*(voice.pcm->bits/8);
    voice.phase=(std::uint64_t(position/block)%voice.pcm->frames())*44100;
    if(playing)start_voice(16,memory_.read(0x76e91c)!=0,0);
    memory_.write(0x76ea28,0);
}
void Audio::set_bgm_loop(std::uint32_t mode){
    ready();
    if(memory_.read(0x76e91c)!=mode){
        auto& voice=voices_[16];
        if(!voice.pcm){memory_.write(0x76ea28,0x8898000du);return;}
        if(voice.playing){
            // Original434dad stops/plays the same DirectSound buffer without
            // seeking. Each output backend retains its own device-time cursor.
            voice.loop=mode!=0;voice.priority=0;
            memory_.write(0x76966c+16*24,0);memory_.write(0x769670+16*24,voice.loop);
            emit(AudioEvent::Kind::Loop,16);
        }
        memory_.write(0x76e91c,mode);
    }
    memory_.write(0x76ea28,0);
}
void Audio::stop_bgm() { ready(); if (voices_[16].playing) { voices_[16].playing = false; emit(AudioEvent::Kind::Stop, 16); }else if(output_)output_->apply(AudioEvent::Kind::Stop,16,voices_[16]); }
bool Audio::bgm_playing() const { ready(); return voices_[16].playing; }
void Audio::apply_bgm(unsigned id) { stop_bgm(); subvolume(100); if (id) { if (id == 999) id = memory_.read(0x5c4f58 + memory_.read(globals::current_map_id) * 0x44); load_bgm(id); if (voices_[16].pcm) play_bgm(); } }
void Audio::subvolume(std::uint32_t percent) {
    ready(); if (percent > 100) return; // Script guard logs and preserves prior volume.
    memory_.write(0x76e918, percent);
    voices_[16].volume = std::min(100u, percent * memory_.read(0x76e914) / 100);
    memory_.write(0x769674 + 16 * 24, std::uint32_t(volume_millibels(voices_[16].volume)));
    if (voices_[16].pcm) emit(AudioEvent::Kind::Volume, 16);
}
void Audio::master_volume(bool bgm,int requested){
    ready();const auto percent=unsigned(std::clamp(requested,0,100));
    if(bgm){
        const auto before=memory_.read(0x76e914);if(!percent&&before)stop_bgm();else if(percent&&!before&&memory_.read(0x76e91c))play_bgm();
        memory_.write(0x76e914,percent);subvolume(memory_.read(0x76e918));
    }else{
        memory_.write(0x76e90c,percent);
        for(unsigned i=0;i<16;++i){auto& voice=voices_[i];if(!percent){if(voice.playing){voice.playing=false;emit(AudioEvent::Kind::Stop,i);}else if(output_)output_->apply(AudioEvent::Kind::Stop,i,voice);}
            else if(voice.pcm&&voice.loop){voice.volume=percent;memory_.write(0x769674+i*24,std::uint32_t(volume_millibels(percent)));emit(AudioEvent::Kind::Volume,i);}}
    }
    memory_.write(0x76ea28,0);
}
void Audio::cancel_fade() {
    ready(); const auto object = resolve_compact(memory_, memory_.read(0x768a84));
    if (object) { subvolume(memory_.read(*object + 0xec)); if (!memory_.read(*object + 0xf0)) stop_bgm(); Arena(memory_).release(compact_handle(memory_, *object)); memory_.write(0x768a84, 0); }
    memory_.write(0x768a9c, 0);
}
void Audio::fade(std::uint32_t duration, std::uint32_t start, std::uint32_t target, bool keep) {
    ready(); const auto handle = Arena(memory_).allocate_after(memory_.read(globals::group1_append_link), routines::audio_fade_tick, 0x30000, 0);
    const auto object = *resolve_compact(memory_, handle);
    memory_.write(object + 0x100, duration); memory_.write(object + 0xe8, start); memory_.write(object + 0xec, target); memory_.write(object + 0xf0, keep);
    memory_.write(0x768a84, handle); memory_.write(0x768a9c, 1);
}
void Audio::tick_fade(Address object, unsigned jobs) {
    const auto duration = memory_.read(object + 0x100); auto elapsed = memory_.read(object + 0x2c) + jobs;
    if (signed32(elapsed) >= signed32(duration)) elapsed = duration;
    memory_.write(object + 0x2c, elapsed); const auto start = memory_.read(object + 0xe8);
    subvolume(sequence_alu(alu::signed_divide, (memory_.read(object + 0xec) - start) * elapsed, duration) + start);
    if (elapsed == duration) { if (!memory_.read(object + 0xf0)) stop_bgm(); memory_.write(0x768a9c, 0); memory_.write(0x768a84, 0); memory_.write(object + 0x20, 0xffffffff); }
}
std::optional<unsigned> Audio::find_handle(unsigned handle) const {
    // Original lookup checks the handle column before querying a buffer; even
    // an empty slot can match zero and then report stopped at the status query.
    for (unsigned i = 0; i < voices_.size(); ++i) if (memory_.read(0x769660 + i * 24) == handle) return i;
    return std::nullopt;
}
void Audio::play_cue(unsigned id) {
    if (std::getenv("FSB_CUE_TRACE")) {
        std::fprintf(stderr, "CUE %u\n", id);
#if defined(FSB_AUDIO_HAS_BACKTRACE)
        void* frames[64];
        const int count = backtrace(frames, 64);
        backtrace_symbols_fd(frames, count, 2);
#endif
    }
    ready(); if (id >= 362) return;
    if (!memory_.read(0x76f258 + id * 4)) {
        if (resource_name(memory_, false, id).empty()) return;
        const auto asset = cue_assets_.find(id); if (asset == cue_assets_.end()) throw Fault(id, "cue PCM asset is missing");
        memory_.write(0x76f258 + id * 4, asset->second->byte_count); memory_.write(0x76fa58 + id * 4, 0);
        memory_.write(0x770258 + id * 4, now_); memory_.write(0x770a58, memory_.read(0x770a58) + asset->second->byte_count);
        const auto& pcm = *asset->second; const auto base = 0x7697f8 + id * 40;
        memory_.write(base, 1 | (pcm.channels << 16)); memory_.write(base + 4, pcm.rate);
        memory_.write(base + 8, pcm.rate * pcm.channels * pcm.bits / 8);
        memory_.write(base + 12, pcm.channels * pcm.bits / 8 | (pcm.bits << 16)); memory_.write(base + 16, 0, 2);
        memory_.write(base + 20, pcm.byte_count); memory_.write(base + 24, memory_.read(0x5ad38c + id * 16));
        memory_.write(base + 28, memory_.read(0x5ad390 + id * 16) != 0);
    }
    if(const auto handle=play_cached_cue(id))memory_.write(0x76ea58+id*4,*handle);
    memory_.write(0x76fa58 + id * 4, memory_.read(0x76fa58 + id * 4) + 1); memory_.write(0x770258 + id * 4, now_); trim_cache();
}
std::optional<unsigned> Audio::play_cached_cue(unsigned id) {
    ready();memory_.write(0x76ea28,0);
    //434584 consumes an already loaded PCM source. It does not perform
    //435373's resource load, request counting, timestamp update or trimming.
    //The portable source-cache size is the loaded flag (no host PCM pointer
    //is placed in guest memory). An unloaded or disabled slot is a no-op.
    if(id>=362||!memory_.read(0x76e908)||!memory_.read(0x76f258+id*4))return std::nullopt;
    {
        auto selected = (memory_.read(0x76e8fc) & 2) ? find_handle(memory_.read(0x76981c + id * 40)) : std::nullopt;
        if (selected) { if (!voices_[*selected].pcm) throw Fault(*selected, "PCM replay matched an empty buffer"); voices_[*selected].phase = 0; voices_[*selected].playing = true; emit(AudioEvent::Kind::Play, *selected); }
        else {
            std::optional<unsigned> reclaim; std::uint64_t fewest = 0xffffffff;
            const auto priority = memory_.read(0x769810 + id * 40);
            for (unsigned i = 0; i < 16; ++i) {
                const auto& v = voices_[i];
                if (!v.pcm || !v.playing) { selected = i; break; }
                if (!v.loop && (memory_.read(0x76e8fc) & 4) && priority < v.priority) {
                    const auto remaining = v.pcm->byte_count - v.phase / 44100 * v.pcm->channels * v.pcm->bits / 8;
                    if (remaining < fewest) { reclaim = i; fewest = remaining; }
                }
            }
            if (!selected) selected = reclaim;
            if (!selected) {
                //4341d0 returns DSBUFFERERR_NOT_ENOUGH_SLOTS. With the
                //original runtime flags4,435218 shows no error dialog;
                //435373 ignores the HRESULT and retains the previous handle.
                memory_.write(0x76ea28,0x8898000eu);return std::nullopt;
            }
            create_voice(*selected, id, cue_assets_.at(id)); voices_[*selected].volume = std::min(100u, memory_.read(0x76e90c));
            memory_.write(0x769674 + *selected * 24, std::uint32_t(volume_millibels(voices_[*selected].volume)));
            start_voice(*selected, memory_.read(0x769814 + id * 40) != 0, priority);
            memory_.write(0x76981c + id * 40, voices_[*selected].handle);return voices_[*selected].handle;
        }
    }
    //The original replay-existing-buffer branch leaves out_handle untouched.
    return std::nullopt;
}
void Audio::stop_cue(unsigned id) {
    ready(); if (id >= 362 || !memory_.read(0x76f258 + id * 4)) return;
    const auto slot = find_handle(memory_.read(0x76ea58 + id * 4));
    if (slot && voices_[*slot].playing) { voices_[*slot].playing = false; emit(AudioEvent::Kind::Stop, *slot); }
    else if(slot&&output_)output_->apply(AudioEvent::Kind::Stop,*slot,voices_[*slot]);
}
bool Audio::cue_finished(unsigned id) const {
    ready(); if (id >= 362 || !memory_.read(0x76f258 + id * 4)) return true;
    const auto slot = find_handle(memory_.read(0x76ea58 + id * 4)); return !slot || !voices_[*slot].playing;
}
void Audio::trim_cache() {
    while (signed32(memory_.read(0x770a58)) > signed32(memory_.read(0x5ad380))) {
        auto oldest = now_; std::optional<unsigned> id;
        for (unsigned i = 0; i < 362; ++i) if (memory_.read(0x76f258 + i * 4) && signed32(memory_.read(0x770258 + i * 4)) < signed32(oldest)) { id = i; oldest = memory_.read(0x770258 + i * 4); }
        if (!id) return;
        memory_.write(0x770a58, memory_.read(0x770a58) - memory_.read(0x76f258 + *id * 4));
        memory_.write(0x76f258 + *id * 4, 0); memory_.write(0x76fa58 + *id * 4, 0); memory_.write(0x770258 + *id * 4, 0);
        // The source PCM cache is freed, while already copied playback survives.
    }
}
std::vector<std::int16_t> Audio::mix(unsigned count) {
    ready(); if (count > 44100 * 60) throw Fault(count, "audio output batch exceeds60 seconds");
    std::vector<std::int16_t> output(std::size_t(count) * 2);
    for (unsigned frame = 0; frame < count; ++frame) {
        std::int64_t sum[2]{};
        for (unsigned slot = 0; slot < voices_.size(); ++slot) {
            auto& v = voices_[slot]; if (!v.playing) continue;
            const auto& pcm = *v.pcm; const auto index = v.phase / 44100, fraction = v.phase % 44100;
            const auto next = index + 1 < pcm.frames() ? index + 1 : v.loop ? 0 : index;
            for (unsigned channel = 0; channel < 2; ++channel) {
                const auto ch = pcm.channels == 1 ? 0 : channel;
                const auto a = pcm.samples[index * pcm.channels + ch], b = pcm.samples[next * pcm.channels + ch];
                const auto sample = std::int64_t(a) + (std::int64_t(b) - a) * std::int64_t(fraction) / 44100;
                sum[channel] += sample * audio_gain_q24[v.volume] / 16777216;
            }
            v.phase += pcm.rate;
            if (v.phase >= pcm.frames() * 44100ull) {
                if (v.loop) v.phase %= pcm.frames() * 44100ull;
                else { v.playing = false; emit(AudioEvent::Kind::Complete, slot); events_.back().output_sample = output_sample_ + 1; }
            }
        }
        for (unsigned channel = 0; channel < 2; ++channel) output[frame * 2 + channel] = std::int16_t(std::clamp<std::int64_t>(sum[channel], -32768, 32767));
        ++output_sample_;
    }
    return output;
}
} // namespace fsb::core
