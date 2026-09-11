#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=4)return 2;
        const auto executable=fsb::lab::read(argv[1]),reference=fsb::lab::read(argv[3]);
        const auto initial=fsb::lab::read_guest_snapshot(argv[2]);
        if(reference.size()<12||std::string(reference.begin(),reference.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid runtime gap oracle");
        std::size_t cursor=8;
        const auto word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(reference.at(cursor++))<<(i*8);return value;};
        const auto count=word();std::uint64_t compared=0;
        for(unsigned index=0;index<count;++index){
            Runtime runtime(executable);runtime.memory=initial;auto& memory=runtime.memory;
            const auto entry=word(),mask=word(),arguments=word();std::vector<std::uint32_t> args;
            for(unsigned i=0;i<arguments;++i)args.push_back(word());
            const auto writes=word();
            for(unsigned i=0;i<writes;++i){const auto address=word(),width=word(),value=word();memory.write(address,value,width);}
            auto expected=memory.bytes(0x4a5000,3882100);const auto expected_return=word(),changes=word();
            for(unsigned i=0;i<changes;++i){const auto address=word();expected.at(address-0x4a5000)=reference.at(cursor++);}
            unsigned returned=0;
            if(entry==0x402832){
                ObjectPump pump(memory,runtime.arena,[&](Address callback,Address object,unsigned){
                    if(callback!=0x100e000)throw std::runtime_error("unexpected isolated callback");
                    memory.write(object+compact_offset::lifecycle,0);
                });
                pump.update_object(args.at(0),1);
            }else if(entry==0x4490c7){
                runtime.actors.flood_costs(args[0],signed32(args[5]),signed32(args[6]),args[7],{signed32(args[1]),signed32(args[2]),signed32(args[3]),signed32(args[4])});
            }else returned=runtime.battle.recovered.invoke(entry,args)&mask;
            const auto actual=memory.bytes(0x4a5000,expected.size());compared+=actual.size();
            if(actual!=expected||returned!=expected_return){
                for(unsigned i=0,n=0;i<actual.size()&&n<8;++i)if(actual[i]!=expected[i]){
                    ++n;std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" at="<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<std::dec<<'\n';
                }
                std::cerr<<"return="<<returned<<" expected="<<expected_return<<'\n';return 1;
            }
        }
        if(cursor!=reference.size())throw std::runtime_error("unconsumed oracle records");
        // Native wrappers and normal core users share exactly one HSM queue.
        Runtime runtime(executable);auto& call=runtime.battle.recovered;auto& memory=runtime.memory;
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        for(unsigned i=0;i<31;++i)runtime.messages.enqueue({77,88,i,99});
        for(unsigned i=0;i<30;++i)runtime.messages.remove(runtime.messages.tail());
        call.invoke(0x403cd9,{77,88,123,456});call.invoke(0x403cd9,{77,88,0x8000|3,789});
        const auto found=call.invoke(0x403d49,{77,88,0xffffffff});
        const auto out=memory.allocate_zeroed(16);call.invoke(0x403e0b,{out,found});
        check(memory.read(out+8)==30&&memory.read(out+12)==99,"HSM native read bypassed the shared queue");
        check(call.invoke(0x403ed7,{77,88,123})==1&&runtime.messages.size()==2,"wrapped HSM consume lost records");
        const auto flagged=call.invoke(0x403dbd,{77});check(runtime.messages.at(flagged).sender==789,"flagged HSM lookup failed");
        call.invoke(0x403e55,{flagged});check(call.invoke(0x403dbd,{77})==0xffffffff,"missing HSM result is not the original sentinel");
        const auto allocation=call.invoke(0x401151,{8,4});memory.write(allocation,0xa1b2c3d4);
        const auto resized=call.invoke(0x401188,{allocation,64});check(memory.read(resized)==0xa1b2c3d4&&memory.read(resized+28)==0,"realloc failed to preserve calloc bytes");
        check(call.invoke(0x401188,{resized,0})==0,"zero-size realloc did not release");
        const std::array<std::uint16_t,8> observation{1998,9,3,10,21,35,17,456};
        runtime.environment.local_time=[&]{return observation;};call.invoke(0x859484,{out});
        for(unsigned i=0;i<8;++i)check(memory.read(out+i*2,2)==observation[i],"host time word layout changed");
        const auto rectangle=memory.allocate_zeroed(16);for(unsigned i=0;i<4;++i)memory.write(rectangle+i*4,i<2?10:20);
        call.invoke(0x8594ec,{rectangle,2,3});check(memory.read(rectangle)==8&&memory.read(rectangle+4)==7&&memory.read(rectangle+8)==22&&memory.read(rectangle+12)==23,"InflateRect boundary mismatch");
        check(call.invoke(0x859500,{rectangle,8,7})==1&&call.invoke(0x859500,{rectangle,22,23})==0,"PtInRect must exclude right and bottom edges");
        call.invoke(0x859554,{0,0x406,123,456});check(runtime.pending_input_count()==1,"PostMessage bypassed the portable queue");
        const std::pair<std::uint64_t,std::uint64_t> square_roots[]={{0,0},{0x8000000000000000ull,0x8000000000000000ull},{0x4022000000000000ull,0x4008000000000000ull},{0x7ff0000000000000ull,0x7ff0000000000000ull},{0xbff0000000000000ull,0xfff8000000000000ull},{0x7ff8000000000000ull,0x7ff8000000000000ull},{0xfff0000000000000ull,0xfff8000000000000ull}};
        for(const auto [input,expected]:square_roots){
            check(std::bit_cast<std::uint64_t>(RecoveredBattle::x87_square_root(std::bit_cast<double>(input)))==expected,"original sqrt result bits changed");
            memory.write(globals::crt_math_errno,0);call.invoke(0x498174,{std::uint32_t(input),std::uint32_t(input>>32)});
            const bool domain=input==0xbff0000000000000ull||input==0x7ff8000000000000ull||input==0xfff0000000000000ull;
            check(memory.read(globals::crt_math_errno)==(domain?0x21u:0u),"original sqrt guest errno changed");
        }
        std::cout<<"runtime_gap_original_cases="<<count<<" guest_bytes_compared="<<compared<<" shared_HSM_and_heap=passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
