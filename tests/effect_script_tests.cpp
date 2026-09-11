#include "fsb_core/effect_script.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv) {
    try {
        if(argc!=3&&argc!=4)return 2;
        const bool native_entry=argc==4&&std::string(argv[3])=="--native-entry";
        const auto initial=Memory::from_pe32(fsb::lab::read(argv[1]));
        const auto bytes=fsb::lab::read(argv[2]);std::size_t cursor=8;
        if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBEFX1\0",8))throw std::runtime_error("bad effect fixture");
        const auto word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(cursor++))<<(8*i);return value;};
        const auto count=word();
        for(unsigned index=0;index<count;++index) {
            auto memory=initial;EffectScript effects(memory);RecoveredBattle recovered(memory);recovered.effect_scripts=&effects;
            const auto entry=word(),mask=word(),mode=word(),argc=word();std::vector<unsigned> args;
            for(unsigned i=0;i<argc;++i)args.push_back(word());
            const auto writes=word();for(unsigned i=0;i<writes;++i){const auto at=word(),width=word(),value=word();memory.write(at,value,width);}
            auto expected=memory.bytes(0x4a5000,3882100);const auto wanted=word(),changes=word();
            for(unsigned i=0;i<changes;++i){const auto at=word();expected.at(at-0x4a5000)=bytes.at(cursor++);}
            std::vector<std::pair<unsigned,unsigned>> expected_calls,calls;const auto observations=word();
            for(unsigned i=0;i<observations;++i){const auto kind=word(),value=word();expected_calls.emplace_back(kind,value);}
            effects.play_cue=[&](unsigned cue){calls.emplace_back(1, cue);};
            effects.stop_cue=[&](unsigned cue){calls.emplace_back(2, cue);};
            effects.invoke_actor=[&](Address callback,Address actor){
                if(callback!=0x100e000)throw std::runtime_error("unexpected controller callback");
                calls.emplace_back(3,actor);
                if(mode==2)memory.write(actor+actor_offset::callback_state,memory.read(actor+actor_offset::callback_state)+1);
                if(mode==3)memory.write(effect_script::controller,Actors::slot(702));
            };
            unsigned result=0;
            if(native_entry)result=recovered.invoke(entry,args);
            else if(entry==0x447841){effects.start(args[0],args[1]);result=1;}
            else if(entry==0x447898){effects.stop(args[0]);result=1;}
            else if(entry==0x4478ba)effects.tick();
            else if(entry==0x461d25)result=effects.notify_controller(args[0],signed32(args[1]));
            else {
                unsigned op=0;while(op<20&&memory.read(0x5be798+op*4)!=entry)++op;
                if(op==20)throw std::runtime_error("unknown fixture entry");
                effects.execute(args[0],static_cast<effect_script::Command>(op));
            }
            const auto actual=memory.bytes(0x4a5000,expected.size());
            if(actual!=expected||(result&mask)!=wanted||calls!=expected_calls){
                for(unsigned i=0,n=0;i<actual.size()&&n<6;++i)if(actual[i]!=expected[i]){++n;std::cerr<<"at="<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<'\n';}
                throw std::runtime_error("effect case "+std::to_string(index)+" entry="+std::to_string(entry)+" mismatch");
            }
        }
        if(cursor!=bytes.size())throw std::runtime_error("unconsumed effect fixture");
        std::cout<<"effect_script_original_cases="<<count<<" passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
