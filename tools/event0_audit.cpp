#include "fsb_core/vm.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>

int main(int argc, char** argv) {
    using namespace fsb::core;
    try {
        if (argc != 2&&argc!=4) { std::cerr << "Usage: fsb_event0_audit FLYINGSB.EXE [START END]\n"; return 2; }
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("cannot open EXE");
        const auto length = file.tellg(); file.seekg(0);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        file.read(reinterpret_cast<char*>(bytes.data()), length);
        const auto memory = Memory::from_pe32(bytes);
        std::map<unsigned, unsigned> histogram;
        unsigned root = 0, children = 0, external = 0;
        std::cout << "scope\tva\topcode\tsubop\tlength\timplemented_local_contract\traw_5byte_cells\n";
        std::vector<Instruction> listing;
        if(argc==4)listing=Instruction::scan(memory,Address(std::stoul(argv[2],nullptr,0)),Address(std::stoul(argv[3],nullptr,0)));
        else {listing=Instruction::scan(memory,0x61f05d,memory.read(0x6d0144));const auto main=Instruction::scan(memory,memory.read(0x6d0144),memory.read(0x6d0148),true);listing.insert(listing.end(),main.begin(),main.end());}
        for (const auto& ins : listing) {
            const Address pc = ins.pc;
            ++histogram[(unsigned(ins.opcode) << 8) | ins.subop];
            if (pc < 0x61f0ba) ++external; else if (pc < 0x61f96e) ++root; else ++children;
            std::cout << (pc < 0x61f0ba ? "external-child" : pc < 0x61f96e ? "root-region" : "child-region") << "\t0x" << std::hex << pc
                      << "\t0x" << unsigned(ins.opcode) << "\t0x" << unsigned(ins.subop) << std::dec
                      << '\t' << ins.length << '\t' << (Vm::supported(ins.opcode, ins.subop) ? "yes" : "NO") << '\t';
            for (unsigned i = 0; 4 + 5 * (i + 1) <= ins.length; ++i) {
                const auto op = ins.operand(memory, i);
                if (i) std::cout << ' ';
                std::cout << std::hex << unsigned(op.descriptor) << ':' << op.payload << std::dec;
            }
            std::cout << '\n';
        }
        std::cerr << "root-region=" << root << " child-region=" << children
                  << " external-child=" << external << " total-decoded=" << listing.size() << " distinct-op/sub=" << histogram.size()
                  << " padding-bytes=3"
                  << "\nThis is a static scan, not a successful execution/parity claim.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
