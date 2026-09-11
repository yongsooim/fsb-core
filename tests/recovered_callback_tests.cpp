#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include "../tools/lab_io.hpp"
#include <algorithm>
#include <iostream>
using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc!=2)return 2;auto memory=Memory::from_pe32(fsb::lab::read(argv[1]));RecoveredBattle code(memory);
        const Address actor=0x8073d8,out=0x6df000;unsigned checks=0;
        const auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("recovered callback contract mismatch");};
        memory.write(actor+0x14,0x00038000);memory.write(actor+0x18,0xfffd8000);memory.write(actor+0x1c,0x18000);
        code.service=[&](Address entry,RecoveredBattle& c){
            if(entry!=0x42feb9)return false;
            check(c.argument(0)==144);const auto saved=c.r;
            check(c.callback(0x408de1,{0x4000})==65536);check(c.r==saved);
            bool rejected=false;try{c.callback(0xdeadbeef);}catch(const Fault& fault){rejected=true;check(fault.address==0xdeadbeef&&fault.recovered_calls==std::vector<Address>{0xdeadbeef});}
            check(rejected&&c.r==saved);check(c.callback(0x408de1,{0x4000})==65536&&c.r==saved);
            c.result(actor,4);return true;
        };
        code.invoke(0x430aa6,{144,out,out+4,out+8});
        check(memory.read(out)==3&&memory.read(out+4)==0xfffffffdu&&memory.read(out+8)==1);
        check(code.callback(0x408de1,{0x4000})==65536);
        Runtime game(fsb::lab::read(argv[1]));auto& view=game.memory;
        view.write(0x7e0ca8,768*65536);view.write(0x7e0cac,2016*65536);
        view.write(0x6e12b0,640);view.write(0x6e1440,480);
        // 46923f, reached by the later learned battle effect, crosses this
        // original RET12 boundary into the existing core camera implementation.
        game.battle.recovered.invoke(0x453fe3,{929,1728,1});
        check(view.read(0x7873c0)==384&&view.read(0x7873c4)==1727);
        game.battle.recovered.invoke(0x453fe3,{929,1728,256});
        check(view.read(0x7873c0)==929&&view.read(0x7873c4)==1728); //char argument, low byte only.
        view.write(actor+4,0x1024e);game.battle.recovered.invoke(0x42ff9b,{actor,0});check(view.read(actor+4)==0x1020e);
        game.battle.recovered.invoke(0x42ff9b,{actor,1});check(view.read(actor+4)==0x1024e);
        set_actor_raw_position(view,actor,128*65536,240*65536);
        view.write(0x6d66dc,1);
        check(game.battle.recovered.invoke(0x43113b,{actor,2,3,60,0,0,1})==0);
        check(view.read(actor+8)==256*65536&&view.read(actor+12)==384*65536);
        view.write(0x6d66dc,0);
        const auto tween=game.battle.recovered.invoke(0x43113b,{actor,1,1,60,0,0,0});const auto child=resolve_compact(view,tween);
        check(child.has_value()&&view.read(*child+0xfc)==actor&&view.read(*child+0x19c)==1);
        check(view.read(*child+0x168)==64*65536&&view.read(*child+0x16c)==48*65536);
        const auto attached=game.battle.recovered.invoke(0x430d1c,{actor,0x6ca07e});const auto bound=resolve_compact(view,attached);
        check(bound.has_value()&&view.read(*bound+0xf8)==actor&&view.read(*bound+0xfc)==actor&&view.read(*bound+0x30)==0x6ca07e);
        const auto early=game.arena.members(1);check(std::find(early.begin(),early.end(),attached)!=early.end());
        view.write(globals::grid_row_stride,12);view.write(globals::grid_height,12);view.write(globals::shift_key_state,1);
        set_actor_tile_position(view,actor,5,5,0);view.write(actor+actor_offset::facing,0);
        const auto followed=game.battle.recovered.invoke(0x430e02,{actor,7,6,3,1,0,0,1});const auto path_obj=resolve_compact(view,followed);
        check(path_obj.has_value()&&view.read(*path_obj+0x30)==scripts::alternate_path_followup&&view.read(*path_obj+0x19c)==0);
        check(view.read(actor+actor_offset::path_count)>0&&view.read(actor+actor_offset::tile_x)==5&&view.read(actor+actor_offset::tile_y)==5);
        // Mirror battle149 finalizes an extra party-shaped enemy through
        //45d91d's indirect callback. The default visual has already existed in
        //the frame loop; it must also be callable on this nested path.
        const auto mirror=Actors::slot(68),link=29u;
        view.write(globals::game_mode,9);view.write(mirror,68);
        view.write(mirror+actor_offset::callback,0x45c526);
        view.write(mirror+actor_offset::callback_state,0);
        view.write(mirror+0x11c,link);
        view.write(tables::actor_slot_ids+link*0x44,68);
        view.write(tables::actor_object_pointers+link*0x44,mirror);
        game.battle.recovered.invoke(0x45d91d,{mirror});
        check(view.read(mirror)==68&&view.read(tables::actor_slot_ids+link*0x44)==0xffffffffu&&view.read(tables::actor_object_pointers+link*0x44)==0);
        const auto cleared=view.bytes(mirror+4,layout::actor_size-4);
        check(std::all_of(cleared.begin(),cleared.end(),[](auto byte){return byte==0;}));
        //458b79 loads the Gatewarp palette through4050fa. The non-null form
        //copies all256RGB entries; the null form applies them to the palette.
        const auto gatewarp=fsb::lab::read(std::filesystem::path(argv[1]).parent_path()/"ASE_FM/GATEWARP.pcx");
        game.sprites.register_pcx("@Gatewarp.pcx",gatewarp);
        game.battle.recovered.invoke(0x4050fa,{0x5d095c,out,0});
        game.battle.recovered.invoke(0x4050fa,{0x5d095c,0,0});
        for(unsigned i=0;i<256;++i){
            const auto offset=gatewarp.size()-768+i*3;
            const unsigned color=gatewarp[offset]|(unsigned(gatewarp[offset+1])<<8)|(unsigned(gatewarp[offset+2])<<16);
            check(view.read(out+i*4)==color&&game.palette.raw_entries()[i]==color);
        }
        // The Garasa enemy can stamp a north-facing span above row0, corrupting
        // 780254 (the skill count). Check all map edges through Runtime's actual
        // service, and compare every data byte with the original on interior
        // footprints, for both the one-region and all-region entry points.
        for(unsigned seed=0;seed<8;++seed)for(auto entry:{0x452168u,0x452255u}){
            Runtime bounded(fsb::lab::read(argv[1]));auto& b=bounded.memory;
            b.write(globals::grid_row_stride,20);b.write(globals::grid_height,20);
            b.write(0x77fff8,4);b.write(0x787398,3);b.write(0x780254,2);
            for(unsigned region=0;region<4;++region){
                b.write(0x7865c0+region*4,2);
                for(unsigned j=0;j<2;++j){const auto span=0x780000+region*144+j*12;
                    b.write(span,int(j)-1);b.write(span+4,-2);b.write(span+8,region+1);}
            }
            for(unsigned i=0;i<3;++i){b.write(0x786620+i*24,5+(seed+i)%8);b.write(0x786624+i*24,5+i*3);}
            Memory original=b;RecoveredBattle exact(original);
            const std::vector<unsigned> args=entry==0x452168?std::vector<unsigned>{seed%4}:std::vector<unsigned>{};
            const auto expected=exact.invoke(entry,args),actual=bounded.battle.recovered.invoke(entry,args);
            check(actual==expected&&b.bytes(0x4a5000,3882100)==original.bytes(0x4a5000,3882100));
        }
        for(auto entry:{0x452168u,0x452255u}){
            Runtime bounded(fsb::lab::read(argv[1]));auto& b=bounded.memory;
            b.write(globals::grid_row_stride,20);b.write(globals::grid_height,20);
            b.write(0x77fff8,1);b.write(0x787398,4);b.write(0x780254,2);b.write(0x7865c0,3);
            for(unsigned j=0;j<3;++j){b.write(0x780000+j*12,int(j)-1);b.write(0x780004+j*12,-1);b.write(0x780008+j*12,1);}
            for(unsigned i=0;i<4;++i){b.write(0x786620+i*24,(i&1)?19:0);b.write(0x786624+i*24,(i&2)?19:0);}
            const auto before=b.bytes(0x780240,28),after=b.bytes(0x780260+400,32);
            bounded.battle.recovered.invoke(entry,entry==0x452168?std::vector<unsigned>{0}:std::vector<unsigned>{});
            check(b.bytes(0x780240,28)==before&&b.bytes(0x780260+400,32)==after&&b.read(0x780254)==2);
            for(unsigned y=0;y<20;++y)for(unsigned x=0;x<20;++x)
                check(b.read(0x780260+y*20+x,1)==unsigned((x<2||x>=18)&&(y<2||y>=18)));
        }
        std::cout<<"recovered_callback_checks="<<checks<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
