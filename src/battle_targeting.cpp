#include "fsb_core/battle_rules.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/camera.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>

namespace fsb::core {
unsigned BattleRules::status(unsigned context)const{
    const auto actor=Actors::slot(context),flags=memory_.read(actor+4);
    if(flags&0x100)return memory_.read(party_record(memory_.read(globals::party_actor_ids+context*4))+8);
    if(flags&0x400)return memory_.read(enemy_record(memory_.read(actor+0x118))+8);return 0;
}
void BattleRules::prepare_vitality(){
    for(unsigned i=0;i<16;++i)memory_.write(0x776418+i*4,memory_.read(party_record(i)+0x1c));
    for(unsigned i=0;i<memory_.read(0x776484);++i)memory_.write(0x77a518+i*4,memory_.read(enemy_record(i)+20));
}
void BattleRules::commit_vitality(){
    const auto apply=[&](Address destination,unsigned value,unsigned maximum){if(signed32(value)<0)value=0;if(signed32(maximum)<signed32(value))value=maximum;memory_.write(destination,value);};
    for(unsigned i=0;i<16;++i)apply(party_record(i)+0x1c,memory_.read(0x776418+i*4),memory_.read(party_record(i)+0x18));
    for(unsigned i=0;i<memory_.read(0x776484);++i){const auto record=enemy_record(i);apply(record+20,memory_.read(0x77a518+i*4),memory_.read(monster_record(memory_.read(record+4))+24));}
}
void BattleRules::snapshot_party_panel(bool active){
    memory_.write(0x806500,1);
    for(unsigned i=0;i<memory_.read(globals::party_count);++i){
        const auto id=memory_.read(globals::party_actor_ids+i*4),record=party_record(id);
        memory_.write(0x805740+i*4,memory_.read(active?0x776418+id*4:record+0x1c));memory_.write(0x805780+i*4,memory_.read(record+0x24));memory_.write(0x805838+i*4,memory_.read(record+0x2c));memory_.write(0x805700+i*4,memory_.read(record+8));
    }
}
void BattleRules::snapshot_enemy_panel(bool active){
    memory_.write(0x806504,1);
    for(unsigned i=0;i<memory_.read(0x776484);++i){const auto record=enemy_record(i);memory_.write(0x805900+i*4,memory_.read(active?0x77a518+i*4:record+20));memory_.write(0x805978+i*4,memory_.read(record+24));}
}
void BattleRules::snapshot_panels(bool active){snapshot_party_panel(active);snapshot_enemy_panel(active);}
std::uint32_t BattleRules::take_poison_delta(unsigned context){
    const auto actor=Actors::slot(context),flags=memory_.read(actor+4);Address hp=0;
    if(flags&0x100)hp=party_record(memory_.read(globals::party_actor_ids+context*4))+0x1c;
    else if(flags&0x400)hp=enemy_record(memory_.read(actor+0x118))+20;else return 0;
    const auto value=memory_.read(hp),delta=sequence_alu(alu::signed_divide,value,4);memory_.write(hp,value-delta);return delta;
}
void BattleRules::apply_delta(unsigned context,std::uint32_t delta){
    const auto actor=Actors::slot(context),flags=memory_.read(actor+4);Address target=0;
    if(flags&0x100)target=0x776418+memory_.read(globals::party_actor_ids+context*4)*4;
    else if(flags&0x400)target=0x77a518+memory_.read(actor+0x118)*4;else return;
    memory_.write(target,memory_.read(target)+delta);
}
unsigned BattleRules::default_action(unsigned context,bool secondary)const{
    const auto actor=Actors::slot(context),flags=memory_.read(actor+4);
    if(flags&0x100)return memory_.read(party_record(memory_.read(globals::party_actor_ids+context*4))+(secondary?0x8c:0x88));
    if(flags&0x400)return memory_.read(monster_record(memory_.read(enemy_record(memory_.read(actor+0x118))+4))+0x48);return 0;
}
unsigned BattleRules::default_handler(unsigned context,bool secondary)const{
    const auto flags=memory_.read(Actors::slot(context)+4),action=default_action(context,secondary);
    if(flags&0x100)return memory_.read(0x608858+action*24);
    if(flags&0x400)return memory_.read(0x610c20+action*32);return 0;
}
void BattleRules::prepare_handler(unsigned handler,unsigned facing){
    const auto row=handler*24,source=0x5c3800+memory_.read(0x5c2094+row)*121,target=0x5c2a48+memory_.read(0x5c2090+row)*121;
    for(unsigned y=0;y<11;++y)for(unsigned x=0;x<11;++x){
        const unsigned indices[]={y*11+x,120-y*11-x,x*11+10-y,(10-x)*11+y};
        for(unsigned dir=0;dir<4;++dir){memory_.write(0x77fc18+dir*121+y*11+x,memory_.read(source+indices[dir],1),1);memory_.write(0x77fe00+dir*121+y*11+x,memory_.read(target+indices[dir],1),1);}
    }
    memory_.write(0x77fc10,facing);const auto x=memory_.read(0x5c2098+row),y=memory_.read(0x5c209c+row);unsigned dx,dy;
    switch(facing){case 0:dx=x;dy=y;break;case 1:dx=0u-x;dy=0u-y;break;case 2:dx=y;dy=0u-x;break;case 3:dx=0u-y;dy=x;break;default:return;}
    memory_.write(0x77fc08,dx);memory_.write(0x77fc0c,dy);
}
void BattleRules::clear_cursor(int x,int y,int radius,unsigned keep){
    const int width=signed32(memory_.read(globals::grid_row_stride)),height=signed32(memory_.read(globals::grid_height));
    for(int cy=std::max(0,y-radius);cy<std::min(height,y+radius+1);++cy)for(int cx=std::max(0,x-radius);cx<std::min(width,x+radius+1);++cx){const auto cell=0x7764e0+unsigned(cy*width+cx)*4;memory_.write(cell,memory_.read(cell)&keep);}
}
void BattleRules::mark_reachable(int maximum){
    const auto cells=memory_.read(globals::grid_row_stride)*memory_.read(globals::grid_height);
    for(unsigned i=0;i<cells;++i){const auto cost=memory_.read(globals::move_cost_grid+i*4);memory_.write(0x7764e0+i*4,signed32(cost)>=0&&int(cost&255)<=maximum);}
}
void BattleRules::rasterize_ai_regions(std::optional<unsigned> selected){
    constexpr Address grid=0x780260,counts=0x7865c0,spans=0x780000,candidates=0x786620;
    const auto width=memory_.read(globals::grid_row_stride),height=memory_.read(globals::grid_height);
    if(!width||!height||std::uint64_t(width)*height>4096)throw Fault(grid,"invalid AI raster dimensions");
    const auto cells=memory_.span(grid,width*height);std::fill(cells.begin(),cells.end(),0);
    const unsigned first=selected.value_or(0),end=selected?first+1:memory_.read(0x77fff8);
    const auto count=memory_.read(0x787398);
    if(first>=4||end>4||count>width*height)throw Fault(candidates,"invalid AI region/candidate count");
    for(unsigned region=first;region<end;++region){
        const auto segments=memory_.read(counts+region*4);
        if(segments>12)throw Fault(spans,"invalid AI region span count");
        for(unsigned i=0;i<count;++i){
            const auto candidate=candidates+i*24;
            const auto x=std::int64_t(signed32(memory_.read(candidate))),y=std::int64_t(signed32(memory_.read(candidate+4)));
            for(unsigned j=0;j<segments;++j){
                const auto span=spans+region*144+j*12;
                const auto row=y+signed32(memory_.read(span));
                const auto left=std::max<std::int64_t>(0,x+signed32(memory_.read(span+4)));
                const auto right=std::min<std::int64_t>(width-1,x+signed32(memory_.read(span+8)));
                if(row>=0&&row<height&&left<=right){const auto run=cells.subspan(std::size_t(row*width+left),std::size_t(right-left+1));std::fill(run.begin(),run.end(),1);}
            }
        }
    }
}
void BattleRules::preview(unsigned frames){
    const auto actor=Actors::slot(memory_.read(0x7757e0)),dummy=Actors::slot(764);
    const auto x=memory_.read(actor+20)+sequence_alu(alu::signed_divide,memory_.read(0x77fc08)*65536,2),y=(memory_.read(actor+24)+sequence_alu(alu::signed_divide,memory_.read(0x77fc0c)*65536,2))&0xffff8000u;
    for(auto b:{32,36,40,44,48,52,56,0x108,0x10c,0x110,0x114,0x118,0x120,0x124,0x128,0x12c})memory_.write(dummy+b,0);
    memory_.write(dummy+20,x);memory_.write(dummy+24,y);memory_.write(dummy+28,memory_.read(actor+28));memory_.write(dummy+8,x<<6);memory_.write(dummy+12,y*48);memory_.write(dummy+0x11c,0xffffffffu);memory_.write(dummy+4,0);memory_.write(dummy+0x148,0);
    Camera(memory_).line_focus(memory_.read(globals::camera_focus_actor_index),764,frames);
}
void BattleRules::project_action(int x,int y,bool shifted){
    const auto direction=memory_.read(0x77fc10);if(direction>=4)throw Fault(direction,"battle action mask facing outside four rotations");
    const int width=signed32(memory_.read(globals::grid_row_stride)),height=signed32(memory_.read(globals::grid_height)),dx=signed32(memory_.read(0x77fc08)),dy=signed32(memory_.read(0x77fc0c));
    const auto put=[&](int cx,int cy,unsigned bits){if(cx>=0&&cy>=0&&cx<width&&cy<height){const auto cell=0x7764e0+unsigned(cy*width+cx)*4;memory_.write(cell,memory_.read(cell)|bits);}};
    for(unsigned row=0;row<11;++row)for(unsigned column=0;column<11;++column){
        const auto index=direction*121+row*11+column;const int cx=x+int(column)-5,cy=y+int(row)-5;
        put(cx,cy,memory_.read(0x77fe00+index,1));if(shifted)put(cx+dx,cy+dy,memory_.read(0x77fc18+index,1)&~8u);
    }
    if(memory_.read(0x77ec4c,1)&4)preview(5);
}
bool BattleRules::shift_footprint(unsigned direction){
    if(direction>=4)throw Fault(direction,"invalid footprint move direction");constexpr int xs[]={0,0,-1,1},ys[]={-1,1,0,0};
    const int dx=xs[direction],dy=ys[direction],x=signed32(memory_.read(0x77fc08)),y=signed32(memory_.read(0x77fc0c));const auto facing=memory_.read(0x77fc10);
    for(int row=0;row<11;++row)for(int column=0;column<11;++column)if(memory_.read(0x77fc18+facing*121+unsigned(row*11+column),1)&8){
        const auto cx=column+x+dx,cy=row+y+dy;if(cx<0||cy<0||cx>10||cy>10||memory_.read(0x77fe00+facing*121+unsigned(cy*11+cx),1)!=2)return false;
    }
    memory_.write(0x77fc08,std::uint32_t(x+dx));memory_.write(0x77fc0c,std::uint32_t(y+dy));if(memory_.read(0x77ec4c,1)&4)preview(10);return true;
}
std::vector<std::uint32_t> BattleRules::collect_targets(Address actor,unsigned mask,unsigned excluded,std::optional<unsigned> required)const{
    const auto direction=memory_.read(0x77fc10);if(direction>=4)throw Fault(direction,"invalid target-mask facing");
    const int x=signed32(memory_.read(actor+0x128)),y=signed32(memory_.read(actor+0x12c)),z=signed32(memory_.read(actor+0x1c))/65536;
    const int width=signed32(memory_.read(globals::grid_row_stride)),height=signed32(memory_.read(globals::grid_height)),dx=signed32(memory_.read(0x77fc08)),dy=signed32(memory_.read(0x77fc0c));
    std::vector<std::uint32_t> targets;
    // The original keeps one status slot for the whole scan and only refreshes
    // it for party and enemy occupants, so a tile held by anything else is
    // weighed with the previous occupant's status. Starting it at zero is this
    // port's choice; the original reads its own uninitialised frame there.
    std::uint32_t status=0;
    for(int row=0;row<11;++row)for(int column=0;column<11;++column){
        if(!(memory_.read(0x77fc18+direction*121+unsigned(row*11+column),1)&4))continue;
        const int cx=x+column-5+dx,cy=y+row-5+dy;if(cx<0||cy<0||cx>=width||cy>=height)continue;
        const auto cell=unsigned(cy*width+cx),occupied=memory_.read(globals::tile_occupancy+(unsigned(z)*4096+cell)*4);if(!(occupied&0x10000))continue;
        const auto id=occupied&65535,target=Actors::slot(id),flags=memory_.read(target+4);
        if(flags&0x100)status=memory_.read(party_record(memory_.read(globals::party_actor_ids+memory_.read(target)*4))+8);else if(flags&0x400)status=memory_.read(enemy_record(memory_.read(target+0x118))+8);
        if((flags&mask)&&!(status&excluded)&&(!required||(status&*required)))targets.push_back(((memory_.read(0x7764e0+cell*4)&0xf0)<<12)|id);
    }
    return targets;
}
} // namespace fsb::core
