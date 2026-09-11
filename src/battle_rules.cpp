#include "fsb_core/battle_rules.hpp"
#include "fsb_core/actors.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
bool BattleRules::equipped(unsigned id,unsigned item)const{
    for(unsigned i=0;i<5;++i)if(memory_.read(party_record(id)+0x74+i*4)==item)return true;return false;
}
unsigned BattleRules::precheck()const{
    const auto party=memory_.read(globals::party_count),enemies=memory_.read(0x776484);unsigned down=0;
    for(unsigned i=0;i<party;++i)down+=(memory_.read(party_record(memory_.read(globals::party_actor_ids+i*4))+8)&battle_status::down)!=0;
    if(down==party)return 2;down=0;
    for(unsigned i=0;i<enemies;++i)down+=(memory_.read(enemy_record(i)+8)&battle_status::down)!=0;
    return down==enemies?1:0;
}
void BattleRules::decay_status(){
    const auto nibble=[&](Address timer,Address byte,unsigned shift,unsigned flag){
        const auto value=memory_.read(timer),mask=15u<<shift;
        if(value&mask){memory_.write(timer,(value&~mask)|(((value&mask)-(1u<<shift))&mask));memory_.write(byte,memory_.read(byte,1)|flag,1);}
    };
    for(unsigned i=0;i<memory_.read(globals::party_count);++i){
        const auto record=party_record(memory_.read(globals::party_actor_ids+i*4)),flags=memory_.read(record+8);
        if(flags&battle_status::unavailable)continue;
        memory_.write(record+8,flags&0xfffc00ffu);
        for(unsigned slot=0;slot<8;++slot)nibble(record+12,record+9,slot*4,1u<<slot);
        for(unsigned slot=0;slot<2;++slot)nibble(record+16,record+10,slot*4,1u<<slot);
    }
    for(unsigned i=0;i<memory_.read(0x776484);++i){
        const auto record=enemy_record(i),flags=memory_.read(record+8);if(flags&battle_status::unavailable)continue;
        memory_.write(record+8,flags&0xffffe0ffu);
        for(unsigned slot=0;slot<5;++slot)nibble(record+12,record+9,slot*4,1u<<slot);
        nibble(record+16,record+10,8,4); // Unnamed enemy slot5; separate from ordinary status slots.
    }
}
void BattleRules::advance_gauges(){
    for(unsigned i=0;i<memory_.read(globals::party_count);++i){
        const auto id=memory_.read(globals::party_actor_ids+i*4),record=party_record(id);auto gain=memory_.read(record+0x70);
        if(equipped(id,0x146)){const auto random=crt_rand(memory_);gain=sequence_alu(alu::signed_remainder,random,gain<<1);}
        if(signed32(memory_.read(record+0x1c))<1||(memory_.read(record+8)&battle_status::action_blocked))gain=0;
        else{
            const auto increment=[&](){const auto counter=signed32(memory_.read(record+0x2c));if(counter>=0&&counter<16)memory_.write(record+0x2c,unsigned(counter+1));};
            increment();if(equipped(id,0x145))increment();if(equipped(id,0xf4))increment();
        }
        if(memory_.read(record+9,1)&1)gain=sequence_alu(alu::signed_divide,gain*3,4);
        memory_.write(record+0x30,memory_.read(record+0x30)+gain);
    }
    for(unsigned i=0;i<memory_.read(0x776484);++i){
        const auto record=enemy_record(i);auto gain=memory_.read(monster_record(memory_.read(record+4))+0x40);
        if((memory_.read(record+8)&battle_status::action_blocked)||signed32(memory_.read(record+20))<1){gain=0;memory_.write(record+32,0);}
        if(memory_.read(record+8)&battle_status::poison)gain=sequence_alu(alu::signed_divide,gain*3,4);
        memory_.write(record+32,memory_.read(record+32)+gain);
    }
}
std::optional<unsigned> BattleRules::next_context(){
    std::optional<unsigned> selected;Address gauge=0;int best=20;
    for(unsigned i=0;i<memory_.read(globals::party_count);++i){
        const auto record=party_record(memory_.read(globals::party_actor_ids+i*4));const auto value=signed32(memory_.read(record+0x30));
        if(!(memory_.read(record+8)&battle_status::action_blocked)&&value>=best){best=value;selected=i;gauge=record+0x30;}
    }
    bool enemy=false;
    for(unsigned i=0;i<memory_.read(0x776484);++i){
        const auto record=enemy_record(i);const auto value=signed32(memory_.read(record+32));
        if(!(memory_.read(record+8)&battle_status::action_blocked)&&value>=best){best=value;selected=memory_.read(record);enemy=true;}
    }
    if(selected){
        if(enemy)gauge=enemy_record(memory_.read(Actors::slot(*selected)+0x118))+32;
        memory_.write(gauge,memory_.read(gauge)-20);
    }
    return selected;
}
void BattleRules::compute_rewards(){
    for(unsigned i=0;i<4;++i){memory_.write(0x77e5a8+i*4,0);memory_.write(0x774170+i*4,0xffffffffu);}
    memory_.write(0x77ebf8,0);memory_.write(0x77ec00,0);memory_.write(0x775c84,0);
    for(unsigned i=0;i<memory_.read(0x776484);++i){
        const auto monster=monster_record(memory_.read(enemy_record(i)+4));
        memory_.write(0x77ebf8,memory_.read(0x77ebf8)+memory_.read(monster+0x68));memory_.write(0x77ec00,memory_.read(0x77ec00)+memory_.read(monster+0x6c));
        const auto roll=crt_rand(memory_);if(int(roll%1000)>=signed32(memory_.read(monster+0x74)))continue;
        const auto count=memory_.read(0x775c84),item=memory_.read(monster+0x70);bool duplicate=false;
        for(unsigned j=0;j<count;++j)if(memory_.read(0x774170+j*4)==item){duplicate=true;break;}
        // Preserve44dd1c's actual tail-index write even on duplicate drops.
        memory_.write(0x77e5a8+count*4,memory_.read(0x77e5a8+count*4)+1);memory_.write(0x774170+count*4,item);
        if(!duplicate)memory_.write(0x775c84,count+1);
    }
    memory_.write(0x77ebf8,sequence_alu(alu::signed_divide,memory_.read(0x77ebf8)*145,100));memory_.write(0x77ec00,sequence_alu(alu::signed_divide,memory_.read(0x77ec00)*145,100));
}
int BattleRules::relation(Address attacker,Address target)const{
    const auto x=signed32(memory_.read(target+actor_offset::tile_x)-memory_.read(attacker+actor_offset::tile_x)),y=signed32(memory_.read(target+actor_offset::tile_y)-memory_.read(attacker+actor_offset::tile_y));
    if(x<-10||x>10||y<-10||y>10)return 0;int rx=0,ry=0;
    switch(memory_.read(target+actor_offset::facing)){case 0:rx=-x;ry=-y;break;case 1:rx=x;ry=y;break;case 2:rx=y;ry=-x;break;case 3:rx=-y;ry=x;break;}
    const auto value=memory_.read(0x5c04a4+std::uint32_t(rx+ry*21),1);return value<128?int(value):int(value)-256;
}
int BattleRules::relation_hit_bonus(unsigned relation){constexpr int values[]={0,8,15,23,30};if(relation>=5)throw Fault(relation,"invalid battle relation");return values[relation];}
int BattleRules::relation_damage_percent(unsigned relation){constexpr int values[]={100,110,120,135,150};if(relation>=5)throw Fault(relation,"invalid battle relation");return values[relation];}
int BattleRules::counter_chance(unsigned row,unsigned relation){
    constexpr int values[][5]={{30,28,25,22,20},{20,18,15,12,10},{10,8,5,3,1}};
    if(row>=3||relation>=5)throw Fault(row,"invalid counter relation");return values[row][relation];
}
int BattleRules::guard_percent(unsigned attacker,unsigned target){
    const auto element=[](unsigned flags){switch(flags&0xf000000){case 0x1000000:return 1;case 0x2000000:return 2;case 0x4000000:return 3;case 0x8000000:return 4;default:return 0;}};
    constexpr int matrix[][5]={{100,100,100,100,100},{100,100,170,100,50},{100,50,100,170,100},{100,100,50,100,170},{100,170,100,50,100}};
    return matrix[element(attacker)][element(target)];
}
int BattleRules::accuracy_bonus(unsigned attacker,unsigned target){const auto delta=[](unsigned status){return (status&0x3000)==0x1000?-10:(status&0x3000)==0x2000?10:0;};return delta(attacker)-delta(target);}
} // namespace fsb::core
