#pragma once
#include "fsb_core/recovered_battle.hpp"

// Relocate input/output buffers from original .data fixtures into the legacy
// stack. Expectations remain the original executable's bytes; this never uses
// a second port invocation as a golden result.
inline unsigned check_pointer_domains(const fsb::core::Memory& before,
    fsb::core::Address entry, const std::vector<std::uint32_t>& args,
    const std::vector<std::uint8_t>& expected, std::uint32_t return_mask,
    std::uint32_t expected_return) {
    using namespace fsb::core;
    struct Buffer { unsigned argument, bytes; };
    std::vector<Buffer> buffers;
    switch (entry) {
    case 0x448977: buffers={{0,4},{1,4}}; break;
    case 0x451b57: buffers={{0,4}}; break;
    case 0x45297e: buffers={{0,121},{1,121*8},{2,4}}; break;
    case 0x451e3f: {
        const auto cells = signed32(args[0]) > 0 && signed32(args[1]) > 0 ? args[0]*args[1] : 0;
        buffers={{2,cells},{5,4},{6,(cells+1)*12}}; break;
    }
    default: return 0;
    }
    constexpr Address data_base=0x4a5000;
    unsigned checked=0;
    for (unsigned selection=1; selection<(1u<<buffers.size()); ++selection) {
        Memory memory=before;
        RecoveredBattle code(memory);
        auto call_args=args;
        auto data_expected=expected;
        Address stack=0x01001000;
        for (unsigned i=0;i<buffers.size();++i) {
            if (!(selection&(1u<<i))) continue;
            const auto [argument,bytes]=buffers[i];
            if (stack+bytes>=0x0100d000) throw std::runtime_error("pointer fixture exceeds reserved test stack");
            call_args[argument]=stack;
            for (unsigned byte=0;byte<bytes;++byte) {
                const auto value=before.read(args[argument]+byte,1);
                code.write(stack+byte,value,1);
                data_expected.at(args[argument]-data_base+byte)=value;
            }
            stack+=(bytes+15u)&~15u;
        }
        if ((code.invoke(entry,call_args)&return_mask)!=expected_return)
            throw std::runtime_error("relocated pointer return differs from original");
        for (unsigned i=0;i<buffers.size();++i) {
            if (!(selection&(1u<<i))) continue;
            const auto [argument,bytes]=buffers[i];
            for (unsigned byte=0;byte<bytes;++byte)
                if (code.read(call_args[argument]+byte,1)!=expected.at(args[argument]-data_base+byte))
                    throw std::runtime_error("relocated pointer buffer differs from original");
        }
        if (memory.bytes(data_base,data_expected.size())!=data_expected)
            throw std::runtime_error("relocated pointer call changed unexpected game state");
        ++checked;
    }
    return checked;
}
