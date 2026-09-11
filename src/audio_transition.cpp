#include "fsb_core/audio.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core {
namespace {
bool volume(int value){return value>=0&&value<=100;}
void ramp(Memory& memory,unsigned mode,int start,int target,int frames){
    memory.write(0x76965c,mode);memory.write(0x769630,std::uint32_t(frames));
    memory.write(0x769640,sequence_alu(alu::signed_divide,std::uint32_t(target-start)*65536,std::uint32_t(frames)));
    memory.write(0x769634,std::uint32_t(start)<<16);
}
}
void Audio::fade_volume(int frames,int target){
    ready();if(!volume(target))return;
    if(!frames)subvolume(unsigned(target));else ramp(memory_,1,signed32(memory_.read(0x76e918)),target,frames);
}
void Audio::transition_bgm(int out_frames,int out_volume,unsigned next_id,int in_volume,int in_frames,int target){
    ready();if(next_id<1||next_id>58){stop_bgm();return;}
    if(!volume(out_volume)||!volume(in_volume)||!volume(target))return;
    if(!out_frames){
        load_bgm(next_id);play_bgm();subvolume(unsigned(in_volume));
        if(!in_frames)subvolume(unsigned(target));else ramp(memory_,1,in_volume,target,in_frames);
    }else{
        ramp(memory_,2,signed32(memory_.read(0x76e918)),out_volume,out_frames);
        memory_.write(0x769648,unsigned(in_volume));memory_.write(0x76964c,unsigned(target));memory_.write(0x769650,next_id);memory_.write(0x769644,std::uint32_t(in_frames));
    }
}
void Audio::tick_global_fade(unsigned jobs){
    const auto mode=memory_.read(0x76965c);if(!mode)return;if(mode>2)throw Fault(0x76965c,"unknown global BGM fade mode");
    // 43362b latches the mode before its job loop, including boundary batches.
    for(unsigned i=0;i<jobs;++i){
        const auto accumulator=memory_.read(0x769634)+memory_.read(0x769640);memory_.write(0x769634,accumulator);
        subvolume(unsigned(std::clamp(signed32(accumulator+0x8000)/65536,0,100)));
        const auto remaining=memory_.read(0x769630)-1;memory_.write(0x769630,remaining);
        if(remaining)continue;
        if(mode==1)memory_.write(0x76965c,0);
        else{
            load_bgm(memory_.read(0x769650));play_bgm();subvolume(memory_.read(0x769648));
            const auto frames=memory_.read(0x769644);
            if(!frames)subvolume(memory_.read(0x76964c));
            else ramp(memory_,1,signed32(memory_.read(0x769648)),signed32(memory_.read(0x76964c)),signed32(frames));
        }
    }
}
} // namespace fsb::core
