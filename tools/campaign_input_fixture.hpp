#pragma once
#include "field_input_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fsb::lab {
// A developer input script, separate from the game VM. Commands observe state
// and post ordinary keys; they never write quest, inventory or battle records.
class CampaignInputFixture {
public:
    std::function<void(unsigned)> save_checkpoint;
    void load(const std::filesystem::path& path){
        std::ifstream file(path);if(!file)throw std::runtime_error("cannot read campaign input script");
        std::string line;unsigned number=0;
        while(std::getline(file,line)){
            ++number;std::istringstream row(line);Command c;row>>c.kind;
            if(c.kind.empty()||c.kind[0]=='#')continue;
            std::string value;while(row>>value){if(value[0]=='#')break;c.args.push_back(std::stoul(value,nullptr,0));}
            const unsigned count=c.kind=="settle"?(c.args.empty()?0:1):c.kind=="walk"?3:c.kind=="expect"||c.kind=="until"?3:c.kind=="dialog"||c.kind=="battle"||c.kind=="close"||c.kind=="menu"?0:c.kind=="buy"||c.kind=="sell"||c.kind=="select"||c.kind=="hold"?2:1;
            if(c.args.size()!=count||(c.kind!="walk"&&c.kind!="face"&&c.kind!="key"&&c.kind!="hold"&&c.kind!="wait"&&c.kind!="dialog"&&c.kind!="battle"&&c.kind!="event"&&c.kind!="complete"&&c.kind!="world"&&c.kind!="npc"&&c.kind!="talk"&&c.kind!="choice"&&c.kind!="buy"&&c.kind!="sell"&&c.kind!="pick"&&c.kind!="member"&&c.kind!="select"&&c.kind!="close"&&c.kind!="menu"&&c.kind!="expect"&&c.kind!="until"&&c.kind!="checkpoint"&&c.kind!="settle"))throw std::runtime_error("invalid campaign command at line "+std::to_string(number));
            c.line=number;commands_.push_back(c);
        }
        enabled_=true;
    }
    bool enabled()const{return enabled_;}
    bool completed()const{return enabled_&&cursor_==commands_.size()&&!key_;}
    std::size_t cursor()const{return cursor_;}
    std::optional<core::InputMessage> next(core::Runtime& runtime,unsigned now){
        using namespace core;const auto& m=runtime.memory;
        if(key_&&now>=release_){const auto key=key_;key_=0;ready_=now+200;return keyboard_message(key,scan(key),false);}
        if(!enabled_||completed()||key_||now<ready_)return {};
        const auto& c=commands_[cursor_];
        if(!started_){started_=true;began_=now;saw_battle_=false;walk_={};world_={};actor_={};if(c.kind=="walk"){walk_.map=c.args[0];walk_.x=int(c.args[1]);walk_.y=int(c.args[2]);}if(c.kind=="world")world_.destination=c.args[0];if(c.kind=="npc"||c.kind=="talk")actor_.template_id=c.args[0];}
        if((c.kind=="buy"||c.kind=="sell")&&now==began_){if(c.args[0]>=362||!c.args[1]||c.args[1]>99)throw Fault(c.line,"original shop transaction requires1..99 items");owned_=m.read(0x806e30+c.args[0]*4);if(c.kind=="sell"&&owned_<c.args[1])throw Fault(c.line,"sale exceeds owned quantity");}
        const auto timeout=c.kind=="settle"&&!c.args.empty()?c.args[0]:(c.kind=="walk"||c.kind=="event"||c.kind=="complete"||c.kind=="battle"||c.kind=="settle")?1800000u:300000u;
        if(now-began_>timeout)throw Fault(c.line,"campaign input command timed out: "+c.kind);
        const auto done=[&](){++cursor_;started_=false;};
        if(c.kind=="walk"){
            if(auto input=walk_.next(runtime,now))return input;
            if((m.read(globals::current_event_id)!=0xffffffffu||!m.read(0x7683d4))&&m.read(globals::live_dialogue_count)&&now>=confirm_){confirm_=now+900;return press(13,now);}
            const auto actor=Actors::slot(m.read(globals::active_party_index));
            if(walk_.settled()&&!m.read(actor+actor_offset::motion_state))done();
        }else if(c.kind=="world"){
            if(now==began_)saw_world_=false;
            saw_world_|=m.read(globals::game_mode)==10;
            if(auto input=world_.next(runtime,now))return input;
            // A field exit can still be fading when this command starts.
            // Observe entering world mode before accepting its later exit.
            if(saw_world_&&m.read(globals::game_mode)!=10)done();
        }else if(c.kind=="npc"||c.kind=="talk"){
            if(c.kind=="talk"&&m.read(0x7683d4)){done();return {};}
            if(auto input=actor_.next(runtime,now))return input;
            if(actor_.completed()||m.read(globals::game_mode)==5||m.read(globals::game_mode)==6)done();
        }else if(c.kind=="choice"){
            for(auto handle:runtime.arena.members(5)){
                const auto ctrl=*resolve_compact(m,handle);if(m.read(ctrl+compact_offset::callback)!=routines::dialogue_tick)continue;
                const auto state=m.read(ctrl+compact_offset::state_pointer);if(!state||!(m.read(state+0x15c)&64))continue;
                if(c.args[0]>=m.read(state+0x16c))throw Fault(c.line,"choice index outside actual menu");
                const auto current=m.read(state+0x170);if(current!=c.args[0])return press(current<c.args[0]?40:38,now);
                done();return press(13,now);
            }
            if(m.read(globals::live_dialogue_count)&&now>=confirm_){confirm_=now+900;return press(13,now);}
        }else if(c.kind=="menu"){
            const auto root=m.read(0x7735e8);
            if(m.read(globals::game_mode)==11&&root&&m.read(root+0x20)==20)done();
            else if(m.read(globals::game_mode)==3&&m.read(globals::current_event_id)==0xffffffffu&&!m.read(globals::live_dialogue_count)&&!runtime.palette.busy()&&!m.read(0x803a4c))return press(27,now);
        }else if(c.kind=="close"){
            if(m.read(globals::game_mode)==3&&!m.read(globals::live_dialogue_count))done();
            else if(m.read(globals::game_mode)==6||m.read(globals::game_mode)==11)return press(27,now);
        }else if(c.kind=="member"){
            if(c.args[0]>=m.read(globals::party_count))throw Fault(c.line,"party menu member outside roster");
            if(m.read(globals::game_mode)!=11)return {};
            const auto root=m.read(0x7735f4),active=m.read(0x773634);if(!root||m.read(root+0x20)!=20||!active||m.read(active+0x20)!=20)return {};
            const auto current=m.read(0x772a10);if(current==c.args[0])done();else return press(current<c.args[0]?37:39,now); //43bcc7: Left increments carousel index.
        }else if(c.kind=="select"){
            if(c.args[0]>=18)throw Fault(c.line,"menu selector outside original slot table");
            const auto menu=m.read(0x7735e8+c.args[0]*4);if(!menu||m.read(menu+0x20)!=20)return {};
            const auto current=m.read(menu+0x1a0);if(current==c.args[1])done();else return press(current<c.args[1]?40:38,now);
        }else if(c.kind=="pick"){
            const auto picker=m.read(0x7735f8);if(!picker||m.read(picker+0x20)!=20)return {};
            const auto row=m.read(picker+0x1a0),count=m.read(0x773638);if(row>=count||count>362)throw Fault(c.line,"item picker row outside catalog");
            unsigned wanted=0;while(wanted<count&&m.read(0x773030+wanted*4)!=c.args[0])++wanted;
            if(wanted==count)throw Fault(c.args[0],"requested item is absent from the current picker");
            if(row==wanted){done();return press(13,now);}
            return press(row<wanted?40:38,now);
        }else if(c.kind=="buy"||c.kind=="sell"){
            const bool sell=c.kind=="sell";
            if(m.read(0x806e30+c.args[0]*4)==(sell?owned_-c.args[1]:owned_+c.args[1])){done();return {};}
            if(m.read(globals::game_mode)!=6||m.read(0x803850)!=1)return {};
            if(!m.read(0x80380c)){const auto mode=m.read(0x803828);return press(mode==unsigned(sell)?13:mode<unsigned(sell)?39:37,now);}
            if(m.read(0x80380c)!=unsigned(sell)+1)return press(27,now);
            unsigned row=0;while(row<m.read(0x802cb8)&&m.read(0x802cc0+row*8)!=c.args[0])++row;
            if(row==m.read(0x802cb8))throw Fault(c.args[0],"item is not stocked by this shop");
            const auto current=m.read(0x802cb0);if(current!=row)return press(current<row?40:38,now);
            const auto quantity=m.read(0x802cc4+row*8);if(quantity<c.args[1]){
                if(!sell&&m.read(0x803a18)-m.read(0x803838)<m.read(0x613194+c.args[0]*76))throw Fault(c.args[0],"purchase fixture has insufficient gold");
                return press(39,now);
            }
            return press(quantity>c.args[1]?37:13,now);
        }else if(c.kind=="key"||c.kind=="hold"){
            const auto duration=c.kind=="hold"?c.args[1]:48;if(!duration||duration>60000)throw Fault(c.line,"hold duration outside1..60000ms");done();return press(c.args[0],now,duration);
        }else if(c.kind=="face"){
            if(c.args[0]>=4)throw Fault(c.line,"campaign facing must be cardinal");
            const auto actor=Actors::slot(m.read(globals::active_party_index));
            if(!m.read(actor+actor_offset::motion_state)){
                if(m.read(actor+actor_offset::facing)==c.args[0])done();
                else{constexpr unsigned keys[]={38,40,37,39};return press(keys[c.args[0]],now);}
            }
        }else if(c.kind=="settle"){
            if(now==began_)field_ready_since_=0;
            const auto actor=Actors::slot(m.read(globals::active_party_index));
            const bool ready=m.read(globals::game_mode)==3&&m.read(globals::current_event_id)==0xffffffffu&&
                !m.read(globals::live_dialogue_count)&&!runtime.messages.size()&&!m.read(0x803a4c)&&!runtime.palette.busy()&&
                m.read(actor+actor_offset::callback)==routines::field_actor_movement&&
                !m.read(actor+actor_offset::motion_state)&&(m.read(actor+actor_offset::flags)&0x80);
            if(!ready)field_ready_since_=0;
            else if(!field_ready_since_)field_ready_since_=now;
            else if(now-field_ready_since_>=1000){done();return {};}
            // Keep chained scenes moving through normal confirmations. Choices
            // still require an explicit choice command; no story flag is set.
            if(m.read(globals::live_dialogue_count)&&!m.read(0x7683d4)&&now>=confirm_){confirm_=now+900;return press(13,now);}
        }else if(c.kind=="checkpoint"){
            if(m.read(globals::game_mode)==3&&m.read(globals::current_event_id)==0xffffffffu&&!m.read(globals::live_dialogue_count)&&!runtime.messages.size()&&!m.read(0x803a4c)&&!runtime.palette.busy()){
                if(!save_checkpoint)throw Fault(c.line,"checkpoint output is not connected");save_checkpoint(c.args[0]);done();
            }
        }else if(c.kind=="wait"){if(now-began_>=c.args[0])done();
        }else if(c.kind=="dialog"){
            if(now-began_>=500){if(!m.read(globals::live_dialogue_count)&&!runtime.messages.size())done();else if(now>=confirm_){confirm_=now+900;return press(13,now);}}
        }else if(c.kind=="battle"){
            saw_battle_|=m.read(globals::game_mode)==9;
            if(saw_battle_&&m.read(globals::game_mode)==3&&m.read(globals::current_event_id)==0xffffffffu&&!m.read(globals::live_dialogue_count)&&!runtime.messages.size())done();
            if(started_&&m.read(globals::live_dialogue_count)&&now>=confirm_){confirm_=now+900;return press(13,now);}
        }else if(c.kind=="event"||c.kind=="complete"){
            for(const auto& event:runtime.completed_events())if(event.id==c.args[0]&&(c.kind=="event"||m.read(globals::event_activation_counts+c.args[0]*4)==0xffffffffu)){done();break;}
            if(started_&&m.read(globals::live_dialogue_count)&&now>=confirm_){confirm_=now+900;return press(13,now);}
        }else{
            const bool matches=(m.read(c.args[0])&c.args[1])==c.args[2];
            if(matches)done();else if(c.kind=="expect")throw Fault(c.args[0],"campaign state assertion failed at line "+std::to_string(c.line));
        }
        return {};
    }
private:
    struct Command{std::string kind;std::vector<unsigned> args;unsigned line=0;};
    std::vector<Command> commands_;std::size_t cursor_=0;
    FieldWalkFixture walk_;WorldInputFixture world_;FieldInputFixture actor_;
    bool enabled_=false,started_=false,saw_battle_=false,saw_world_=false;unsigned key_=0,release_=0,ready_=0,began_=0,confirm_=0,owned_=0,field_ready_since_=0;
    static unsigned scan(unsigned key){switch(key){case 27:return 1;case 38:return 0xc8;case 40:return 0xd0;case 37:return 0xcb;case 39:return 0xcd;case 68:return 0x20;case 90:return 0x2c;default:return 0x1c;}}
    core::InputMessage press(unsigned key,unsigned now,unsigned duration=48){key_=key;release_=now+duration;return core::keyboard_message(key,scan(key),true);}
};
} // namespace fsb::lab
