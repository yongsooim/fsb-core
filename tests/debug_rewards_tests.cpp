#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include "../tools/lab_io.hpp"
#include <algorithm>
#include <iostream>
using namespace fsb::core;
namespace {void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
int main(int argc,char**argv){try{
    if(argc!=2)return 2;const auto exe=fsb::lab::read(std::filesystem::path(argv[1])/"FLYINGSB.EXE");
    Runtime normal(exe),boosted(exe);constexpr auto hero=0x607a08+3*188;
    const auto setup=[&](Runtime& game){auto& m=game.memory;m.write(0x803a20,1);m.write(0x5d2258,3);m.write(0x7757d8,0xffffffffu);m.write(0x776484,8);m.write(0x6d1bf0,12345);
        m.write(hero+8,0);m.write(hero+0x28,1);m.write(hero+0x34,0);m.write(hero+0x38,100);
        for(unsigned i=0;i<8;++i){m.write(0x806b64+i*36,138);m.write(0x806b60+i*36+20,0);}};
    setup(normal);setup(boosted);Memory original=normal.memory;RecoveredBattle reference(original);
    reference.invoke(0x44dd1c);normal.battle.recovered.invoke(0x44dd1c);
    check(normal.memory.bytes(0x4a5000,3882100)==original.bytes(0x4a5000,3882100),"OFF must keep complete original reward state");
    boosted.debug_rewards.enable(true);boosted.battle.recovered.invoke(0x44dd1c);
    check(boosted.memory.bytes(0x4a5000,3882100)==original.bytes(0x4a5000,3882100),"reward preparation must remain original until the common payout boundary");
    const auto exp=original.read(0x77ebf8)*10,gold=original.read(0x77ec00)*10,rng=original.read(0x6d1bf0);const auto drops=original.bytes(0x774170,16);
    boosted.battle.recovered.invoke(0x44de92);
    check(boosted.memory.read(0x77ebf8)==exp&&boosted.memory.read(0x77ec00)==gold,"payout must scale EXP and gold exactly once");
    check(boosted.memory.read(0x6d1bf0)==rng&&boosted.memory.bytes(0x774170,16)==drops,"boost must preserve item drops and RNG");
    check(boosted.memory.read(hero+0x34)==exp,"boosted XP must survive the original single-level cap");
    // Compare the entire character/equipment state with the same XP delivered
    // across ordinary one-level awards. In particular, HP/attack growth in
    // 450b1c is incremental and must not skip the intervening levels.
    Runtime gradual(exe);setup(gradual);
    const auto gradual_text=gradual.memory.allocate_zeroed(4096),boosted_text=boosted.memory.allocate_zeroed(4096);
    while(gradual.memory.read(hero+0x34)<exp){
        const auto current=gradual.memory.read(hero+0x34),level=gradual.memory.read(hero+0x28);
        const auto chunk=std::min(exp-current,gradual.memory.read(0x5c0238+level*4)-current);
        if(gradual.battle.recovered.invoke_byte(0x44de15,{3,chunk}))gradual.battle.recovered.invoke(0x450b1c,{3,gradual.memory.read(hero+0x28),gradual_text});
    }
    boosted.battle.recovered.invoke(0x450b1c,{3,boosted.memory.read(hero+0x28),boosted_text});
    for(unsigned offset=0;offset<16*188;offset+=4)if(boosted.memory.read(0x607a08+offset)!=gradual.memory.read(0x607a08+offset)){
        std::cerr<<"growth mismatch address=0x"<<std::hex<<0x607a08+offset<<std::dec<<" boosted="<<boosted.memory.read(0x607a08+offset)<<" gradual="<<gradual.memory.read(0x607a08+offset)<<'\n';
        throw std::runtime_error("multi-level stats and skills must equal ordinary incremental level gains");
    }
    check(boosted.memory.bytes(0x806e30,362*4)==gradual.memory.bytes(0x806e30,362*4),"level growth must preserve original inventory effects");
    boosted.debug_rewards.enable(false);
    const auto level=boosted.memory.read(hero+0x28);check(level>2&&level<=99,"boosted reward must cross multiple real level thresholds");
    for(unsigned i=0;i<10;++i){const auto pair=0x5bfd30+3*80+i*8,learn=boosted.memory.read(pair),slot=boosted.memory.read(pair+4);
        if(learn>1&&learn<level&&slot<10)check(boosted.memory.read(hero+0x94+slot*4)==boosted.memory.read(0x6085c8+3*40+slot*4),"intermediate skill unlock was lost during debug level jump");}
    boosted.battle.recovered.invoke(0x44de15,{3,100000});check(boosted.memory.read(hero+0x28)==level+1,"OFF must restore original one-level-per-award behavior after the batch");
    Runtime scripted(exe);setup(scripted);scripted.debug_rewards.enable(true);scripted.battle.rules.compute_rewards();scripted.battle.recovered.invoke(0x44de92);
    check(scripted.memory.read(0x77ebf8)==exp&&scripted.memory.read(0x77ec00)==gold&&scripted.memory.read(hero+0x34)==exp,"scripted and normal battles must share the boosted payout");
    for(const unsigned id:{1u,4u,9u}){
        Runtime batch(exe);setup(batch);const auto record=0x607a08+id*188;
        batch.memory.write(0x5d2258,id);batch.memory.write(record+8,0);batch.memory.write(record+0x28,1);batch.memory.write(record+0x34,0);
        batch.memory.write(record+0x80,0xffffffffu);batch.memory.write(record+0x84,0xffffffffu);
        batch.memory.write(0x77ebf8,8000);batch.memory.write(0x77ec00,0);
        Memory expected=batch.memory;RecoveredBattle original_growth(expected);const auto expected_text=expected.allocate_zeroed(4096);
        original_growth.service=[](Address entry,RecoveredBattle& call){
            if(entry!=0x4992d0)return false;
            const auto text=call.format_text(call.argument(1),call.r[4]+12);const auto out=call.argument(0);
            for(unsigned i=0;i<text.size();++i)call.write(out+i,std::uint8_t(text[i]),1);
            call.write(out+unsigned(text.size()),0,1);call.result(unsigned(text.size()));return true;
        };
        while(expected.read(record+0x34)<80000){
            const auto current=expected.read(record+0x34),level=expected.read(record+0x28);
            const auto chunk=std::min(80000-current,expected.read(0x5c0238+level*4)-current);
            if(original_growth.invoke_byte(0x44de15,{id,chunk}))original_growth.invoke(0x450b1c,{id,expected.read(record+0x28),expected_text});
        }
        batch.debug_rewards.enable(true);batch.battle.recovered.invoke(0x44de92);
        const auto batch_text=batch.memory.allocate_zeroed(4096);batch.battle.recovered.invoke(0x450b1c,{id,batch.memory.read(record+0x28),batch_text});
        check(batch.memory.bytes(0x607a08,16*188)==expected.bytes(0x607a08,16*188),"party-specific growth/skills/equipment must match all original incremental levels");
        check(batch.memory.bytes(0x806e30,362*4)==expected.bytes(0x806e30,362*4),"Son's level-dependent equipment conversion must retain original inventory updates");
    }
    scripted.debug_rewards.enable(true);scripted.memory.write(0x803a18,1000);
    const auto pc=scripted.memory.allocate_zeroed(14);scripted.memory.write(pc,0x29,1);scripted.memory.write(pc+1,0,1);scripted.memory.write(pc+2,14,2);scripted.memory.write(pc+4,0xc4,1);scripted.memory.write(pc+5,0x803a18);scripted.memory.write(pc+9,4,1);scripted.memory.write(pc+10,25);
    const auto handle=scripted.arena.clone_event(pc,0),obj=*resolve_compact(scripted.memory,handle);Vm vm(scripted.memory,scripted.messages,scripted.environment,obj);vm.step();check(scripted.memory.read(0x803a18)==1250,"scripted positive gold reward must be multiplied once");
    scripted.memory.write(pc+1,1,1);scripted.memory.write(obj+vm_offset::pc,pc);vm.step();check(scripted.memory.read(0x803a18)==1225,"gold costs must remain unchanged");
    scripted.debug_rewards.enable(false);scripted.memory.write(pc+1,0,1);scripted.memory.write(obj+vm_offset::pc,pc);vm.step();check(scripted.memory.read(0x803a18)==1250,"script reward toggle OFF must immediately restore original gain");
    std::cout<<"debug rewards: exact OFF, 10x totals, original RNG/drops, multiple levels/skills, toggle and scripted gains passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
