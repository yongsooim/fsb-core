#include "fsb_core/map.hpp"
#include "fsb_core/map_logic/event_flags.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/sprites.hpp"
#include <sstream>
#include <algorithm>

namespace fsb::core {
namespace {
constexpr Address layer_base = globals::map_layers, layer_stride = layout::map_layer_stride, scroll_base = globals::map_scroll_descriptors;
std::uint32_t word(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    if (at > bytes.size() || 4 > bytes.size() - at) throw Fault(Address(at), "truncated map payload");
    return bytes[at] | std::uint32_t(bytes[at + 1]) << 8 | std::uint32_t(bytes[at + 2]) << 16 | std::uint32_t(bytes[at + 3]) << 24;
}
void copy_bytes(Memory& m, Address dst, const std::vector<std::uint8_t>& bytes, unsigned offset, unsigned count) {
    if (offset > bytes.size() || count > bytes.size() - offset) throw Fault(offset, "truncated map span");
    for (unsigned i = 0; i < count; ++i) m.write(dst + i, bytes[offset + i], 1);
}
unsigned cell_count(const std::vector<std::uint8_t>& bytes, unsigned header,bool allow_empty=false) {
    const auto width = word(bytes, header + 24), height = word(bytes, header + 28);
    if(allow_empty&&!width&&!height)return 0;
    if (!width || !height || width > 4096 || height > 4096 || std::uint64_t(width) * height > 4096)
        throw Fault(header, "map layer exceeds 4096-cell capacity");
    return width * height;
}
std::uint32_t decimal_number(const std::string& text){
    //4562f0's atoi accepts decimal prefixes (stock data has "30." and "0y").
    // Keep the supplied MFO bytes and reproduce that conversion.
    std::size_t at=0;const bool negative=!text.empty()&&text[0]=='-';
    if(!text.empty()&&(text[0]=='-'||text[0]=='+'))++at;
    std::uint32_t value=0;while(at<text.size()&&text[at]>='0'&&text[at]<='9')value=value*10+unsigned(text[at++]-'0');
    return negative?0u-value:value;
}
std::uint32_t number(const std::string& text, bool automatic) {
    if(!automatic)return decimal_number(text);
    try {
        std::size_t consumed = 0; const auto value = std::stoll(text, &consumed, 0);
        if (consumed != text.size() || value < -2147483648ll || value > 4294967295ll) throw std::invalid_argument("range");
        return std::uint32_t(value);
    } catch (...) { throw Fault(0, "invalid numeric MFO token: " + text); }
}
}
std::array<std::string, 6> Map::asset_names(const Memory& memory, unsigned map_id) {
    const auto key = [&](Address at) {
        std::string result;
        for (auto c : memory.bytes(at, 9)) { if (!c) break; if (c != '#') result.push_back(char(c >= 'a' && c <= 'z' ? c - 32 : c)); }
        return result;
    };
    if (map_id > 1023) throw Fault(map_id, "map id outside addressable catalog scope");
    const auto tiles = key(0x5c4f44 + map_id * 0x44), layout = key(0x5c4f4d + map_id * 0x44);
    return {tiles + "P.pcx", tiles + "P.mat", tiles + "S.pcx", tiles + "S.mat", layout + "P.map", layout + "P.mfo"};
}
void Map::load_mat(unsigned plane, const std::vector<std::uint8_t>& bytes) {
    const auto cells = cell_count(bytes, 0), base = layer_base + (plane + 2) * layer_stride;
    copy_bytes(memory_, base, bytes, 0, 48);
    for (unsigned i = 0; i < cells; ++i) {
        const auto raw = word(bytes, 48 + i * 4), type = raw >> 8;
        if (type > 2) throw Fault(i, "unimplemented MAT material class");
        memory_.write(base + 40 + i * 4, raw & 255);
        memory_.write(0x7d8ca0 + (plane * 4096 + i) * 4, type == 0 ? 0xffffffffu : type == 1 ? 1 : 0);
    }
}
void Map::load_layout(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 480) throw Fault(0, "truncated MAP reserved header");
    for (unsigned layer = 0; layer < 2; ++layer) {
        const auto base = layer_base + layer * layer_stride;
        copy_bytes(memory_, base, bytes, layer * 48, 48);
        memory_.write(base + 16, 0); memory_.write(base + 20, 0);
    }
    unsigned at = 480;
    //4560c5 clears the auxiliary attr plane even when the second MAP header
    // is empty. Single-layer interior maps use it for foreground occlusion.
    for(unsigned i=0;i<cell_count(bytes,0);++i)memory_.write(globals::tile_attributes+(4096+i)*4,0);
    for (unsigned layer = 0; layer < 2; ++layer) {
        const auto base = layer_base + layer * layer_stride, cells = cell_count(bytes, layer * 48,layer!=0);
        copy_bytes(memory_, base + 40, bytes, at, cells * 4); at += cells * 4;
        for (unsigned i = 0; i < cells; ++i) {
            const auto raw = word(bytes, at + i * 4), index = raw & 65535;
            if (index >= 0x104) throw Fault(at + i * 4, "MAP attribute outside original template table");
            memory_.write(base + 0x4028 + i * 4, index);
            const auto attribute = (memory_.read(tables::tile_attribute_templates + index * 4) & ~31u) | ((raw >> 16) & 31u);
            memory_.write(globals::tile_attributes + (layer * 4096 + i) * 4, attribute);
        }
        at += cells * 4;
    }
    if (at != bytes.size()) throw Fault(at, "unconsumed MAP payload");
}
void Map::load_event_mfo(const std::vector<std::uint8_t>& bytes) {
    // Original MFO initialization touches only the named fields, not a blanket
    // battleset memset (notably battlearea's other tuple components survive).
    memory_.write(0x77ec3c, 0xffffffff); memory_.write(0x7757dc, 0);
    for (Address record = 0x7744bc; record < 0x7755bc; record += 0x220) {
        for (auto field : {-1, 6, 0x86}) memory_.write(record + field * 4, 0xffffffff);
        for (auto field : {0, 1, 4, 0x80}) memory_.write(record + field * 4, 0);
        for (unsigned i = 0; i < 5; ++i) memory_.write(record + (8 + i * 4) * 4, 0);
        for (unsigned i = 0; i < 20; ++i) { memory_.write(record + (0x1c + i * 5) * 4, 0xffffffff); memory_.write(record + (0x1d + i * 5) * 4, 0); }
        memory_.write(record + 0x85 * 4, 1, 1);
    }
    Actors(memory_).reset_sequence_list();
    std::istringstream input(std::string(bytes.begin(), bytes.end())); std::string line;
    unsigned pcpos=0,jump=0,animation=0,battle=0;bool in_battle=false;
    memory_.write(globals::tile_animation_count, 0); memory_.write(globals::camera_bounds_enabled, 1);
    while (std::getline(input, line)) {
        const auto comment = line.find("//"); if (comment != std::string::npos) line.resize(comment);
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line); std::string kind; if (!(row >> kind) || kind == "mapname") continue;
        std::transform(kind.begin(),kind.end(),kind.begin(),[](unsigned char c){return char(c>='A'&&c<='Z'?c+32:c);});
        if(kind=="["){
            std::string block,index,close;if(!(row>>block>>index>>close)||block!="battleset"||close!="]")throw Fault(0,"invalid MFO battleset block");
            if(index=="end")in_battle=false;
            else{battle=number(index,false);if(battle>=8)throw Fault(battle,"MFO battleset exceeds original record pool");in_battle=true;memory_.write(0x7757dc,memory_.read(0x7757dc)+1);}
            continue;
        }
        if(kind=="camera_range_check"){
            std::string equals,value;if(!(row>>equals>>value)||equals!="="||(value!="true"&&value!="false"))throw Fault(0,"invalid MFO camera range option");
            memory_.write(globals::camera_bounds_enabled,value=="true");continue;
        }
        if(in_battle){
            std::string token;if(!(row>>token))throw Fault(battle,"missing MFO battle assignment");
            unsigned slot=0;const bool indexed=token!="=";
            if(indexed){slot=number(token,false);if(!(row>>token)||token!="=")throw Fault(battle,"invalid MFO battle slot assignment");}
            std::vector<std::string> values;while(row>>token)values.push_back(token);
            if(values.empty())throw Fault(battle,"missing MFO battle value");
            const auto at=battle*0x220;
            const auto store=[&](Address base,unsigned count){if(values.size()!=count)throw Fault(base,"invalid MFO battle tuple length");for(unsigned i=0;i<count;++i)memory_.write(base+at+i*4,number(values[i],false));};
            if(kind=="event"){
                if(values.size()!=1)throw Fault(battle,"invalid MFO event field");
                const auto id=values[0]=="no"?0xffffffffu:number(values[0],false);memory_.write(0x7744b8+at,id);
                if(id!=0xffffffffu&&signed32(memory_.read(0x77ec3c))<0)memory_.write(0x77ec3c,battle);
            }else if(kind=="group")store(0x7744bc,1);
            else if(kind=="partylevel")store(0x7744c0,1);
            else if(kind=="battlearea"){if(values[0]!="auto")store(0x7744c4,4);}
            else if(kind=="escape"){
                if(values.size()!=1||(values[0]!="yes"&&values[0]!="no"))throw Fault(battle,"invalid MFO escape field");
                memory_.write(0x7746d0+at,values[0]=="yes",1);
            }else if(kind=="enemytype")store(0x7746c0,4);
            else if(kind=="player"){
                if(values[0]!="auto"){if(!indexed||slot>=5)throw Fault(slot,"invalid MFO player slot");store(0x7744dc+slot*16,4);}
            }else if(kind=="enemy"){
                if(values[0]!="auto"){if(!indexed||slot>=20)throw Fault(slot,"invalid MFO enemy slot");store(0x77452c+slot*20,5);}
            }else throw Fault(battle,"unknown MFO battleset field: "+kind);
            continue;
        }
        // Original keyword dispatcher ignores unknown top-level lines. The
        // TMO6___P column heading is deliberately un-commented in the resource.
        if(kind!="scroll"&&kind!="pcpos"&&kind!="jmp"&&kind!="anime")continue;
        unsigned id; std::string equals;
        if (!(row >> id >> equals) || equals != "=") throw Fault(0, "invalid MFO assignment");
        if (kind == "scroll") {
            if (id >= memory_.read(globals::background_layer_count)) throw Fault(id, "MFO scroll layer outside active map");
            constexpr unsigned fields[] = {0, 1, 4, 5, 6, 7, 8, 9, 10, 11, 12};
            for (unsigned i = 0; i < 11; ++i) {
                std::string value; if (!(row >> value)) throw Fault(id, "incomplete MFO scroll row");
                memory_.write(scroll_base + id * 52 + fields[i] * 4, number(value, i != 4 && i != 5 && i != 10));
            }
        } else if (kind == "pcpos") {
            if (id != pcpos++ || id >= 64) throw Fault(id, "invalid MFO pcpos sequence");
            for (unsigned i = 0; i < 6; ++i) {
                std::string value; if (!(row >> value)) throw Fault(id, "incomplete MFO pcpos row");
                memory_.write(0x7ab9a0 + id * 24 + i * 4, number(value, false));
            }
        }else if(kind=="jmp"){
            if(id!=jump)continue;if(id>=32)throw Fault(id,"MFO jump table exceeds32 entries");
            for(unsigned i=0;i<8;++i){std::string value;if(!(row>>value))throw Fault(id,"incomplete MFO jump tuple");memory_.write(0x7ab5a0+id*32+i*4,number(value,false));}++jump;
        }else if(kind=="anime"){
            if(id!=animation)continue;if(id>=capacity::tile_animation_records)throw Fault(id,"MFO animation table exceeds256 entries");
            const auto at=globals::tile_animations+id*80;
            for(unsigned i=0;i<4;++i){std::string value;if(!(row>>value))throw Fault(id,"incomplete MFO animation header");memory_.write(at+i*4,decimal_number(value));}
            const auto count=memory_.read(at+4);if(count>16)throw Fault(id,"MFO animation exceeds16 frames");
            for(unsigned i=0;i<count;++i){std::string value;if(!(row>>value))throw Fault(id,"incomplete MFO animation frames");memory_.write(at+16+i*4,decimal_number(value));}
            memory_.write(globals::tile_animation_count,++animation);
        }else throw Fault(0,"unknown MFO map record: "+kind);
        std::string extra; if (row >> extra) throw Fault(0, "extra MFO assignment values");
    }
}
void Map::load_assets(unsigned map_id, const MapAssets& assets) {
    if (map_id > 1023) throw Fault(map_id, "map id outside addressable catalog scope");
    for (unsigned layer = 0; layer < 5; ++layer) for (unsigned i = 0; i < 48; ++i)
        memory_.write(layer_base + layer * layer_stride + i, 0, 1);
    memory_.write(0x800db0, 0x7f0d58); memory_.write(0x800db4, 0x7f8d80);
    for (unsigned plane = 0; plane < 2; ++plane) {
        sheets_[plane] = Image8::pcx(assets.files[plane * 2]);
        if(sprites_){
            std::string resource;for(auto c:memory_.bytes(0x5c4f44+map_id*68,9)){if(!c)break;resource.push_back(char(c));}
            resource+=plane?"s.pcx":"p.pcx";
            sprites_->replace_sheet(0x804d10+plane*68,resource,assets.files[plane*2],64,48);
        }
        load_mat(plane, assets.files[plane * 2 + 1]);
    }
    for (unsigned i = 0; i < 256; ++i) {
        const auto color = sheets_[0].palette[i];
        memory_.write(globals::decoded_image_palette + i * 4, color.r | std::uint32_t(color.g) << 8 | std::uint32_t(color.b) << 16);
    }
    for (Address target : {unsigned(globals::palette_target), unsigned(globals::overlay_palette_cache)}) {
        for (unsigned i = 0x10; i < 0x70; ++i) memory_.write(target + i * 4, memory_.read(globals::decoded_image_palette + i * 4));
        for (unsigned i = 0; i < 15; ++i) memory_.write(target + 4 + i * 4, memory_.read(0x5ab214 + i * 4));
        for (unsigned i = 0; i < 8; ++i) memory_.write(target + 0x3e0 + i * 4, memory_.read(0x5ab250 + i * 4));
    }
    const auto packed = memory_.read(0x5c4f40 + map_id * 0x44);
    if (packed > 3) throw Fault(map_id, "invalid map layer count");
    memory_.write(globals::parallax_layer_index, packed / 2); memory_.write(globals::background_layer_count, packed / 2 + 1);
    memory_.write(globals::active_map_layer_count, packed % 2 + packed / 2 + 1); memory_.write(globals::current_map_id, map_id);
    load_layout(assets.files[4]);
    for (unsigned layer = 0; layer < 2; ++layer) {
        const auto base = layer_base + layer * layer_stride, desc = scroll_base + layer * 52;
        const auto width = memory_.read(base + 24), height = memory_.read(base + 28);
        const std::uint32_t values[] = {0, 0, width << 22, height * 0x300000, (width / 2) << 16, (height / 2) << 16, 1, 1, 65536, 65536, 0, 0, 0};
        for (unsigned i = 0; i < 13; ++i) memory_.write(desc + i * 4, values[i]);
    }
    load_event_mfo(assets.files[5]);
    Address collected = 0x8019d8; unsigned count = 0, index = 16;
    for (Address src = 0x5b39a4; src < 0x5be6d4; src += 68, ++index) {
        memory_.write(src + 60, 0);
        if (memory_.read(src) == map_id && (memory_.read(src + 11, 1) & 128)) {
            if (count >= 50) throw Fault(src, "sequence actor collection over capacity");
            memory_.write(collected - 8, index);
            const unsigned fields[] = {4,5,6,7,9,10,11,12};
            for (unsigned i = 0; i < 8; ++i) memory_.write(collected + i * 4, memory_.read(src + fields[i] * 4));
            ++count; collected += 40;
        }
    }
    memory_.write(globals::collected_actor_count, count);
    const auto desc = scroll_base + memory_.read(globals::parallax_layer_index) * 52;
    const int ox = signed32(memory_.read(desc)) / units::q16_one, oy = signed32(memory_.read(desc + 4)) / units::q16_one;
    const int ex = signed32(memory_.read(desc + 8) + memory_.read(desc)) / units::q16_one;
    const int ey = signed32(memory_.read(desc + 12) + memory_.read(desc + 4)) / units::q16_one;
    memory_.write(globals::camera_scroll_min_x, std::uint32_t(ox + 64)); memory_.write(globals::camera_scroll_min_y, std::uint32_t(oy + 48));
    memory_.write(globals::camera_scroll_max_x, std::uint32_t(ex - 64) - memory_.read(0x5c4ec8));
    memory_.write(globals::camera_scroll_max_y, std::uint32_t(ey - 48) - memory_.read(0x5c4ecc));
    memory_.write(0x7873c8, std::uint32_t(ex - 64) - memory_.read(globals::viewport_width) / 64 * 64);
    memory_.write(0x7873cc, std::uint32_t(ey - 48) - memory_.read(globals::viewport_height) / 48 * 48);
    memory_.write(0x7873d0, std::uint32_t(ox + 64)); memory_.write(0x7873d4, std::uint32_t(oy + 48));
    memory_.write(globals::clip_left, memory_.read(globals::viewport_origin_x)); memory_.write(globals::clip_top, memory_.read(globals::viewport_origin_y));
    memory_.write(globals::clip_right, memory_.read(globals::framebuffer_width) - memory_.read(globals::viewport_origin_x)); memory_.write(globals::clip_bottom, memory_.read(globals::framebuffer_height) - memory_.read(globals::viewport_origin_y));
    memory_.write(globals::move_cost_grid_valid, 0); memory_.write(globals::grid_row_stride, memory_.read(layer_base + 24));
    memory_.write(globals::grid_height, memory_.read(layer_base + 28));
}
void Map::tick_tile_animation(){
    const auto layers=memory_.read(globals::background_layer_count), active=memory_.read(globals::active_map_layer_count);
    if(layers>2||active>2)throw Fault(globals::background_layer_count,"frame map layers exceed Event0 renderer scope");
    //0x45726d consumes the original3DWORD per-cell animation lookup.
    for(unsigned layer=0;layer<layers;++layer){
        const auto cells=memory_.read(layer_base+layer*layer_stride+24)*memory_.read(layer_base+layer*layer_stride+28);
        if(cells>4096)throw Fault(layer,"invalid frame map dimensions");
        for(unsigned cell=0;cell<cells;++cell){
            const auto lookup=globals::tile_animation_lookup+layer*0xc000+cell*12, index=memory_.read(lookup), cycles=memory_.read(lookup+4);
            if(signed32(index)<0||!cycles)continue;
            if(index>=capacity::tile_animation_records)throw Fault(index,"animated tile descriptor outside original MFO table");
            const auto animation=globals::tile_animations+index*80, delay=memory_.read(animation+12), count=memory_.read(animation+4), frame=memory_.read(lookup+8);
            const auto selected=sequence_alu(alu::signed_divide,frame,delay);
            if(selected>=16)throw Fault(lookup,"animated tile frame outside source record");
            memory_.write(layer_base+layer*layer_stride+40+cell*4,memory_.read(animation+16+selected*4));
            memory_.write(lookup+8,frame+1);
            if(signed32(delay*count)<=signed32(frame+1)){memory_.write(lookup+8,0);if(signed32(cycles)>0)memory_.write(lookup+4,cycles-1);}
        }
    }
}
void Map::update_occupancy(){
    const auto active=memory_.read(globals::active_map_layer_count);
    if(active>2)throw Fault(active,"occupancy map layers exceed Event0 scope");
    //0x45584f keep high flags;0x4558bf stamps the first90 rendered actor slots.
    // Same &0xfffe0000 over one contiguous run: clear the low two bytes and one
    // bit of the third. Done through a span so the run costs a single address
    // lookup instead of eight thousand read/write pairs every frame.
    if(const auto cells=active*4096){
        const auto words=memory_.span(globals::tile_occupancy,std::size_t(cells)*4);
        for(unsigned cell=0;cell<cells;++cell){const auto word=words.subspan(std::size_t(cell)*4,4);word[0]=0;word[1]=0;word[2]&=0xfe;}
    }
    const auto stamp=[&](Address actor,bool cached){
        const auto layer=std::uint32_t(signed32(memory_.read(actor+0x1c))/units::q16_one);
        if(layer>=active)throw Fault(actor,"occupancy actor layer outside map");
        const auto width=memory_.read(layer_base+layer*layer_stride+24);
        const auto x=cached?memory_.read(actor+0x128):std::uint32_t(signed32(memory_.read(actor+0x14))/units::q16_one);
        const auto y=cached?memory_.read(actor+0x12c):std::uint32_t(signed32(memory_.read(actor+0x18))/units::q16_one);
        const auto cell=layer*4096+y*width+x;
        memory_.write(globals::tile_occupancy+cell*4,((actor-globals::actor_objects)/428)|0x10000);
    };
    for(unsigned i=0;i<90;++i){const auto actor=Actors::slot(i);if(memory_.read(actor+4)&64){stamp(actor,false);stamp(actor,true);}}
    // 45514b uses definition coordinates for static markers, even if the
    // rendered object's position differs. Dynamic markers use live positions.
    for(unsigned i=0;i<memory_.read(globals::sparkle_count);++i){
        const auto definition=globals::sparkle_definitions+memory_.read(globals::sparkle_active_definitions+i*8)*10;
        const auto byte=[&](unsigned offset){const auto v=memory_.read(definition+offset,1);return v<128?int(v):int(v)-256;};
        const auto z=std::uint32_t(byte(4)),x=std::uint32_t(byte(2)),y=std::uint32_t(byte(3));
        const auto cell=z*4096+y*memory_.read(globals::map_layer_width+z*layout::map_layer_stride)+x;
        const auto object=memory_.read(globals::sparkle_active_objects+i*8);
        memory_.write(globals::tile_occupancy+cell*4,((object-globals::actor_objects)/layout::actor_size)|0x10000);
    }
    for(Address at=0x7ab560;at<0x7ab5a0;at+=4)if(const auto actor=memory_.read(at))stamp(actor,false);
}
void Map::register_assets(unsigned id, MapAssets assets) {
    if (id > 1023) throw Fault(id, "map id outside addressable catalog scope");
    registered_[id] = std::move(assets);
}
void Map::apply_patch(unsigned id,bool flag){
    if(signed32(id)<0)return;
    if(id>=0x199)throw Fault(id,"map patch id outside original table");
    const auto row=tables::map_patches+id*36;
    if(memory_.read(row)==memory_.read(globals::current_map_id)){
        const auto data=memory_.read(row+(flag?24:28));
        const int x=signed32(memory_.read(row+4)),y=signed32(memory_.read(row+8)),width=signed32(memory_.read(row+16)),height=signed32(memory_.read(row+20));
        const auto stride=memory_.read(globals::grid_row_stride);
        if(data&&height>0&&width>0){
            if(std::int64_t(width)*height>4096)throw Fault(row,"patch rectangle exceeds original plane capacity");
            for(int py=0;py<height;++py)for(int px=0;px<width;++px){
                const auto cell=std::uint32_t(y+py)*stride+std::uint32_t(x+px),source=data+std::uint32_t(py*width+px)*16;
                if(cell>=4096)throw Fault(row,"patch cell outside original plane");
                //45733a always updates the paired planes; descriptor+12 is unused.
                for(unsigned layer=0;layer<2;++layer){
                    const auto raw=memory_.read(source+layer*8+4),index=raw&0xffff;const auto format=signed32(raw)>>16;
                    const auto target=layer_base+layer*layer_stride+40+cell*4;
                    memory_.write(target,memory_.read(source+layer*8));memory_.write(target+0x4000,index);
                    const auto attr=index<0x104?(memory_.read(tables::tile_attribute_templates+index*4)&~31u)|((std::uint32_t(format/4)&7)<<2)|(std::uint32_t(format%4)&3):0;
                    memory_.write(globals::tile_attributes+(layer*4096+cell)*4,attr);
                }
            }
        }
    }
    //497065 finishes through496fe8/49700e, so the flag lives with the bitmap.
    map_logic::EventFlags flags(memory_);
    if(flag)flags.set(std::int32_t(id));else flags.clear(std::int32_t(id));
}
void Map::load_registered(unsigned id){const auto found=registered_.find(id);if(found!=registered_.end())load_assets(id,found->second);else if(asset_provider)load_assets(id,asset_provider(id));else throw Fault(id,"map resource bundle not registered");}
void Map::switch_map(unsigned id, std::uint32_t flags) {
    rebuild(id,flags,std::nullopt);
}
void Map::enter_field(unsigned id,unsigned entry){rebuild(id,1,entry);}
void Map::rebuild(unsigned id,std::uint32_t flags,std::optional<unsigned> entry){
    Actors fallback(memory_);auto& actors=actor_service_?*actor_service_:fallback;
    memory_.write(0x5f858c, 0xffffffff);
    for (Address at = 0x7ab5ac; at < 0x7ab9ac; at += 32) memory_.write(at, 0xffffffff);
    for (Address at = 0x7ab9b0; at < 0x7abcb0; at += 24) memory_.write(at, 0xffffffff);
    actors.reset_range(memory_.read(globals::party_count), 0x59);
    for (unsigned index = 0; index < 768; ++index) for (unsigned offset = 0x2c; offset <= 0x38; offset += 4) memory_.write(Actors::slot(index) + offset, 0);
    for (unsigned offset = 0; offset < 0xc00; offset += 4) memory_.write(globals::overlay_effects + offset, 0);
    load_registered(id);
    if(flags&1)actors.spawn_collected();
    for (unsigned i = 0; i < memory_.read(globals::sparkle_count); ++i) { const auto obj = memory_.read(globals::sparkle_active_objects + i * 8); if (obj) actors.finalize(obj); }
    // Run the original table selection, rather than silently assuming the map
    // has no sparkle/patch records. Original map469 selects none.
    unsigned marker_count=0;
    for (unsigned i = 0; i < 4096; ++i) {
        const auto definition=globals::sparkle_definitions+i*10;
        const auto raw = memory_.read(definition, 2); const int map = raw < 32768 ? int(raw) : int(raw) - 65536;
        if (map > int(id)) break;
        if (map == int(id)) {
            if(marker_count>=64)throw Fault(id,"active map marker table exhausted");
            const auto object=actors.spawn_map_marker();
            memory_.write(object+actor_offset::flags,memory_.read(object+actor_offset::flags)|0x40);
            memory_.write(object+actor_offset::sprite_base,0xd0);memory_.write(object+actor_offset::sprite_selector,0xcb);
            const auto byte=[&](unsigned offset){const auto v=memory_.read(definition+offset,1);return v<128?int(v):int(v)-256;};
            const auto visited=(memory_.read(globals::collected_sparkle_bits+(i/32)*4)>>(i&31))&1;
            memory_.write(object+actor_offset::sprite_frame,std::uint32_t(byte(9)*2)+visited);
            const auto x=byte(2),y=byte(3),z=byte(4);set_actor_tile_position(memory_,object,x,y,z);
            const auto cell=std::uint32_t(z)*4096+std::uint32_t(y)*memory_.read(globals::grid_row_stride)+std::uint32_t(x);
            const auto collision=globals::tile_attributes+cell*4+1;
            memory_.write(collision,memory_.read(collision,1)|2,1);
            memory_.write(globals::sparkle_active_definitions+marker_count*8,i);
            memory_.write(globals::sparkle_active_objects+marker_count*8,object);++marker_count;
        }
        if (i == 4095) throw Fault(id, "sparkle table has no terminator");
    }
    memory_.write(globals::sparkle_count, marker_count);
    for (unsigned i = 0; i < 0x199; ++i) if (memory_.read(tables::map_patches + i * 36) == id)
        apply_patch(i,(memory_.read(globals::event_flag_bits+(i/32)*4)&(1u<<(i&31)))!=0);
    const auto callback = memory_.read(0x5c4f60 + id * 68);
    if(callback==routines::palace_trigger_setup){
        // Original map471 registers a dormant trigger pair at(17,5).
        // Registration bit24 is distinct from the active callback bit25.
        for(auto patch:{0x195u,0xffffffffu}){
            Address slot=globals::overlay_effects;while(slot<globals::overlay_effects_end&&(memory_.read(slot)&0x1000000))slot+=48;
            if(slot>=globals::overlay_effects_end)throw Fault(callback,"overlay trigger slots exhausted");
            const std::uint32_t fields[]={0x100010f,17,5,17,5,routines::map_trigger_tick,0,0};
            for(unsigned i=0;i<8;++i)memory_.write(slot+i*4,fields[i]);
            memory_.write(slot+32,patch);memory_.write(slot+36,0);memory_.write(slot+44,0xffffffffu);memory_.write(globals::current_overlay_effect,slot);
        }
    }else if(callback&&callback!=routines::empty_map_callback&&!setup_field_overlays(callback)){
        if(!setup_callback)throw Fault(callback,"map callback is not connected");setup_callback(callback);
    }
    rebuild_animation_lookup();
    for (Address logical = 0x8572e8; logical < 0x8577ec; logical += 0x1ac) {
        memory_.write(logical, 0x10000); memory_.write(logical + 4, 0x10000); memory_.write(logical + 8, 0x18000);
        memory_.write(logical - 12, 0x400000); memory_.write(logical - 8, 0x300000); memory_.write(logical - 16, 0x1000); memory_.write(logical + 0x108, 0xffffffff);
        for (auto offset : {12,16,20,24,28,32,36,0xf0,0xf4,0xf8,0xfc,0x100,0x104,0x10c,0x110,0x114,0x118}) memory_.write(logical + offset, 0);
    }
    for (unsigned i = 0; i < 33; ++i) memory_.write(0x7873d8 + i * 4, 0);
    if(!entry)memory_.write(0x7ab9ac,0xffffffffu);
    memory_.write(0x7873b8,0);memory_.write(0x773f80,0);
    restore_entry(entry.value_or(0));
    memory_.write(0x804aac,0);
    for(unsigned i=0x70;i<0xe0;++i)memory_.write(globals::palette_target+i*4,memory_.read(globals::overlay_palette_cache+i*4));
}
void Map::rebuild_animation_lookup(){
    for (unsigned layer = 0; layer < memory_.read(globals::background_layer_count); ++layer) {
        const auto cells = memory_.read(layer_base + layer * layer_stride + 24) * memory_.read(layer_base + layer * layer_stride + 28);
        for (unsigned i = 0; i < cells * 3; ++i) memory_.write(globals::tile_animation_lookup + layer * 0xc000 + i * 4, 0xffffffff);
        for(unsigned cell=0;cell<cells;++cell){
            const auto tile=memory_.read(globals::map_tile_codes+layer*layout::map_layer_stride+cell*4),lookup=globals::tile_animation_lookup+layer*0xc000+cell*12;
            for(unsigned index=0;index<memory_.read(globals::tile_animation_count);++index){
                const auto animation=globals::tile_animations+index*80;
                if(memory_.read(animation+16)==tile){memory_.write(lookup,index);memory_.write(lookup+4,memory_.read(animation+8));memory_.write(lookup+8,0);}
            }
        }
    }
}
void Map::restore_entry(unsigned entry){
    if(entry>=32)throw Fault(entry,"field entry outside original pcpos table");
    memory_.write(0x803a28,entry);const auto position=0x7ab9a0+entry*24;
    const auto active = memory_.read(globals::active_party_index), actor = Actors::slot(active);
    memory_.write(globals::camera_focus_actor_index, active); memory_.write(actor + 4, memory_.read(actor + 4) | 0x100c0);
    set_actor_tile_position(memory_,actor,signed32(memory_.read(position)),signed32(memory_.read(position+4)),signed32(memory_.read(position+8)));
    for (unsigned offset = 0x2c; offset <= 0x38; offset += 4) memory_.write(actor + offset, 0);
    memory_.write(actor+actor_offset::callback,routines::field_actor_movement);
    Actors(memory_).apply_entry_direction(actor,memory_.read(position+12));
}
void Map::update_scroll() {
    const auto active = memory_.read(globals::parallax_layer_index);
    if (active > 1) throw Fault(active, "invalid active parallax layer");
    const auto active_desc = scroll_base + active * 52;
    const auto target_x = memory_.read(globals::camera_x) - std::uint32_t(signed32(memory_.read(active_desc + 16)) / units::q16_one) * 64;
    const auto target_y = memory_.read(globals::camera_y) - std::uint32_t(signed32(memory_.read(active_desc + 20)) / units::q16_one) * 48;
    for (unsigned layer = 0; layer < 2; ++layer) {
        const auto desc = scroll_base + layer * 52, base = layer_base + layer * layer_stride;
        for (unsigned axis = 0; axis < 2; ++axis) {
            const auto tile_size = axis ? 48 : 64;
            const auto divisor = sequence_alu(alu::arithmetic_shift_right, memory_.read(active_desc + 32 + axis * 4), 8);
            const auto factor = sequence_alu(alu::signed_divide, memory_.read(desc + 32 + axis * 4) << 8, divisor);
            auto origin = factor * (axis ? target_y : target_x) + memory_.read(desc + 16 + axis * 4) * tile_size;
            origin += memory_.read(desc + 40 + axis * 4) * memory_.read(desc + 48);
            if (memory_.read(desc + 24 + axis * 4) == 0xffffffffu) {
                const auto extent = memory_.read(base + 24 + axis * 4) * tile_size * units::q16_one;
                if (signed32(origin) < 0) origin += extent;
                else if (signed32(origin) >= signed32(extent)) origin -= extent; // One wrap, not arbitrary modulo.
            }
            memory_.write(desc + axis * 4, origin);
        }
        memory_.write(desc + 48, memory_.read(desc + 48) + 1);
    }
}
std::vector<TileCommand> Map::background_commands(unsigned layer,std::uint32_t effect_mask) {
    if (layer >= memory_.read(globals::background_layer_count) || layer > 1) throw Fault(layer, "invalid background layer");
    const auto desc = scroll_base + layer * 52;
    const auto width = signed32(memory_.read(globals::viewport_width)), height = signed32(memory_.read(globals::viewport_height));
    const auto grid_w = signed32(memory_.read(globals::grid_row_stride)), grid_h = signed32(memory_.read(globals::grid_height));
    if (width < 1 || height < 1 || width > 4096 || height > 4096 || grid_w < 1 || grid_h < 1) throw Fault(layer, "invalid map viewport/grid");
    const auto pixel_x = signed32(memory_.read(desc)) / units::q16_one - width / 2;
    const auto pixel_y = signed32(memory_.read(desc + 4)) / units::q16_one - height / 2;
    const auto rx = (pixel_x % 64 + 64) % 64, ry = (pixel_y % 48 + 48) % 48;
    const auto col0 = pixel_x / 64 - (pixel_x < 0 && rx != 0), row0 = pixel_y / 48 - (pixel_y < 0 && ry != 0);
    const auto x0 = signed32(memory_.read(globals::viewport_left)) - rx, y0 = signed32(memory_.read(globals::viewport_top)) - ry;
    const auto columns = width / 64 + (rx != 0) + (width % 64 != 0), rows = height / 48 + (ry != 0) + (height % 48 != 0);
    memory_.write(0x8021bc, std::uint32_t(x0)); memory_.write(0x8021c0, std::uint32_t(y0));
    std::vector<TileCommand> commands;
    const auto policy = memory_.read(desc + 24);
    for (int y = 0; y < rows; ++y) for (int x = 0; x < columns; ++x) {
        const auto row = row0 + y, col = col0 + x;
        const bool inside = row >= 0 && col >= 0 && row < grid_h && col < grid_w;
        std::uint32_t tile,cell_effect=0;
        if (policy == 0xffffffffu) {
            const auto stride = memory_.read(layer_base + layer * layer_stride + 24);
            tile = memory_.read(layer_base + layer * layer_stride + 40 + (((row % grid_h + grid_h) % grid_h) * stride + (col % grid_w + grid_w) % grid_w) * 4);
        } else if (inside){tile = memory_.read(layer_base + layer * layer_stride + 40 + (row * grid_w + col) * 4);cell_effect=memory_.read(globals::battle_grid_flags+(row*grid_w+col)*4)&effect_mask;}
        else if (!policy) continue; else tile = policy;
        if (tile >= 4096) throw Fault(tile, "tile exceeds MAT table capacity");
        const auto flags = memory_.read(0x7d8ca0 + tile * 4);
        commands.push_back({layer, layer * 2, tile, flags, x0 + x * 64, y0 + y * 48, (y0 + 1) * 48 + y * 0x900,{}});
        // Original45555b only highlights in-bounds, non-modulo cells.
        if(cell_effect){commands.back().flags=0;commands.back().checker_fill=std::uint8_t(cell_effect+223);}
    }
    return commands;
}
const Image8& Map::sheet(unsigned plane) const {
    if (plane >= 2 || sheets_[plane].pixels.empty()) throw Fault(plane, "map sheet is not loaded");
    return sheets_[plane];
}
void Map::draw_background(Image8& target, const std::vector<TileCommand>& commands) const {
    const auto& source = sheet(0); const auto columns = source.width / 64, frames = columns * (source.height / 48);
    if (!frames) throw Fault(0, "map sheet contains no complete tile");
    for (const auto& command : commands) {
        if (command.flags == 0xffffffffu) continue;
        if (command.flags > 1) throw Fault(command.tile, "unsupported tile blit flags");
        const auto frame_index = command.tile < frames ? command.tile : 0; // Original frame0 fallback.
        SpriteFrame frame; frame.x = int(frame_index % columns) * 64; frame.y = int(frame_index / columns) * 48; frame.width = 64; frame.height = 48;
        const Rect clip{signed32(memory_.read(globals::clip_left)), signed32(memory_.read(globals::clip_top)),
                        signed32(memory_.read(globals::clip_right)), signed32(memory_.read(globals::clip_bottom))};
        if(command.checker_fill)draw_checker_tile(target,source,{frame.x,frame.y,frame.x+64,frame.y+48},command.x,command.y,false,*command.checker_fill,clip);
        else draw_sprite(target, source, frame, command.x, command.y, command.flags != 0, clip);
    }
}
} // namespace fsb::core
