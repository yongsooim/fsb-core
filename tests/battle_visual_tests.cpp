#include "fsb_core/raster.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>
using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=2)return 2;const auto data=fsb::lab::read(argv[1]);std::size_t cursor=8;
        if(data.size()<12||std::string(data.begin(),data.begin()+7)!="FSBCHK1")throw std::runtime_error("checker reference header");
        const auto word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(data.at(cursor++))<<(i*8);return value;};
        const auto bytes=[&](unsigned size){if(cursor+size>data.size())throw std::runtime_error("checker reference truncated");std::vector<std::uint8_t> value(data.begin()+cursor,data.begin()+cursor+size);cursor+=size;return value;};
        const auto count=word();unsigned failures=0;
        for(unsigned i=0;i<count;++i){
            const auto sw=word(),sh=word(),dw=word(),dh=word();const auto x=signed32(word()),y=signed32(word());
            const Rect sr{signed32(word()),signed32(word()),signed32(word()),signed32(word())},clip{signed32(word()),signed32(word()),signed32(word()),signed32(word())};
            const auto transparent=word(),fill=word();Image8 source,target;source.width=sw;source.height=sh;source.pixels=bytes(sw*sh);target.width=dw;target.height=dh;target.pixels=bytes(dw*dh);const auto expected=bytes(dw*dh);
            draw_checker_tile(target,source,sr,x,y,transparent,std::uint8_t(fill),clip);
            if(target.pixels!=expected){++failures;std::cerr<<"checker mismatch case="<<i<<'\n';}
        }
        if(cursor!=data.size())throw std::runtime_error("checker reference has unused bytes");
        std::cout<<"original_checker_cases="<<count<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
