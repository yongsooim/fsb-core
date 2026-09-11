#include "fsb_core/primitives.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv) {
    try {
        if(argc!=3)return 2;
        auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));RecoveredBattle code(memory);
        const auto bytes=fsb::lab::read(argv[2]);std::size_t cursor=8;
        if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBRNG1\0",8))throw std::runtime_error("invalid RNG fixture");
        const auto word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(cursor++))<<(8*i);return value;};
        const auto groups=word();unsigned total=0;
        for(unsigned group=0;group<groups;++group) {
            auto state=word();const auto draws=word();memory.write(0x6d1bf0,state);
            auto expected_data=memory.bytes(0x4a5000,3882100);
            for(unsigned draw=0;draw<draws;++draw) {
                const auto expected=word(),next_state=word();
                // Alternate old callers and native consumers to check one shared stream.
                const auto actual=draw%2?crt_rand(memory):code.invoke(0x498090);
                if(actual!=expected||memory.read(0x6d1bf0)!=next_state||msvc_rand(state)!=expected||state!=next_state)throw std::runtime_error("original RNG stream mismatch");
                ++total;
            }
            for(unsigned i=0;i<4;++i)expected_data[0x6d1bf0-0x4a5000+i]=std::uint8_t(state>>(8*i));
            if(memory.bytes(0x4a5000,expected_data.size())!=expected_data)throw std::runtime_error("RNG changed unrelated game data");
        }
        if(cursor!=bytes.size())throw std::runtime_error("unconsumed RNG fixture");
        std::cout<<"original_rng_draws="<<total<<" shared stream and scalar paths passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
