#include "runtime_assets.hpp"
#include "fsb_core/original_cheats.hpp"
#include "fsb_core/symbols.hpp"
#include <iostream>
using namespace fsb::core;
int main(int argc,char** argv){
    if(argc!=2)return 2;
    Runtime runtime(fsb::lab::read(std::filesystem::path(argv[1])/"FLYINGSB.EXE"));FontRaster fonts(runtime.memory);
    fsb::lab::register_runtime_assets(runtime,fonts,argv[1],Runtime::full_campaign);
    auto& m=runtime.memory;fsb::original_cheats::configure(m,true,1998);
    if(m.read(fsb::original_cheats::enabled))throw std::runtime_error("1998 gate");
    fsb::original_cheats::configure(m,true,1999);runtime.start_field_fixture(22,Runtime::full_campaign);
    unsigned now=0;
    const auto advance=[&](unsigned ms){for(unsigned i=0;i<ms;++i)runtime.advance(now++,false);};
    const auto press=[&](unsigned key,unsigned scan){runtime.post_input(keyboard_message(key,scan,true));advance(70);runtime.post_input(keyboard_message(key,scan,false));advance(70);};
    const unsigned scans[]={0x13,0x17,0x2e,0x23,0x17,0x1f,0x13,0x17,0x2e,0x23};
    const auto phrase=[&]{unsigned i=0;for(unsigned char key:std::string("RICHISRICH"))press(key,scans[i++]);};
    advance(1500);const auto gold=m.read(0x803a18);
    phrase();if(m.read(0x803a18)!=gold)throw std::runtime_error("phrase fired before next key");
    press(32,0x39);if(m.read(0x803a18)!=gold+100000)throw std::runtime_error("game frame did not apply gold cheat");
    const auto ring=m.read(fsb::original_cheats::cursor);
    press(113,0x3c);if(m.read(fsb::original_cheats::cursor)!=ring)throw std::runtime_error("menu-opening key entered cheat ring");
    phrase();if(m.read(fsb::original_cheats::cursor)!=ring||m.read(0x803a18)!=gold+100000)throw std::runtime_error("cheat entered from native menu");
    std::cout<<"original cheat year gate, frame integration, delayed trigger and menu exclusion passed\n";
}
