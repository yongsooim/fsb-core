#include "fsb_core/markup.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=3)return 2;
        auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));const auto data=fsb::lab::read(argv[2]);unsigned cursor=8;
        if(data.size()<12||std::string(data.begin(),data.begin()+8)!="FSBMARK1")throw std::runtime_error("invalid markup oracle");
        const auto u32=[&](){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(data.at(cursor++))<<(i*8);return v;};
        const auto count=u32();unsigned failures=0;
        for(unsigned i=0;i<count;++i){
            const auto size=u32(),address=memory.allocate_zeroed(size+1);std::string label;
            for(unsigned j=0;j<size;++j){const auto c=data.at(cursor++);memory.write(address+j,c,1);label.push_back(char(c));}
            const auto code=u32(),length=u32(),arg=u32(),aux=u32(),flags=u32();const bool relative=(flags&1)!=0,ignored=(flags&2)!=0;const auto token=tokenize_markup(memory,address);
            if(token.code!=code||token.bytes!=length||(!ignored&&token.argument!=arg+(relative?address:0))||token.auxiliary!=aux){
                ++failures;std::cerr<<"FAIL "<<label<<" got("<<token.code<<','<<token.bytes<<','<<token.argument-(relative?address:0)<<','<<token.auxiliary<<") original("<<code<<','<<length<<','<<arg<<','<<aux<<")\n";
            }
            memory.release_allocation(address);
        }
        if(cursor!=data.size())throw std::runtime_error("trailing markup oracle data");
        std::cout<<"markup_oracle_cases="<<count<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
