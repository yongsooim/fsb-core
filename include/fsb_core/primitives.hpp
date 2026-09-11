#pragma once
#include "random_state.hpp"
#include "scene_state.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsb::core {
using Address = std::uint32_t;
using Handle = std::uint32_t;

struct Fault : std::runtime_error {
    Address address;
    // Innermost first; collected only while an original call unwinds a fault.
    std::vector<Address> recovered_calls;
    Fault(Address address, const std::string& message);
};

// PE sections and zero-filled virtual tails are mapped explicitly. A missed
// read never becomes zero, an identity pointer, or a host dereference.
class Memory {
public:
    // A serialized observation, not a second live store of native fields.
    struct Region { Address base; std::vector<std::uint8_t> bytes; bool writable; bool allocation = false; };
    struct StorageUsage {std::size_t raw_bytes=0,native_bytes=0,logical_bytes=0,regions=0;};
    RandomState& random_state(){return random_;}
    const RandomState& random_state()const{return random_;}
    SceneState& scene_state(){return scene_;}
    const SceneState& scene_state()const{return scene_;}
    StorageUsage storage_usage()const;
    std::optional<std::size_t> allocation_size(Address base)const;
    void require_writable(Address address,std::size_t count)const;
    static Memory from_pe32(const std::vector<std::uint8_t>& file);
    void map(Address base, std::vector<std::uint8_t> bytes, bool writable);
    Address allocate_zeroed(unsigned bytes);
    void release_allocation(Address base);
    // The cached-region path is inline so the compiler can see the whole access.
    // The guest is little-endian x86, so on a little-endian host the 1/2/4-byte
    // transfer is one unaligned load or store; the byte loop remains for anything
    // else. Widths are already validated by cached(). Out-of-line copies of these
    // used to assemble the value a byte at a time.
    std::uint32_t read(Address address, unsigned width = 4) const {
        if (const auto* r = cached(address, width)) {
            const auto* at = contiguous(*r,address-r->base,width);
            if(!at)return read_slow(address,width);
            std::uint32_t value = 0;
            if constexpr (std::endian::native == std::endian::little) std::memcpy(&value, at, width);
            else for (unsigned i = 0; i < width; ++i) value |= std::uint32_t(at[i]) << (8 * i);
            return value;
        }
        return read_slow(address, width);
    }
    void write(Address address, std::uint32_t value, unsigned width = 4) {
        if (const auto* r = cached(address, width); r && r->writable) {
            auto* at = const_cast<std::uint8_t*>(contiguous(*r,address-r->base,width));
            if(!at){write_slow(address,value,width);return;}
            if constexpr (std::endian::native == std::endian::little) std::memcpy(at, &value, width);
            else for (unsigned i = 0; i < width; ++i) at[i] = std::uint8_t(value >> (8 * i));
            return;
        }
        write_slow(address, value, width);
    }
    std::vector<std::uint8_t> bytes(Address address, std::size_t count) const;
    std::span<const std::uint8_t> view(Address address, std::size_t count) const;
    // A borrowed physical run. Mixed raw/native storage must use scalar access
    // or bytes() for serialization; never materialize a second mutable store.
    std::span<std::uint8_t> span(Address address, std::size_t count);
    std::vector<Region> snapshot_regions() const;
private:
    enum class NativeField {CrtSeed,SequenceSeed,CurrentEvent,PreviousEvent,PendingEvent,ResumeEvent,CurrentMap,GameMode};
    struct Binding {std::size_t offset;unsigned size,field_offset;NativeField field;};
    struct MappedRegion {
        Address base;
        std::vector<std::uint8_t> bytes; // Packed raw bytes; native field bytes are absent.
        bool writable;
        bool allocation=false;
        std::size_t logical_size=0;
        unsigned native_bytes=0;
        std::vector<Binding> bindings;
    };
    RandomState random_;
    SceneState scene_;
    std::vector<MappedRegion> regions_;
    const std::uint32_t& native_value(NativeField field)const {
        switch (field) {
        case NativeField::CrtSeed: return random_.crt_seed;
        case NativeField::SequenceSeed: return random_.sequence_seed;
        case NativeField::CurrentEvent: return scene_.current_event;
        case NativeField::PreviousEvent: return scene_.previous_event;
        case NativeField::PendingEvent: return scene_.pending_event;
        case NativeField::ResumeEvent: return scene_.resume_event;
        case NativeField::CurrentMap: return scene_.current_map;
        case NativeField::GameMode: return scene_.game_mode;
        }
        throw std::logic_error("unknown native state field");
    }
    std::uint32_t& native_value(NativeField field) {
        return const_cast<std::uint32_t&>(static_cast<const Memory&>(*this).native_value(field));
    }
    const std::uint8_t* contiguous(const MappedRegion& region,std::size_t offset,std::size_t count)const {
        const auto raw=[&](std::size_t at){return at?region.bytes.data()+at:region.bytes.data();};
        if(region.bindings.empty())return raw(offset);
        const auto& last=region.bindings.back();
        if(offset>=last.offset+last.size)return raw(offset-region.native_bytes);
        unsigned skipped=0;
        for(const auto& field:region.bindings) {
            if(offset>=field.offset+field.size){skipped+=field.size;continue;}
            if(offset+count<=field.offset)return raw(offset-skipped);
            if(offset>=field.offset&&count<=field.size-(offset-field.offset)) {
                if constexpr(std::endian::native==std::endian::little)
                    return reinterpret_cast<const std::uint8_t*>(&native_value(field.field))+field.field_offset+offset-field.offset;
            }
            return nullptr;
        }
        return raw(offset-skipped);
    }
    std::uint8_t read_byte(const MappedRegion&,std::size_t)const;
    void write_byte(MappedRegion&,std::size_t,std::uint8_t);
    // Mappings never overlap - map() rejects that - so at most one region holds
    // an address and any region that fits is the one a scan would have found.
    // The list grows past five hundred entries in play while access alternates
    // between a handful of them, so remembering the last two removes most of a
    // linear scan per guest read and write.
    mutable std::size_t recent_[2] = {0, 0};
    mutable unsigned recent_next_ = 0;
    Address next_allocation_ = 0x30000000;
    const MappedRegion& region(Address address, std::size_t width) const;
    std::uint32_t read_slow(Address address, unsigned width) const;
    void write_slow(Address address, std::uint32_t value, unsigned width);
    const MappedRegion* cached(Address address, unsigned width) const {
        if (width != 1 && width != 2 && width != 4) return nullptr;
        for (const auto candidate : recent_) {
            if (candidate >= regions_.size()) continue;
            const auto& r = regions_[candidate];
            if (address < r.base) continue;
            const std::uint64_t offset = std::uint64_t(address) - r.base;
            if (offset <= r.logical_size && width <= r.logical_size - offset) return &r;
        }
        return nullptr;
    }
};

