#include "fsb_core/field_menu.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>
#include <cctype>

namespace fsb::core {
namespace {
std::string text(RecoveredBattle& call,Address at){std::string result;for(unsigned i=0;i<260;++i){const auto ch=call.read(at+i,1);if(!ch)return result;result+=char(ch);}throw Fault(at,"unterminated file name");}
std::string filename(std::string name){
    for(auto& c:name)c=char(std::tolower(static_cast<unsigned char>(c)));
    if(name=="saveold.dat")return "Saveold.dat";
    if(name.size()==9&&name.substr(0,4)=="save"&&name[4]>='0'&&name[4]<='8'&&name.substr(5)==".dat")return "Save"+name.substr(4);
    throw Fault(0,"unsupported save file name");
}
}
std::optional<FieldMenu::Bytes> FieldMenu::read(const std::string& name){try{if(name=="Save0.dat"&&pending_load_)return pending_load_;if(read_file)return read_file(name);const auto it=memory_files_.find(name);return it==memory_files_.end()?std::nullopt:std::optional<Bytes>(it->second);}catch(const std::exception& e){error_=e.what();return std::nullopt;}}
bool FieldMenu::write(const std::string& name,const Bytes& bytes){try{if(write_file)return write_file(name,bytes);memory_files_[name]=bytes;return true;}catch(const std::exception& e){error_=e.what();return false;}}
bool FieldMenu::remove(const std::string& name){if(remove_file)return remove_file(name);return memory_files_.erase(name)!=0;}
void FieldMenu::open(unsigned slot){
    auto& m=runtime_.memory;m.write(0x80384c,0);m.write(0x803850,0);m.write(globals::game_mode,11);
    runtime_.battle.recovered.invoke(0x43934c,{1});runtime_.battle.recovered.invoke(0x439701,{slot,1});
}
bool FieldMenu::active()const{return runtime_.memory.read(0x773634)!=0;}
void FieldMenu::field_input(){
    auto& m=runtime_.memory;const auto actor=Actors::slot(m.read(globals::active_party_index)),flags=m.read(actor+4);
    if((flags&0x10080)!=0x10080||m.read(actor+0x104)||m.read(actor+0x148)!=routines::field_actor_movement||m.read(0x8021d8)||m.read(globals::input_message)!=0x100||(m.read(globals::input_flags)&0x40000000))return;
    const auto key=m.read(globals::input_key),input_flags=m.read(globals::input_flags),scan=((input_flags>>1)&0x800000)|(input_flags&0xff0000);
    if(key==27||key=='Z'||scan==0x520000)open();
    else if(key=='D'){
        const auto map=m.read(globals::current_map_id);
        if(map>456&&map<467)runtime_.battle.recovered.invoke(0x45f07e,{m.read(0x803a3c)==0});
    }
    else if(scan==0x3c0000)open(7);
    else if(scan==0x3d0000){if(runtime_.battle.recovered.invoke(0x460e35)&&m.read(0x5c4f56+m.read(globals::current_map_id)*68,1))open(8);}
    else if(scan==0x3e0000)open(10);
    else if(scan==0x3f0000)open(3);
    else if(key=='C')runtime_.battle.recovered.invoke(0x45f495,{0xffffffffu});
}
void FieldMenu::tick(){
    auto& m=runtime_.memory;const auto packed=runtime_.battle.recovered.invoke(0x43955f),command=packed&65535;
    if(!command)return;
    if(command>6)return;
    runtime_.battle.recovered.invoke(0x4394ea);m.write(0x766f74,0xffffffffu);
    if(command==2){m.write(globals::game_mode,0xffffffffu);return;}
    m.write(globals::game_mode,3);
    if(command==3){m.write(0x804a64,7);runtime_.battle.recovered.invoke(0x460528,{packed>>16});m.write(0x804abc,m.read(globals::frame_time_ms));}
}
bool FieldMenu::save(unsigned slot){return slot>=1&&slot<=8&&runtime_.battle.recovered.invoke(0x460e58,{slot})!=0;}
bool FieldMenu::load_snapshot(const Bytes& bytes){
    if(!valid_save(bytes)){error_="Invalid or unsupported save file";return false;}
    error_.clear();pending_load_=bytes;
    return runtime_.battle.recovered.invoke(0x46118e,{0})!=0;
}
void FieldMenu::after_call(Address entry,RecoveredBattle& call){
    if(entry!=0x460e58)return;
    if(write_failed_){call.r[0]=0;runtime_.environment.diagnostics.emplace_back(entry,error_);}
    streams_.clear();pending_backups_.clear();write_failed_=false;
}
FieldMenu::Bytes FieldMenu::settings()const{
    Bytes data{'F','S','B','C','F','G','1',0};for(unsigned i=0;i<14;++i){const auto value=runtime_.memory.read(0x772fe0+i*4);for(unsigned b=0;b<4;++b)data.push_back(std::uint8_t(value>>(b*8)));}return data;
}
bool FieldMenu::load_settings(const Bytes& bytes){
    if(bytes.size()!=64||std::string(bytes.begin(),bytes.begin()+8)!=std::string("FSBCFG1\0",8))return false;
    std::array<std::uint32_t,14> values{};for(unsigned i=0;i<14;++i)for(unsigned b=0;b<4;++b)values[i]|=std::uint32_t(bytes[8+i*4+b])<<(b*8);
    for(unsigned i=0;i<14;++i){const auto id=i+2,value=values[i];if(id<=6&&value>100)return false;if(id>=7&&id<=9&&value>255)return false;if((id==10||id>=13)&&value>1)return false;if(id==11&&value!=0xffffffffu&&value>2)return false;if(id==12&&(value<1||value>6))return false;}
    auto& m=runtime_.memory;for(unsigned i=0;i<14;++i)m.write(0x772fe0+i*4,values[i]);runtime_.battle.recovered.invoke(0x431893);
    runtime_.audio.master_volume(true,signed32(m.read(0x772fec)));runtime_.audio.master_volume(false,signed32(m.read(0x772ff0)));
    for(unsigned i=0;i<3;++i){const auto value=m.read(0x772ff4+i*4);m.write(0x5b15f0+i,value,1);m.write(0x5b15f4+i,value/2,1);}return true;
}
bool FieldMenu::valid_save(const Bytes& bytes)const{
    // Bounds checks precede the original loader's direct writes/allocations.
    if(bytes.size()<0x1c00||bytes.size()>1024*1024)return false;
    const auto word=[&](std::size_t at){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(at+i))<<(i*8);return value;};
    const auto size=word(0x1bfc);if(size<24||size>bytes.size()-0x1c00||size%4)return false;
    const auto tail=0x1c00+size;if(tail+4>bytes.size()||word(tail)!=0x2a4||tail+4+0x2a4!=bytes.size())return false;
    const auto key=word(0x1c00+20)^0x66666666u;if((word(0x1c00)^key)!=size)return false;
    const auto& m=runtime_.memory;const auto expected=0x4c+m.read(0x4a27d0)*4+m.read(0x4a2660)*12+m.read(0x4a27cc)*4;
    if(size!=expected)return false;
    if((word(0x1c04)^key)!=0x34||(word(0x1c08)^key)!=m.read(0x4a27d0)*4||(word(0x1c0c)^key)!=m.read(0x4a2660)*12||(word(0x1c10)^key)!=m.read(0x4a27cc)*4)return false;
    const auto count=word(0xbf8);if(!count||count>10||word(0xbf0)>=500)return false;
    if(std::find(bytes.begin()+4,bytes.begin()+32,0)==bytes.begin()+32)return false;
    for(unsigned i=0;i<count;++i)if(word(0xbfc+i*4)>=16)return false;
    // 803a24 retains the last visual-selection slot. E8 party changes can
    // leave it beyond the current party (Event70: cache1, one actor, active0).
    // The original loader restores it verbatim; only803a1c is the live index.
    if(word(0xc24)>=10||word(0xc28)>=0x300||word(0xc2c)>=count)return false;
    for(unsigned actor=0;actor<16;++actor)for(unsigned slot=0;slot<5;++slot){const auto item=word(0x30+actor*0xbc+0x74+slot*4);if(item!=0xffffffffu&&item>=360)return false;}
    return true;
}
bool FieldMenu::service(Address entry,RecoveredBattle& call){
    auto& m=runtime_.memory;const auto arg=[&](unsigned i){return call.argument(i);};
    switch(entry){
    case 0x8594a0:call.result(m.read(globals::frame_time_ms));return true;
    case 0x8593a4:call.result(remove(filename(text(call,arg(0)))),4);return true;
    case 0x8594b8:error_=text(call,arg(1));runtime_.environment.diagnostics.emplace_back(entry,error_);call.result(1,16);return true;
    case 0x498670:{
        const auto name=filename(text(call,arg(0))),mode=text(call,arg(1));Stream stream;stream.name=name;stream.writing=mode=="wb";if(stream.writing){write_failed_=false;error_.clear();}
        if(!stream.writing){if(mode!="rb")throw Fault(entry,"unsupported save file mode");auto data=read(name);if(!data||!valid_save(*data)){call.result(0);return true;}stream.bytes=std::move(*data);}
        const auto handle=next_stream_++;streams_.emplace(handle,std::move(stream));call.result(handle);return true;
    }
    case 0x498690:case 0x498870:{
        const auto size=arg(1),count=arg(2);const auto total=std::uint64_t(size)*count;
        if(total>1024*1024)throw Fault(entry,"save block exceeds limit");auto& stream=streams_.at(arg(3));
        if(entry==0x498870){if(!stream.writing)throw Fault(entry,"write to save reader");for(unsigned i=0;i<total;++i)stream.bytes.push_back(std::uint8_t(call.read(arg(0)+i,1)));call.result(count);}
        else{const auto available=std::min<std::size_t>(std::size_t(total),stream.bytes.size()-stream.cursor);for(unsigned i=0;i<available;++i)call.write(arg(0)+i,stream.bytes[stream.cursor+i],1);stream.cursor+=available;call.result(size?unsigned(available/size):0);}
        return true;
    }
    case 0x4985c0:{
        auto it=streams_.find(arg(0));if(it==streams_.end()){call.result(0xffffffffu);return true;}bool ok=true;
        if(it->second.writing){const auto backup=pending_backups_.find(it->second.name);if(backup!=pending_backups_.end())ok=write("Saveold.dat",backup->second);if(ok)ok=write(it->second.name,it->second.bytes);}
        else if(it->second.name=="Save0.dat")pending_load_.reset();
        streams_.erase(it);if(!ok){write_failed_=true;error_="Save file could not be written; previous slot preserved";}call.result(ok?0:0xffffffffu);return true;
    }
    case 0x499340:{
        const auto from=filename(text(call,arg(0))),to=filename(text(call,arg(1)));const auto data=read(from);
        // Delay backup rotation until the complete new snapshot is available.
        // Host writes use a temporary file, so failed saves retain the old slot.
        if(to!="Saveold.dat")throw Fault(entry,"unexpected save rename destination");if(data)pending_backups_[from]=*data;call.result(data?0:0xffffffffu);return true;
    }
    case 0x4974a0:call.result(m.allocate_zeroed(std::max(1u,arg(0))));return true;
    case 0x433569:runtime_.audio.stop_bgm();call.result(0);return true;
    case 0x401a82:runtime_.environment.diagnostics.emplace_back(entry,call.format_text(arg(0),call.r[4]+8));call.result(0);return true;
    case 0x460d4c:{
        const auto now=m.read(globals::frame_time_ms),elapsed=now+m.read(0x804ab8)-m.read(0x804abc);m.write(0x804abc,now);m.write(0x804ab8,elapsed%1000);
        const auto seconds=m.read(0x804c78)+elapsed/1000,minutes=m.read(0x804c74)+seconds/60,hours=minutes/60;m.write(0x804c78,seconds%60);m.write(0x804c74,minutes%60);m.write(0x804c70,m.read(0x804c70)+hours);call.result(hours);return true;
    }
    default:return false;
    }
}
} // namespace fsb::core
