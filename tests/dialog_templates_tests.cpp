#include "fsb_core/dialog_templates.hpp"
#include "fsb_core/recovered_battle.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
namespace {
constexpr Address trace=0x6dc000,config=0x6ddb00,out=0x6ddd00,published=0x6dde00;
unsigned service(Memory& m,Address entry,const std::vector<std::uint32_t>& args){
    const auto count=m.read(trace);std::vector<std::uint32_t> values{entry,unsigned(args.size())};
    values.insert(values.end(),args.begin(),args.end());while(values.size()<5)values.push_back(0);
    for(auto a:{0x7686bcu,out,out+4,out+8,out+12})values.push_back(m.read(a));
    for(unsigned i=0;i<values.size();++i)m.write(trace+4+count*48+i*4,values[i]);m.write(trace,count+1);
    if(m.read(config)){m.write(0x7686bc,m.read(0x7686bc)^0x80000000);m.write(out+12,99);}
    return entry==0x457e56?2:m.read(config+4);
}
}
int main(int argc,char** argv){
 try{
    if(argc!=3&&argc!=4)return 2;const bool direct=argc==4;
    const auto initial=fsb::lab::read_guest_snapshot(argv[1]);const auto bytes=fsb::lab::read(argv[2]);
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBACT1\0",8))throw std::runtime_error("invalid dialog template fixture");
    std::size_t cursor=8;const auto word=[&]{std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(8*i);return v;};
    const auto total=word();
    for(unsigned index=0;index<total;++index){
        Memory m=initial;RecoveredBattle code(m);const auto entry=word(),mask=word();auto n=word();std::vector<std::uint32_t> args;
        for(unsigned i=0;i<n;++i)args.push_back(word());
        n=word();for(unsigned i=0;i<n;++i){const auto a=word(),width=word(),value=word();code.write(a,value,width);}
        auto expected=m.bytes(0x4a5000,3882100);const auto expected_return=word();n=word();
        for(unsigned i=0;i<n;++i){const auto a=word();expected.at(a-0x4a5000)=bytes.at(cursor++);}
        std::uint32_t returned=0;
        if(direct){
            const auto row=DialogTemplates::find_static(args[0]);if(!row)throw std::runtime_error("fixture selector is not a native static row");
            DialogTemplateServices services;
            services.observe_player=[&]{service(m,0x457e56,{});};
            services.read_substate=[&]{return code.read(args[5]);};
            services.create=[&](unsigned id,Address blob,unsigned slot){return service(m,0x4139b6,{id,blob,slot});};
            services.facing=[&](unsigned v){if(args[2])code.write(args[2],v);};
            services.menu_context=[&](unsigned v){if(args[3])code.write(args[3],v);};
            services.side_effect=[&](unsigned v){if(args[4])code.write(args[4],v);};
            returned=DialogTemplates(m).expand_static(*row,args[1],services);
        }else{
            code.service=[&](Address entry,RecoveredBattle& call){
                if(entry!=0x4139b6)return false;
                call.result(service(m,entry,{call.argument(0),call.argument(1),call.argument(2)}),12);return true;
            };
            returned=code.invoke(entry,args);
        }
        for(unsigned i=0;i<4;++i)m.write(published+i*4,args[i+2]?code.read(args[i+2]):0xfeedface);
        const auto actual=m.bytes(0x4a5000,expected.size());
        if((returned&mask)!=expected_return||actual!=expected){
            std::cerr<<"case="<<index<<" entry="<<std::hex<<entry<<" result="<<returned<<" expected="<<expected_return<<'\n';
            for(unsigned i=0;i<actual.size();++i)if(actual[i]!=expected[i]){std::cerr<<"first_difference="<<std::hex<<0x4a5000+i<<" actual="<<unsigned(actual[i])<<" expected="<<unsigned(expected[i])<<'\n';break;}return 1;}
    }
    if(cursor!=bytes.size())throw std::runtime_error("trailing dialog template bytes");
    std::cout<<"dialog_template_cases="<<total<<" direct="<<direct<<" matched original data, callbacks and context mutations\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