struct Operand {
    std::uint8_t descriptor = 0;
    std::uint32_t payload = 0;
    std::uint32_t get(const Memory& memory, Address object) const;
    void set(Memory& memory, Address object, std::uint32_t value) const;
    Address location(Address object) const;
};

struct Instruction {
    Address pc;
    std::uint8_t opcode, subop;
    std::uint16_t length;
    static Instruction decode(const Memory& memory, Address pc);
    static std::vector<Instruction> scan(const Memory& memory, Address start, Address end,
                                        bool allow_zero_alignment = false);
    Operand operand(const Memory& memory, unsigned index) const;
    Address next() const;
};

// 0x406913 has a phase gate AND a separate 16ms job counter. The caller
// supplies GetTickCount-compatible milliseconds; no wall-clock API lives here.
class FrameClock {
public:
    explicit FrameClock(std::uint32_t origin = 0) : phase_base_(origin), batch_base_(origin) {}
    void set_interval(std::uint32_t milliseconds);
    void set_fast4(bool enabled) { fast4_ = enabled; }
    unsigned advance(std::uint32_t now);
    std::uint32_t phase_base() const { return phase_base_; }
    std::uint32_t batch_base() const { return batch_base_; }
private:
    std::uint32_t phase_base_, batch_base_, interval_ = 25;
    bool fast4_ = false;
};

struct Message {
    std::uint32_t target = 0, channel = 0, code = 0, sender = 0;
};

class HsmQueue {
public:
    // Original has 32 physical slots and asserts when head catches tail.
    // Strict mode refuses overflow before corrupting the valid 31 records.
    void enqueue(Message message);
    std::optional<unsigned> find(std::uint32_t target, std::uint32_t channel = 0,
                                 std::optional<unsigned> start = std::nullopt) const;
    std::optional<unsigned> find_flagged(std::uint32_t target) const;
    Message at(unsigned slot) const;
    void remove(unsigned slot);
    void clear() { tail_ = head_; }
    unsigned size() const { return (head_ - tail_) & 31u; }
    unsigned head() const { return head_; }
    unsigned tail() const { return tail_; }
    std::vector<Message> logical_records() const;
private:
    std::array<Message, 32> slots_{};
    unsigned head_ = 0, tail_ = 0;
    bool live(unsigned slot) const;
};

std::uint32_t packed_id(const std::string& id);
std::int32_t signed32(std::uint32_t value);
// Magnitude as a32-bit bit pattern, including INT_MIN without signed overflow.
std::uint32_t signed_magnitude(std::uint32_t value);
std::uint32_t msvc_rand(std::uint32_t& state);
std::uint32_t crt_rand(Memory& memory); // Original shared stream at6d1bf0.
std::uint32_t sequence_alu(std::uint32_t selector, std::uint32_t left, std::uint32_t right);
Address compact_address(std::uint16_t index);
Handle compact_handle(const Memory& memory, Address object);
std::optional<Address> resolve_compact(const Memory& memory, Handle handle);
} // namespace fsb::core
