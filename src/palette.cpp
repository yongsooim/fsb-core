#include "fsb_core/palette.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>
#include <cstring>

namespace fsb::core {
std::uint8_t Palette::index(std::uint32_t color) const {
    if((color&0xff000000u)==0x01000000u)return std::uint8_t(color);
    if(!(color&0xffffffu))return 0;
    if((color&0xffffffu)==0xffffffu)return 255;
    unsigned best=0,distance=0xffffffffu;
    for(unsigned i=0;i<active_.size();++i){
        unsigned candidate=0;
        for(unsigned shift:{0u,8u,16u}){const int delta=int((color>>shift)&255)-int((active_[i]>>shift)&255);candidate+=unsigned(delta*delta);}
        if(candidate<distance){best=i;distance=candidate;if(!candidate)break;}
    }
    return std::uint8_t(best);
}
std::uint32_t Palette::gradient(Address destination,std::uint32_t from,std::uint32_t to,std::uint32_t start,std::uint32_t count){
    if(signed32(count)<=0)return count; // Original EAX on the no-loop path.
    if(start>=256||count>256-start)throw Fault(destination,"palette gradient range exceeds256 entries");
    if(count==1)throw Fault(0x404b79,"original palette gradient divides by count-1");
    std::uint32_t accumulated[3]={},delta[3];
    for(unsigned lane=0;lane<3;++lane)delta[lane]=((to>>(lane*8))&255)-((from>>(lane*8))&255);
    for(unsigned i=0;i<count;++i)for(unsigned lane=0;lane<3;++lane){
        const auto value=sequence_alu(alu::signed_divide,accumulated[lane],count-1)+((from>>(lane*8))&255);
        memory_.write(destination+(start+i)*4+lane,value,1);accumulated[lane]+=delta[lane];
    }
    return delta[2]; // Last MOV EAX at0x404b9b; flag bytes are untouched.
}
namespace {
constexpr Address work = globals::palette_work, captured = globals::palette_fade_start, final = globals::palette_fade_target;
void range(unsigned start, unsigned count) {
    if (start > 256 || count > 256 - start) throw Fault(start, "palette span outside 256 entries");
}
unsigned channel_clamp(std::int32_t value) { return value < 1 ? 0 : value > 254 ? 255 : unsigned(value); }
}
void Palette::upload(Address source, unsigned start, unsigned count, bool copy_first) {
    range(start, count);
    // 0x404c1c copies to a local buffer; 0x404bb0 changes the caller's buffer.
    for (unsigned i = 0; i < count; ++i) {
        auto entry = memory_.read(source + i * 4);
        if (windowed_mode()) {
            entry = (entry & 0x00ffffffu) | 0x05000000u;
            if (start + i == 0) entry = 0x02000000u;
            if (start + i == 255) entry = 0x020000ffu;
            if (!copy_first) memory_.write(source + i * 4, entry);
        }
        active_[start + i] = entry;
    }
}
void Palette::capture(Address destination, unsigned start, unsigned count) const {
    range(start, count);
    for (unsigned i = 0; i < count; ++i) {
        auto entry = active_[start + i];
        if (windowed_mode()) {
            if (start + i == 0) entry = 0;
            if (start + i == 255) entry = 0x00ffffff;
        }
        memory_.write(destination + i * 4, entry);
    }
}
void Palette::copy(Address destination, Address source, unsigned count) {
    range(0, count);
    const auto bytes = memory_.bytes(source, count * 4);
    for (unsigned i = 0; i < bytes.size(); ++i) memory_.write(destination + i, bytes[i], 1);
}
void Palette::scale(Address destination, Address source, std::uint32_t percent) {
    // 0x404dbb then 0x404e4c; signed 32-bit arithmetic after each wrapped operation.
    for (unsigned i = 0; i < 256; ++i) {
        memory_.write(destination + i * 4 + 3, memory_.read(source + i * 4 + 3, 1), 1);
        for (unsigned c = 0; c < 3; ++c) {
            const auto scaled = signed32(memory_.read(source + i * 4 + c, 1) * percent) / 100;
            memory_.write(destination + i * 4 + c, channel_clamp(scaled), 1);
        }
    }
}
void Palette::add_delta(Address destination, std::uint32_t delta) {
    adjust_rgb(memory_.span(destination,1024),memory_.view(destination,1024),delta);
}
std::uint32_t Palette::adjust_rgb(std::span<std::uint8_t> destination,std::span<const std::uint8_t> source,std::uint32_t delta){
    if(destination.size()!=source.size()||destination.size()%4)throw Fault(0x404e4c,"palette entries require matching four-byte spans");
    std::uint32_t last=0;
    for(std::size_t offset=0;offset<destination.size();offset+=4){
        destination[offset+3]=source[offset+3];
        for(unsigned channel=0;channel<3;++channel){
            last=channel_clamp(signed32(std::uint32_t(source[offset+channel])+delta));
            destination[offset+channel]=std::uint8_t(last);
        }
    }
    return last;
}
std::uint8_t Palette::grayscale(std::span<std::uint8_t> destination,std::span<const std::uint8_t> source){
    if(destination.size()!=source.size()||destination.size()%4)throw Fault(0x404cf0,"grayscale requires matching palette entries");
    std::uint8_t gray=0;
    for(std::size_t at=0;at<source.size();at+=4){
        if(destination.data()!=source.data())destination[at+3]=source[at+3];
        //404d66..404d87: these exact doubles are at4a2478/80/88.
        //Keep G+B before R; -ffp-contract=off retains the original rounding steps.
        const double green=double(source[at+1])*0.59,blue=double(source[at+2])*0.11;
        const double red=double(source[at])*0.3;
        gray=std::uint8_t(channel_clamp(std::int32_t((green+blue)+red)));
        destination[at]=gray;destination[at+1]=gray;destination[at+2]=gray;
    }
    return gray;
}
void Palette::copy_words(std::span<std::uint8_t> destination,std::span<const std::uint8_t> source){
    if(destination.size()!=source.size()||destination.size()%4)throw Fault(0x404ed0,"word copy requires matching DWORD spans");
    for(std::size_t at=0;at<source.size();at+=4){
        std::array<std::uint8_t,4> word;
        std::memcpy(word.data(),source.data()+at,4);
        std::memcpy(destination.data()+at,word.data(),4);
    }
}
void Palette::begin(Address target, Address initial, std::uint32_t clamp, std::uint32_t duration) {
    copy(final, target ? target : tables::black_palette);
    if (initial) copy(captured, initial); else capture(captured);
    memory_.write(globals::palette_fade_step, 0); memory_.write(globals::palette_fade_duration, duration); memory_.write(globals::palette_fade_clamp, clamp);
    if (clamp) copy(work, captured);
}
void Palette::advance(unsigned jobs) {
    if (!jobs || !busy()) return;
    do {
        const auto step = memory_.read(globals::palette_fade_step), clamp = memory_.read(globals::palette_fade_clamp);
        bool finished;
        if (!clamp) {
            const auto duration = memory_.read(globals::palette_fade_duration);
            // The original integer divide faults for zero duration. Do not turn
            // an invalid fade into an instant completion.
            if (!duration) throw Fault(0x4053ee, "linear palette fade duration is zero");
            for (unsigned i = 0; i < 256; ++i) for (unsigned c = 0; c < 3; ++c) {
                const auto offset = i * 4 + c;
                const auto from = memory_.read(captured + offset, 1);
                const auto difference = memory_.read(final + offset, 1) - from;
                const auto interpolated = sequence_alu(alu::signed_divide, difference * step, duration) + from;
                memory_.write(work + offset, interpolated, 1);
            }
            finished = step == duration;
        } else {
            unsigned changed = 0;
            for (unsigned i = windowed_mode() ? 1 : 0, end = windowed_mode() ? 255 : 256; i < end; ++i)
                for (unsigned c = 0; c < 3; ++c) {
                    const auto offset = i * 4 + c;
                    const auto to = memory_.read(final + offset, 1), before = memory_.read(work + offset, 1);
                    const auto direction = signed32(to - memory_.read(captured + offset, 1));
                    auto value = before;
                    if (direction < 0) { value -= clamp; if (signed32(value) <= signed32(to)) value = to; }
                    else if (direction > 0) { value += clamp; if (signed32(value) >= signed32(to)) value = to; }
                    changed += before != value; memory_.write(work + offset, value, 1);
                }
            finished = changed == 0; // One no-change job after reaching the target.
        }
        memory_.write(globals::palette_fade_step, finished ? 0xffffffffu : step + 1);
    } while (--jobs && busy());
    upload(work);
}
std::array<Rgb, 256> Palette::colors() const {
    std::array<Rgb, 256> result{};
    for (unsigned i = 0; i < result.size(); ++i) {
        auto entry = active_[i];
        if (entry & 0x02000000u) {
            // PC_EXPLICIT contains a hardware palette index, not an RGB red
            // byte. FSB's upload wrapper only emits the fixed black/white pins.
            const auto index = entry & 0xffff;
            if (index != 0 && index != 255) throw Fault(i, "unresolved explicit system-palette index");
            entry = index ? 0x00ffffff : 0;
        }
        result[i] = {std::uint8_t(entry), std::uint8_t(entry >> 8), std::uint8_t(entry >> 16)};
    }
    return result;
}
} // namespace fsb::core
