#include "fsb_core/primitives.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>

namespace fsb::core {
namespace {
std::string hex(Address value) { std::ostringstream s; s << "0x" << std::hex << value; return s.str(); }
std::uint32_t le(const std::vector<std::uint8_t>& data, std::size_t at, unsigned width) {
    if (at > data.size() || width > data.size() - at) throw Fault(0, "truncated binary input");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::uint32_t(data[at + i]) << (8 * i);
    return value;
}
void width_valid(unsigned width) {
    if (width != 1 && width != 2 && width != 4) throw Fault(0, "typed width must be 1, 2 or 4");
}
}
Fault::Fault(Address address, const std::string& text) : std::runtime_error(hex(address) + ": " + text), address(address) {}

Memory Memory::from_pe32(const std::vector<std::uint8_t>& file) {
    if (file.size() < 64 || file[0] != 'M' || file[1] != 'Z') throw Fault(0, "invalid MZ");
    const auto pe = le(file, 0x3c, 4);
    if (le(file, pe, 4) != 0x4550 || le(file, pe + 4, 2) != 0x14c) throw Fault(0, "expected i386 PE");
    const auto count = le(file, pe + 6, 2), opt_size = le(file, pe + 20, 2);
    const std::size_t opt = std::size_t(pe) + 24;
    if (opt_size < 96 || le(file, opt, 2) != 0x10b) throw Fault(0, "expected PE32 optional header");
    const auto base = le(file, opt + 28, 4), image_size = le(file, opt + 56, 4);
    if (!image_size || image_size > 64 * 1024 * 1024 || std::uint64_t(base) + image_size > 0x100000000ull)
        throw Fault(base, "invalid PE image size");
    Memory result;
    for (unsigned i = 0; i < count; ++i) {
        const auto section = opt + opt_size + i * 40;
        const auto virtual_size = le(file, section + 8, 4), rva = le(file, section + 12, 4);
        const auto raw_size = le(file, section + 16, 4), raw = le(file, section + 20, 4);
        const auto flags = le(file, section + 36, 4);
        const auto size = std::max(virtual_size, raw_size);
        if (!size) continue;
        if (rva > image_size || size > image_size - rva || raw > file.size() || raw_size > file.size() - raw)
            throw Fault(base, "invalid PE section bounds");
        std::vector<std::uint8_t> data(size, 0);
        std::copy_n(file.begin() + raw, raw_size, data.begin());
        result.map(base + rva, std::move(data), (flags & 0x80000000u) != 0);
    }
    if (result.regions_.empty()) throw Fault(base, "no PE sections");
    return result;
}

void Memory::map(Address base, std::vector<std::uint8_t> input, bool writable) {
    const auto end=std::uint64_t(base)+input.size();
    if(input.empty()||end>0x100000000ull)throw Fault(base,"invalid memory mapping");
    for(const auto& r:regions_)if(base<std::uint64_t(r.base)+r.logical_size&&r.base<end)throw Fault(base,"overlapping mapping");
    MappedRegion mapped{};mapped.base=base;mapped.writable=writable;mapped.logical_size=input.size();
    // Only the compatibility mapping knows the original addresses. Native
    // consumers use RandomState and SceneState. Bindings carry IDs, never host pointers, so
    // copying/replacing Memory cannot leave references into the previous state.
    if(writable)for(const auto [address,field]:{std::pair{Address(0x57fd1c),NativeField::CurrentEvent},
        std::pair{Address(0x57fd20),NativeField::PreviousEvent},
        std::pair{Address(0x57fd24),NativeField::PendingEvent},
        std::pair{Address(0x57fd28),NativeField::ResumeEvent},
        std::pair{Address(0x5aa408),NativeField::SequenceSeed},
        std::pair{Address(0x5d229c),NativeField::CurrentMap},
        std::pair{Address(0x6d1bf0),NativeField::CrtSeed},
        std::pair{Address(0x80465c),NativeField::GameMode}}) {
        const auto first=std::max<std::uint64_t>(base,address),last=std::min<std::uint64_t>(end,std::uint64_t(address)+4);
        if(first<last){mapped.bindings.push_back({std::size_t(first-base),unsigned(last-first),unsigned(first-address),field});mapped.native_bytes+=unsigned(last-first);}
    }
    if(mapped.bindings.empty())mapped.bytes=std::move(input);
    else {
        mapped.bytes.reserve(input.size()-mapped.native_bytes);std::size_t from=0;
        for(const auto& binding:mapped.bindings){mapped.bytes.insert(mapped.bytes.end(),input.begin()+from,input.begin()+binding.offset);from=binding.offset+binding.size;}
        mapped.bytes.insert(mapped.bytes.end(),input.begin()+from,input.end());
    }
    regions_.push_back(std::move(mapped));
    for(const auto& binding:regions_.back().bindings)for(unsigned i=0;i<binding.size;++i) {
        auto& value=native_value(binding.field);const auto shift=(binding.field_offset+i)*8;
        value=(value&~(0xffu<<shift))|(std::uint32_t(input[binding.offset+i])<<shift);
    }
}
Address Memory::allocate_zeroed(unsigned bytes) {
    if(!bytes||bytes>64*1024*1024)throw Fault(bytes,"invalid guest allocation size");
    std::uint64_t base=next_allocation_;
    for(;;){bool overlap=false;for(const auto& r:regions_)if(base<std::uint64_t(r.base)+r.logical_size&&r.base<base+bytes){base=(std::uint64_t(r.base)+r.logical_size+15)&~15ull;overlap=true;break;}if(!overlap)break;}
    if(base+bytes+15>0xffffffffu)throw Fault(Address(base),"guest heap address space exhausted");
    map(Address(base),std::vector<std::uint8_t>(bytes),true);regions_.back().allocation=true;
    next_allocation_=Address((base+bytes+15)&~15ull);return Address(base);
}
void Memory::release_allocation(Address base) {
    if(!base)return;
    const auto it=std::find_if(regions_.begin(),regions_.end(),[&](const auto& r){return r.base==base&&r.allocation;});
    if(it==regions_.end())throw Fault(base,"invalid guest allocation release");
    regions_.erase(it);recent_[0]=recent_[1]=0;
}
std::optional<std::size_t> Memory::allocation_size(Address base)const {
    for(const auto& r:regions_)if(r.base==base&&r.allocation)return r.logical_size;
    return std::nullopt;
}
Memory::StorageUsage Memory::storage_usage()const {
    StorageUsage out;out.regions=regions_.size();
    for(const auto& r:regions_){out.raw_bytes+=r.bytes.size();out.native_bytes+=r.native_bytes;out.logical_bytes+=r.logical_size;}
    return out;
}
std::vector<Memory::Region> Memory::snapshot_regions()const {
    std::vector<Region> snapshot;snapshot.reserve(regions_.size());
    for(const auto& r:regions_)snapshot.push_back({r.base,bytes(r.base,r.logical_size),r.writable,r.allocation});
    return snapshot;
}
const Memory::MappedRegion& Memory::region(Address address,std::size_t width)const {
    const auto holds=[&](const auto& r){if(address<r.base)return false;const auto offset=std::uint64_t(address)-r.base;return offset<=r.logical_size&&width<=r.logical_size-offset;};
    for(const auto candidate:recent_)if(candidate<regions_.size()&&holds(regions_[candidate]))return regions_[candidate];
    for(std::size_t i=0;i<regions_.size();++i)if(holds(regions_[i])){recent_[recent_next_]=i;recent_next_^=1;return regions_[i];}
    throw Fault(address,"unmapped guest memory");
}
std::uint8_t Memory::read_byte(const MappedRegion& r,std::size_t offset)const {
    if(const auto* at=contiguous(r,offset,1))return *at;
    for(const auto& b:r.bindings)if(offset>=b.offset&&offset<b.offset+b.size)return std::uint8_t(native_value(b.field)>>((b.field_offset+offset-b.offset)*8));
    throw Fault(r.base+Address(offset),"missing native field mapping");
}
void Memory::write_byte(MappedRegion& r,std::size_t offset,std::uint8_t byte) {
    if(auto* at=const_cast<std::uint8_t*>(contiguous(r,offset,1))){*at=byte;return;}
    for(const auto& b:r.bindings)if(offset>=b.offset&&offset<b.offset+b.size){auto& value=native_value(b.field);const auto shift=(b.field_offset+offset-b.offset)*8;value=(value&~(0xffu<<shift))|(std::uint32_t(byte)<<shift);return;}
    throw Fault(r.base+Address(offset),"missing native field mapping");
}
std::uint32_t Memory::read_slow(Address address,unsigned width)const {
    width_valid(width);const auto& r=region(address,width);std::uint32_t result=0;
    for(unsigned i=0;i<width;++i)result|=std::uint32_t(read_byte(r,address-r.base+i))<<(i*8);
    return result;
}
void Memory::write_slow(Address address,std::uint32_t value,unsigned width) {
    width_valid(width);const auto& checked=region(address,width);
    if(!checked.writable)throw Fault(address,"write to read-only guest region");
    auto& r=regions_[std::size_t(&checked-regions_.data())];
    for(unsigned i=0;i<width;++i)write_byte(r,address-r.base+i,std::uint8_t(value>>(i*8)));
}
void Memory::require_writable(Address address,std::size_t count)const {
    if(!region(address,count).writable)throw Fault(address,"write to read-only guest region");
}
std::span<std::uint8_t> Memory::span(Address address,std::size_t count) {
    const auto& r=region(address,count);
    if(!r.writable)throw Fault(address,"write to read-only guest region");
    const auto* data=contiguous(r,address-r.base,count);
    if(!data&&count)throw Fault(address,"range crosses separate state storage; use byte reads/writes");
    return {const_cast<std::uint8_t*>(data),count};
}
std::vector<std::uint8_t> Memory::bytes(Address address,std::size_t count)const {
    const auto& r=region(address,count);std::vector<std::uint8_t> out(count);std::size_t offset=address-r.base,done=0;
    while(done<count) {
        auto size=count-done;
        for(const auto& b:r.bindings){if(offset<b.offset){size=std::min(size,b.offset-offset);break;}if(offset<b.offset+b.size){size=std::min(size,b.offset+b.size-offset);break;}}
        if(const auto* data=contiguous(r,offset,size))std::copy_n(data,size,out.begin()+done);
        else for(std::size_t i=0;i<size;++i)out[done+i]=read_byte(r,offset+i);
        done+=size;offset+=size;
    }
    return out;
}
std::span<const std::uint8_t> Memory::view(Address address,std::size_t count)const {
    const auto& r=region(address,count);const auto* data=contiguous(r,address-r.base,count);
    if(!data&&count)throw Fault(address,"range crosses separate state storage; use bytes() to serialize");
    return {data,count};
}
Address Operand::location(Address object) const {
    if (descriptor & operand_flag::absolute_address) return payload;
    if (std::uint64_t(object) + payload > 0xffffffffu) throw Fault(object, "operand address overflow");
    return object + payload;
}
std::uint32_t Operand::get(const Memory& m, Address object) const {
    return descriptor & operand_flag::indirect ? m.read(location(object), descriptor & operand_flag::width_mask) : payload;
}
void Operand::set(Memory& m, Address object, std::uint32_t value) const {
    m.write(location(object), value, descriptor & operand_flag::width_mask);
}
Instruction Instruction::decode(const Memory& memory, Address pc) {
    if (pc > 0xfffffffbu) throw Fault(pc, "instruction header wraps address space");
    Instruction out{pc, static_cast<std::uint8_t>(memory.read(pc, 1)),
        static_cast<std::uint8_t>(memory.read(pc + 1, 1)), static_cast<std::uint16_t>(memory.read(pc + 2, 2))};
    if (out.length < 4) throw Fault(pc, "instruction is shorter than its header");
    out.next(); memory.bytes(pc, out.length);
    return out;
}
Address Instruction::next() const {
    if (std::uint64_t(pc) + length > 0xffffffffu) throw Fault(pc, "instruction length wraps address space");
    return pc + length;
}
std::vector<Instruction> Instruction::scan(const Memory& memory, Address start, Address end, bool padding) {
    if (start > end) throw Fault(start, "invalid scan range");
    std::vector<Instruction> out;
    while (start < end) {
        if (padding && end - start < 4) {
            for (Address p = start; p < end; ++p)
                if (memory.read(p, 1)) throw Fault(start, "nonzero trailing alignment bytes");
            break;
        }
        if (end - start < 4) throw Fault(start, "truncated instruction header in scan range");
        const auto instruction = decode(memory, start);
        if (instruction.next() > end) throw Fault(start, "instruction crosses declared script boundary");
        out.push_back(instruction); start = instruction.next();
    }
    return out;
}
Operand Instruction::operand(const Memory& memory, unsigned index) const {
    const std::uint64_t offset = 4 + std::uint64_t(index) * 5;
    if (offset + 5 > length) throw Fault(pc, "operand outside instruction");
    return {static_cast<std::uint8_t>(memory.read(pc + static_cast<Address>(offset), 1)),
            memory.read(pc + static_cast<Address>(offset) + 1, 4)};
}

void FrameClock::set_interval(std::uint32_t ms) {
    if (!ms || ms > 0x7fffffff) throw Fault(0, "invalid phase interval");
    interval_ = ms;
}
unsigned FrameClock::advance(std::uint32_t now) {
    const auto elapsed = now - phase_base_, interval = fast4_ ? 4u : interval_;
    if (signed32(elapsed) < static_cast<std::int32_t>(interval)) return 0;
    phase_base_ = now - elapsed % interval;
    const auto elapsed16 = now - batch_base_;
    const auto jobs = signed32(elapsed16) / 16;
    if (jobs < 0) throw Fault(0, "clock advanced more than signed GetTickCount range");
    if (jobs) batch_base_ = now - elapsed16 % 16;
    return std::min(static_cast<unsigned>(jobs), 3u);
}

bool HsmQueue::live(unsigned slot) const { return slot < 32 && ((slot - tail_) & 31u) < size(); }
void HsmQueue::enqueue(Message message) {
    if (size() == 31) throw Fault(0x403cd9, "HSM queue overflow");
    slots_[head_] = message; head_ = (head_ + 1) & 31;
}
std::optional<unsigned> HsmQueue::find(std::uint32_t target, std::uint32_t channel, std::optional<unsigned> start) const {
    auto cursor = start ? *start & 31u : tail_;
    const auto end = head_ < cursor ? head_ + 32 : head_;
    for (; cursor < end; ++cursor) {
        const auto slot = cursor & 31u;
        if (!live(slot)) continue;
        const auto& m = slots_[slot];
        if (m.target == target && (!channel || m.channel == channel) && !(m.code & 0x8000)) return slot;
    }
    return std::nullopt;
}
std::optional<unsigned> HsmQueue::find_flagged(std::uint32_t target) const {
    for (unsigned i = 0; i < size(); ++i) {
        const auto slot = (tail_ + i) & 31u;
        if (slots_[slot].target == target && (slots_[slot].code & 0x8000)) return slot;
    }
    return std::nullopt;
}
Message HsmQueue::at(unsigned slot) const {
    if (!live(slot)) throw Fault(0x403e0b, "HSM slot is not live");
    return slots_[slot];
}
void HsmQueue::remove(unsigned slot) {
    if (!live(slot)) throw Fault(0x403e55, "removing a dead HSM slot");
    if (head_ < tail_ && slot >= tail_) {
        for (unsigned i = slot; i > tail_; --i) slots_[i] = slots_[i - 1];
        // The original increments this physical cursor without masking here.
        // A transient tail==32 is valid; accesses/size still use modulo 32.
        ++tail_;
    } else {
        for (unsigned i = slot; i + 1 < head_; ++i) slots_[i] = slots_[i + 1];
        --head_;
    }
}
std::vector<Message> HsmQueue::logical_records() const {
    std::vector<Message> out;
    for (unsigned i = 0; i < size(); ++i) out.push_back(slots_[(tail_ + i) & 31u]);
    return out;
}
std::uint32_t packed_id(const std::string& id) {
    if (id.empty() || id.size() > 4) throw Fault(0, "compact ID must contain 1..4 bytes");
    std::uint32_t value = 0;
    for (unsigned char c : id) { if (c < 32 || c > 126) throw Fault(0, "non-ASCII compact ID"); value = value << 8 | c; }
    return value;
}
std::int32_t signed32(std::uint32_t value) { return value <= 0x7fffffff ? static_cast<std::int32_t>(value) : -1 - static_cast<std::int32_t>(~value); }
std::uint32_t signed_magnitude(std::uint32_t value) { return signed32(value)<0?0u-value:value; }
std::uint32_t msvc_rand(std::uint32_t& state) { return RandomState::advance_crt(state); }
std::uint32_t crt_rand(Memory& memory){return memory.random_state().next_crt();}
std::uint32_t sequence_alu(std::uint32_t selector, std::uint32_t left, std::uint32_t right) {
    const auto l = signed32(left), r = signed32(right);
    const auto bits = right & 31u;
    switch (selector) {
    case alu::equal: return left == right;
    case alu::not_equal: return left != right;
    case 2: case 4: return l < r;
    case 3: case 5: return l <= r;
    case alu::logical_and: return left && right;
    case alu::logical_or: return left || right;
    case alu::add: return left + right;
    case alu::subtract: return left - right;
    case alu::multiply: return left * right;
    case alu::signed_divide: case alu::signed_remainder:
        if (!right || (left == 0x80000000u && right == 0xffffffffu))
            throw Fault(0x41a126, "x86 signed division fault");
        return static_cast<std::uint32_t>(selector == alu::signed_divide ? l / r : l % r);
    case alu::bit_and: return left & right;
    case alu::bit_and_not: return left & ~right;
    case alu::bit_or: return left | right;
    case alu::bit_xor: return left ^ right;
    case alu::shift_left: return left << bits;
    case alu::arithmetic_shift_right: return (left >> bits) | ((bits && l < 0) ? (0xffffffffu << (32 - bits)) : 0u);
    default: throw Fault(0x41a126, "invalid sequence ALU selector");
    }
}
Address compact_address(std::uint16_t index) {
    if (index > capacity::usable_compact_slots) throw Fault(0x4026c1, "compact arena index outside 1024 slots");
    return unsigned(globals::compact_objects) + std::uint32_t(index) * unsigned(layout::compact_size);
}
Handle compact_handle(const Memory& memory, Address object) {
    if (object < unsigned(globals::compact_objects) || (object - unsigned(globals::compact_objects)) % unsigned(layout::compact_size)) throw Fault(object, "unaligned compact object");
    const auto index = (object - unsigned(globals::compact_objects)) / unsigned(layout::compact_size);
    if (index > capacity::usable_compact_slots) throw Fault(object, "compact arena index outside 1024 slots");
    return (memory.read(object + compact_offset::generation) & 0xffffu) << 16 | index;
}
std::optional<Address> resolve_compact(const Memory& memory, Handle handle) {
    const auto address = compact_address(static_cast<std::uint16_t>(handle));
    if (memory.read(address + compact_offset::generation) != (handle >> 16)) return std::nullopt;
    return address;
}
} // namespace fsb::core
