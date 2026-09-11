#include "fsb_core/debug_state.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace fsb::core {
namespace {
std::string hex(std::uint32_t v){std::ostringstream s;s<<"0x"<<std::hex<<v;return s.str();}
std::string number(std::uint32_t v){return std::to_string(signed32(v));}
std::string mode_name(unsigned mode){switch(mode){case 3:return "필드";case 4:return "전투 진입";case 5:case 6:return "상점";case 9:return "전투";case 10:return "월드맵";case 11:return "필드 메뉴";case 0xffffffffu:return "종료";default:return "미분류";}}
void utf8(std::string& out,unsigned c){if(c<128)out+=char(c);else if(c<2048){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}else{out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}}
std::string guest_text(const Memory& m,Address p,const std::vector<std::uint8_t>& table){
    if(!p)return {};std::string out;
    for(unsigned i=0;i<128;++i){unsigned c=m.read(p+i,1);if(!c)break;if(c>=128){const auto b=m.read(p+ ++i,1);if(!b)break;c=(c<<8)|b;c=table.size()==131072?unsigned(table[c*2])|(unsigned(table[c*2+1])<<8):0xfffd;}utf8(out,c?c:0xfffd);}while(!out.empty()&&out.back()==' ')out.pop_back();return out;
}
std::string quote(const std::string& text){
    std::ostringstream s;s<<'"';
    const auto escape=[&](unsigned c){s<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<c<<std::dec;};
    for(std::size_t i=0;i<text.size();++i){const auto c=std::uint8_t(text[i]);
        if(c=='"'||c=='\\')s<<'\\'<<char(c);else if(c<32)escape(c);else if(c<128)s<<char(c);
        else{
            const unsigned n=c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;bool valid=n&&i+n<=text.size();
            for(unsigned j=1;valid&&j<n;++j)valid=(std::uint8_t(text[i+j])&0xc0)==0x80;
            if(valid){const auto b=std::uint8_t(text[i+1]);valid=!(c==0xe0&&b<0xa0)&&!(c==0xed&&b>=0xa0)&&!(c==0xf0&&b<0x90)&&!(c==0xf4&&b>=0x90);}
            if(valid){s<<text.substr(i,n);i+=n-1;}else escape(c); // Preserve non-UTF8 diagnostic bytes in valid JSON.
        }
    }s<<'"';return s.str();
}
}
DebugSnapshot inspect_runtime(const Runtime& runtime,const std::vector<std::uint8_t>& cp949){
    DebugSnapshot snapshot;const auto& m=runtime.memory;
    auto& sections=snapshot.sections;sections={{"overview","현재 상태",{}},{"party","파티 / 소지품",{}},{"events","이벤트 / 대기",{}},{"logs","진단 기록",{}}};
    const auto add=[&](unsigned section,std::string label,std::string value){sections[section].fields.push_back({std::move(label),std::move(value)});};
    const auto read=[&](Address at){return m.read(at);};
    try{
        snapshot.frame_ms=read(globals::frame_time_ms);const auto mode=read(globals::game_mode),map=read(globals::current_map_id);
        add(0,"게임 모드",mode_name(mode)+" ("+number(mode)+")");
        add(0,"개발용 보상",runtime.debug_rewards.enabled()?"경험치 · 골드 10배 ON":"OFF · 원본 배율");
        add(0,"실행 플래그 / VM 일시정지",hex(read(globals::runtime_mode_flags))+" / "+std::to_string(runtime.environment.input_pause));
        add(0,"맵",number(map)+(map<500?" · "+guest_text(m,0x5c4f24+map*68,cp949):"")); // write_save_slot_file460e58
        add(0,"이벤트",number(read(globals::current_event_id))+"  (없음 = -1)");
        add(0,"논리 시각 / 프레임",std::to_string(snapshot.frame_ms)+" ms / "+number(read(globals::presented_frame_counter)));
        add(0,"루트 실행 위치",hex(runtime.root_pc())+" · handle "+hex(runtime.root()));
        const auto actor_index=read(globals::active_party_index);
        if(actor_index<768){const auto actor=Actors::slot(actor_index);add(0,"조작 배우 / 타일",number(actor_index)+" / ("+number(read(actor+0x128))+", "+number(read(actor+0x12c))+")");add(0,"배우 콜백 / 이동 상태",hex(read(actor+actor_offset::callback))+" / "+number(read(actor+actor_offset::motion_state)));}
        add(0,"카메라",number(read(globals::camera_x))+", "+number(read(globals::camera_y)));
        add(0,"표현 대기","팔레트 "+std::to_string(runtime.palette.busy())+" · 전환 "+number(read(globals::rect_effect_busy)));
        add(0,"입력","key "+number(read(globals::last_virtual_key))+" · held "+number(read(globals::last_key_held))+" · 큐 "+std::to_string(runtime.pending_input_count()));
        add(0,"객체 / 대화 / 메시지",number(read(globals::compact_active_count))+" / "+number(read(globals::occupied_dialogue_count))+" / "+std::to_string(runtime.messages.size()));
        if(mode==4||mode==9){
            add(0,"전투 단계 / 명령 상태",number(read(0x77e5a0))+" / "+number(read(0x775cb0))+" : "+number(read(globals::battle_command_substate)));
            add(0,"행동 문맥 / 대상 수",number(read(0x7757e0))+" / "+number(read(0x77a50c)));
            add(0,"이동·공격 범위 플래그",hex(read(globals::battle_cursor_effect_flags)));
            for(unsigned i=0;i<std::min(20u,read(0x776484));++i){const auto r=BattleRules::enemy_record(i);add(0,"적 "+std::to_string(i)+" (#"+number(read(r+4))+")","HP "+number(read(r+20))+" · 상태 "+hex(read(r+8)));}
        }
        if(runtime.field_menu.active())for(unsigned i=0;i<18;++i)if(const auto p=read(0x7735e8+i*4))add(0,"메뉴 슬롯 "+std::to_string(i),"state "+number(read(p+0x20))+" · "+hex(p));
        add(1,"소지금",number(read(0x803a18))+" G");
        for(unsigned i=0;i<std::min(10u,read(globals::party_count));++i){
            const auto id=read(globals::party_actor_ids+i*4);if(id>=16){add(1,"인물 ID 오류",number(id));continue;}const auto r=BattleRules::party_record(id);
            add(1,(i==actor_index?"> ":"")+guest_text(m,read(r+0x14),cp949)+" (#"+number(id)+")","HP "+number(read(r+0x1c))+"/"+number(read(r+0x18))+" · MP "+number(read(r+0x24))+"/"+number(read(r+0x20)));
            add(1,"상태 / 행동 게이지",hex(read(r+8))+" / "+number(read(r+0x30)));
            std::string equipment;for(unsigned j=0;j<5;++j){const auto item=read(r+0x74+j*4);if(j)equipment+=" / ";equipment+=item==0xffffffffu?"-":number(item);}add(1,"장비 ID (머리/몸/손/장신구)",equipment);
        }
        unsigned items=0;for(unsigned i=0;i<360;++i)if(const auto count=read(0x806e30+i*4)){++items;add(1,guest_text(m,read(0x61318c+i*76),cp949)+" (#"+std::to_string(i)+")",std::to_string(count)+"개");}if(!items)add(1,"소지품","없음 (장착 중인 장비는 위에 표시)");
        add(2,"이벤트: 현재 / 이전 / 대기",number(read(globals::current_event_id))+" / "+number(read(globals::previous_event_id))+" / "+number(read(globals::pending_event_id)));
        add(2,"종료 경계 도달",runtime.finished()?"예":"아니오");
        for(unsigned group=0;group<capacity::object_groups;++group)for(auto handle:runtime.arena.members(group)){
            const auto p=resolve_compact(m,handle);if(!p)continue;const auto callback=read(*p+compact_offset::callback);if(callback!=routines::event_vm_tick)continue;
            add(2,"스크립트 "+hex(handle)+" · 그룹 "+std::to_string(group),"PC "+hex(read(*p+vm_offset::pc))+" · wait "+number(read(*p+vm_offset::wait_count))+" · child "+hex(read(*p+vm_offset::blocking_child)));
        }
        for(auto h:runtime.arena.members(5))if(const auto p=resolve_compact(m,h))if(read(*p+compact_offset::callback)==routines::dialogue_tick){const auto d=runtime.dialogue.state(h);if(d)add(2,"대화 "+hex(h),"state "+hex(d)+" · text "+hex(read(d+dialog_word::text*4))+" · cursor "+number(read(d+dialog_word::cursor*4)));}
        for(const auto& message:runtime.messages.logical_records())add(2,"HSM 대상 "+hex(message.target),"채널 "+hex(message.channel)+" · 코드 "+hex(message.code));
        const auto& completed=runtime.completed_events();for(std::size_t i=completed.size()>12?completed.size()-12:0;i<completed.size();++i){const auto& e=completed[i];add(2,"완료 Event "+std::to_string(e.id),std::to_string(e.ms)+" ms · "+hex(e.exit_pc));}
        add(3,"난수 상태",hex(m.random_state().crt_seed));
        const auto storage=m.storage_usage();add(3,"게스트 메모리",std::to_string(storage.logical_bytes/1024)+" KiB / "+std::to_string(storage.regions)+"개 영역");
        add(3,"코어 음향 시계",std::to_string(runtime.audio.output_sample())+" samples (장치 재생 시계와 별개)");
        const auto& logs=runtime.environment.diagnostics;add(3,"진단 수",std::to_string(logs.size())+" (최근 32개)");for(std::size_t i=logs.size()>32?logs.size()-32:0;i<logs.size();++i)add(3,hex(logs[i].first),logs[i].second);
        if(!runtime.field_menu.last_error().empty())add(3,"저장 오류",runtime.field_menu.last_error());
    }catch(const std::exception& e){add(3,"상태 조회 오류",e.what());}
    return snapshot;
}
std::string debug_json(const DebugSnapshot& snapshot){
    std::ostringstream out;out<<"{\n  \"format\": \"fsb-debug-v1\",\n  \"frame_ms\": "<<snapshot.frame_ms<<",\n  \"sections\": [\n";
    for(std::size_t i=0;i<snapshot.sections.size();++i){const auto& s=snapshot.sections[i];if(i)out<<",\n";out<<"    {\"id\": "<<quote(s.id)<<", \"title\": "<<quote(s.title)<<", \"fields\": [";
        for(std::size_t j=0;j<s.fields.size();++j){const auto& f=s.fields[j];if(j)out<<',';out<<"\n      {\"label\": "<<quote(f.label)<<", \"value\": "<<quote(f.value)<<'}';}out<<"\n    ]}";
    }out<<"\n  ]\n}\n";return out.str();
}
} // namespace fsb::core
