#include "fsb_core/dialogue.hpp"
#include "fsb_core/progress_timer.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/vm.hpp"
#include "../tools/lab_io.hpp"
#include <iostream>

using namespace fsb::core;
namespace{
unsigned checks=0,failures=0;
void check(bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
template<class F>void fault(F fn,const char* text){bool caught=false;try{fn();}catch(const Fault&){caught=true;}check(caught,text);}
Address text(Memory& m,const std::string& value){const auto at=m.allocate_zeroed(unsigned(value.size()+1));for(unsigned i=0;i<value.size();++i)m.write(at+i,std::uint8_t(value[i]),1);return at;}
}
int main(int argc,char** argv){
    try{
        if(argc!=2)return 2;auto m=Memory::from_pe32(fsb::lab::read(argv[1]));
        const auto parse=[&](const std::string& s){const auto p=text(m,s);const auto t=tokenize_markup(m,p);m.release_allocation(p);return t;};
        auto t=parse("<$010>");check(t.code==45&&t.argument==packed_id("010")&&t.bytes==6&&!t.auxiliary,"numeric message IDs preserve ASCII leading zero");
        t=parse("<$SON5:3001>");check(t.code==45&&t.argument==packed_id("SON5")&&t.auxiliary==(3001|0x8000),"flagged actor packet retains its0x8000 routing bit");
        t=parse("<$XmIR!->");check(t.argument==packed_id("XmIR")&&t.auxiliary==32,"message IDs preserve case while suffix controls pack separately");
        t=parse("<F0B>");check(t.code==28&&t.argument==0x20000&&t.bytes==5,"font face and B/T variant are separate packet lanes");
        t=parse("<Dir2>");check(t.code==51&&t.auxiliary==1&&!t.argument,"keypad2 maps to original direction selector1,not a bitmask");
        fault([&]{parse("<Dir5>");},"keypad center is rejected like the original assertion");
        t=parse("<#?>");check(t.code==46&&t.argument==0,"literal question target is zero,not the alias sentinel");
        t=parse("<#>");check(t.code==46&&t.argument==0xffffffff,"empty compact target selects alias fallback");
        check(parse("<M+>").argument==0&&parse("<M->").argument==1,"M signs follow original reverse enable convention");
        t=parse("<Unrecognized>");check(!t.code&&t.bytes==1,"unrecognized markup stays literal text instead of being silently discarded");
        t=parse("<possub_L>");check(t.code==26&&t.argument==0x12da,"PosSub tag spelling is case insensitive");
        const auto first=measure_dialog(m,0x61ecc2,36,2,0);
        check(first.columns==0&&first.lines==1&&first.trim==2,"original SONA preamble measures as an empty initial chunk");
        const auto speech=measure_dialog(m,0x61ecd8,44,2,0);
        check(speech.columns==22&&speech.lines==3&&speech.trim==2,"actual first SONA speech measures22 byte-columns,3 lines");
        const auto p=text(m,"ABCD EFGH");const auto wrap=measure_dialog(m,p,4,0,0);m.release_allocation(p);
        check(wrap.columns==4&&wrap.lines==2,"auto-wrap discards the leading continuation space");
        std::vector<std::uint32_t> markers;
        for(unsigned offset=0;m.read(0x61ecc2+offset,1);){const auto token=tokenize_markup(m,0x61ecc2+offset);if(token.code==45)markers.push_back(token.argument);offset+=token.bytes;}
        check(markers==std::vector<std::uint32_t>{packed_id("010"),packed_id("020"),packed_id("SON5"),packed_id("080")},"original SONA marker order comes from the live byte stream");

        const auto timer_state=m.allocate_zeroed(28);ProgressTimer timer(m,timer_state);
        timer.start(260,true,false,1000);check(timer.tick(1000)==0&&timer.tick(1130)==15015,"millisecond timer preserves exact half progress");
        timer.start(120,false,true,1130);check(timer.tick(1130)==15015&&timer.tick(1190)==0,"direction flip preserves visible progress and remaining60ms");
        timer.start(0xfffffffe,true,false,0);check(timer.tick(0)==10010&&timer.tick(0)==20020&&timer.tick(0)==30030,"negative-frame timer advances by reads,independent of timestamp");
        timer.start(100,false,false,2000);timer.scale_speed(2,5,2020);check(m.read(timer_state+16)==2220&&m.read(timer_state+20)==250,"source0.4 speed factor scales remaining deadline and total duration");
        timer.start(64,true,false,0xfffffff0);check(timer.tick(0xfffffff0)==30030,"original unsigned deadline comparison is preserved at tickcount wrap");
        fault([&]{timer.start(0xffff63bf,true,false,0);},"zero frame step cannot be silently treated as completed");

        Arena arena(m);arena.initialize();Actors actors(m);actors.bootstrap_new_game_actors();
        const auto event=arena.activate_event(0);check(event&&m.read(0x57fd1c)==0&&m.read(*resolve_compact(m,event)+0xe4)==0,"real event activation publishes event id and normalizes trigger context");
        actors.set_party_mask(0x41,true);HsmQueue queue;Dialogue dialogue(m,queue);dialogue.configure_timing(0,30,0);
        const auto handle=dialogue.spawn_markup(9,0x61ecc2,packed_id("SONA")),ctrl=*resolve_compact(m,handle),d=dialogue.state(handle);
        check(arena.members(5)==std::vector<Handle>{handle}&&m.read(ctrl+0x18)==0x410000&&m.read(0x768684)==1,"dialogue controller uses late group5 and original lifetime flags");
        check(m.read(d+0x138)==15&&(m.read(d+0x158)&0x2000)&&m.read(d)==lookup_actor(m,9)+0x120,"creation carries event speed,TurnMan flag and live actor anchor pointers");
        check(m.read(d+0x70)==32&&m.read(d+0x74)==54&&m.read(d+0x54)==0x10000f8,"initial blank layout keeps exact dimensions and original color table value");
        m.write(d+0x15c,0x810);m.write(d+0x168,packed_id("X"));queue.enqueue({packed_id("SONA"),packed_id("BAD"),0,0});queue.enqueue({packed_id("SONA"),packed_id("X"),0,0});
        check(!dialogue.process_waiting_message(handle)&&queue.size()==2,"wrong-channel head is retained and blocks a later matching ACK");
        queue.remove(*queue.find(packed_id("SONA"),0));check(dialogue.process_waiting_message(handle)&&!(m.read(d+0x15c)&16)&&m.read(0x7683dc)==1,"matching ACK clears only real HSM wait and advances TurnMan count");
        m.write(d+0x15c,16);queue.enqueue({packed_id("SONA"),0,0x8000|3001,0});check(!dialogue.process_waiting_message(handle)&&queue.size()==1,"flagged actor messages never enter unflagged dialog ACK consumer");queue.clear();
        dialogue.enqueue_message(handle,packed_id("010"),0);auto message=queue.logical_records().front();check(message.target==packed_id("010")&&message.channel==packed_id("SONA")&&message.sender==handle,"markup marker routes target,channel,and sender distinctly");
        const auto listener=arena.clone_event(0x6ce354,1),listener_obj=*resolve_compact(m,listener),primary=arena.clone_event(0x6ce342,1),secondary=arena.clone_event(0x6ce342,1);
        m.write(listener_obj+0xd0,primary);m.write(listener_obj+0xdc,secondary);m.write(listener_obj+0xf4,packed_id("SON5"));VmEnvironment environment;Vm listener_vm(m,queue,environment,listener_obj);
        check(listener_vm.step()==Yield::Forced&&(m.read(*resolve_compact(m,primary)+0x18)&1),"shared actor-message wait suspends primary animation while secondary child lives");
        arena.release(secondary);check(listener_vm.step()==Yield::Forced&&!(m.read(*resolve_compact(m,primary)+0x18)&1),"shared actor-message wait resumes primary after secondary handle expires");
        std::uint64_t hash=14695981039346656037ull;for(const auto& region:m.snapshot_regions())for(auto byte:region.bytes)hash=(hash^byte)*1099511628211ull;
        std::cout<<"dialogue_state_fnv1a="<<std::hex<<hash<<std::dec<<'\n'<<"dialogue_checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
