#pragma once
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"
#include <algorithm>
#include <limits>

namespace fsb::lab {
// Developer-only replay fixture: inspect public state and send ordinary key
// presses. It never writes game records, resolves an attack, or grants victory.
class BattleInputFixture {
public:
    bool use_offensive_skills=false;
    bool manage_field_dialogue=true;
    std::optional<core::InputMessage> next(core::Runtime& runtime,unsigned now){
        using namespace core;auto& m=runtime.memory;
        if(held_key_&&now>=release_at_){const auto key=held_key_;held_key_=0;next_at_=now+80;return keyboard_message(key,scan(key),false);}
        if(held_key_||now<next_at_)return std::nullopt;
        if(m.read(globals::game_mode)==9)battle_seen_=true;
        else {if(manage_field_dialogue&&battle_seen_&&now>=dialog_at_){dialog_at_=now+600;return press(13,false,now);}return std::nullopt;}
        if(m.read(0x768684)){if(now>=dialog_at_){dialog_at_=now+600;return press(13,false,now);}return std::nullopt;}
        if(m.read(0x775cb0)!=2||m.read(0x775cac)!=4){turn_started_=0;end_pending_=false;skill_skipped_=false;heal_pending_=potion_pending_=cancel_skill_=false;skill_pending_=potion_target_=0xffffffffu;return std::nullopt;}
        const auto context=m.read(0x7757e0);if(context>=10)return std::nullopt;const auto actor=Actors::slot(context);
        if(!turn_started_)turn_started_=now;
        // A human can end a turn when the remaining movement budget cannot
        // reach an enemy. Exercise that same radial-menu action after a stall.
        if(end_pending_&&m.read(0x77ecdc)==1){const auto radial=m.read(0x773624);if(m.read(0x773630)&&radial&&m.read(radial+0x20)==20)return press(40,false,now);return std::nullopt;}
        if(now-turn_started_>12000&&m.read(actor+0x148)==0x458ed7&&!m.read(actor+0x104)&&m.read(0x77ecdc)==0){end_pending_=true;heal_pending_=false;potion_pending_=false;return press(90,false,now);}
        if(now-turn_started_>12000&&m.read(actor+0x148)==0x45c14f&&!m.read(actor+0x104)&&m.read(0x77ecdc)==0){skill_skipped_=true;heal_pending_=potion_pending_=false;skill_pending_=0xffffffffu;return press(90,false,now);}
        if(cancel_skill_){if(m.read(0x77ecdc)==1)return press(90,false,now);cancel_skill_=false;}
        if((heal_pending_||potion_pending_||skill_pending_!=0xffffffffu)&&m.read(0x77ecdc)==1){
            if(!m.read(0x773630))return std::nullopt;
            const auto radial=m.read(0x773624),skills=m.read(0x7735fc);
            if(radial&&m.read(radial+0x20)==20)return press(potion_pending_?37:38,false,now);
            const auto items=m.read(0x7735f8);
            if(potion_pending_&&items&&m.read(items+0x20)==20){const auto selected=m.read(items+0x1a0);if(selected<362){const auto item=m.read(0x773030+selected*4);if(item==potion_item_)return press(13,false,now);return press(item<potion_item_?40:38,false,now);}return std::nullopt;}
            if(skills&&m.read(skills+0x20)==20){
                const auto selected=m.read(skills+0x1a0);if(selected<10&&m.read(0x772398+selected*4)==(skill_pending_==0xffffffffu?10:skill_pending_)){
                    if(skill_pending_!=0xffffffffu&&!m.read(0x772fb8+selected*4)){skill_pending_=0xffffffffu;skill_skipped_=true;cancel_skill_=true;return press(90,false,now);}
                    return press(13,false,now);
                }
                if(selected<10)return press(m.read(0x772398+selected*4)<(skill_pending_==0xffffffffu?10:skill_pending_)?40:38,false,now);
                return std::nullopt;
            }return std::nullopt;
        }
        if(!(m.read(actor+4)&0x80)||m.read(actor+0x104)||m.read(0x77a568)<=5)return std::nullopt;
        const int x=signed32(m.read(actor+0x128)),y=signed32(m.read(actor+0x12c)),z=signed32(m.read(actor+0x1c))/65536;
        std::optional<std::pair<int,int>> support_target;
        const auto id=m.read(globals::party_actor_ids+context*4),action=m.read(BattleRules::party_record(id)+0x88),handler=m.read(0x608858+action*24),record=0x5c2088+handler*24;
        const auto recovery_skill=[](unsigned type){return (type&0xff000001u)==0x01000000u&&(type&(0x40u|0x10000u));};
        const auto support_status=[](unsigned skill){return skill==45?battle_status::damage_guard:skill==40?battle_status::power_samba:skill==16?battle_status::silence:0u;};
        const auto silence_useful=[&](unsigned target){
            const auto enemy=BattleRules::enemy_record(m.read(Actors::slot(target)+0x118)),definition=BattleRules::monster_record(m.read(enemy+4));
            for(unsigned i=0;i<std::min(4u,m.read(definition+0x44));++i){
                const auto action=m.read(definition+0x48+i*4);
                if(action!=0xffffffffu&&!(m.read(0x610c14+action*32)&0x800000)&&m.read(0x610c18+action*32)<=m.read(enemy+24))return true;
            }
            return false;
        };
        const auto recovery_item=[&](bool mana,unsigned missing){
            unsigned selected=0xffffffffu,power=0;
            for(unsigned item=mana?302u:299u;item<=(mana?304u:301u);++item){
                if(!m.read(0x806e30+item*4))continue;
                const auto candidate=m.read(0x6131ac+item*76);
                if(selected==0xffffffffu||(candidate>=missing&&(power<missing||candidate<power))||(candidate<missing&&power<missing&&candidate>power)){selected=item;power=candidate;}
            }
            if((selected==0xffffffffu||power<missing)&&m.read(0x806e30+305*4))selected=305; //HP+MP peach fallback.
            return selected;
        };
        if(m.read(actor+0x148)==0x45c14f&&m.read(0x77ebfc)==3&&m.read(0x774180)==potion_item_){
            potion_pending_=false;const auto target=Actors::slot(potion_target_==0xffffffffu?context:potion_target_);
            const int dx=signed32(m.read(target+0x128))-x-signed32(m.read(0x77fc08)),dy=signed32(m.read(target+0x12c))-y-signed32(m.read(0x77fc0c));
            if(!dx&&!dy)return press(13,false,now);
            Memory scratch=m;BattleRules preview(scratch);
            for(unsigned direction:{dx<0?2u:3u,dy<0?0u:1u})if(((direction<2&&dy)||(direction>=2&&dx))&&preview.shift_footprint(direction)){
                constexpr unsigned keys[]={38,40,37,39};return press(keys[direction],false,now);
            }
            potion_target_=0xffffffffu;skill_skipped_=true;return press(90,false,now);
        }
        if(use_offensive_skills&&m.read(actor+0x148)==0x45c14f&&m.read(0x77ebfc)==2&&m.read(0x774180)<66&&m.read(0x774180)!=10){
            const auto type=m.read(0x60914c+m.read(0x774180)*44);
            if(const auto status=support_status(m.read(0x774180))){
                skill_pending_=0xffffffffu;
                const bool silence=m.read(0x774180)==16;
                for(auto target:runtime.battle.rules.collect_targets(actor,silence?0x400:0x100,battle_status::unavailable))
                    if(!(runtime.battle.rules.status(target&65535u)&status)&&(!silence||silence_useful(target&65535u)))return press(13,false,now);
                skill_skipped_=true;return press(90,false,now);
            }
            if(recovery_skill(type)){
                skill_pending_=0xffffffffu;heal_pending_=false;
                const bool revive=type&0x10000;
                for(auto target:runtime.battle.rules.collect_targets(actor,0x100,revive?0:0x600000,revive?std::optional<unsigned>(0x600000):std::nullopt)){
                    const auto r=BattleRules::party_record(m.read(globals::party_actor_ids+(target&65535u)*4));
                    if(revive?(m.read(r+8)&battle_status::down)!=0:m.read(r+0x1c)<m.read(r+0x18))return press(13,false,now);
                }
                // Selection is planned against the real range mask below.
                // If the target vanished, cancel instead of moving a fixed
                // footprint indefinitely (Minuet cannot move its cross).
                skill_skipped_=true;return press(90,false,now);
            }
        }
        if(m.read(actor+0x148)==0x45c14f&&m.read(0x77ebfc)==2&&m.read(0x774180)==10){
            heal_pending_=false;unsigned target=context;int ratio=100;
            for(unsigned i=0;i<m.read(globals::party_count);++i){const auto r=BattleRules::party_record(m.read(globals::party_actor_ids+i*4)),hp=m.read(r+0x1c),maximum=m.read(r+0x18),candidate=Actors::slot(i);
                const int distance=std::abs(signed32(m.read(candidate+0x128))-x)+std::abs(signed32(m.read(candidate+0x12c))-y);
                if(distance<=1&&hp&&maximum&&int(hp*100/maximum)<ratio){ratio=int(hp*100/maximum);target=i;}}
            if(ratio==100)return press(90,false,now);
            const auto receiver=Actors::slot(target);const int tx=signed32(m.read(receiver+0x128)),ty=signed32(m.read(receiver+0x12c)),cx=x+signed32(m.read(0x77fc08)),cy=y+signed32(m.read(0x77fc0c));
            if(tx!=x||ty!=y){const unsigned direction=tx<x?2:tx>x?3:ty<y?0:1;constexpr unsigned keys[]={38,40,37,39};if(m.read(actor+0x110)!=direction)return press(keys[direction],true,now);}
            if(cx==tx&&cy==ty)return press(13,false,now);
            return press(cx<tx?39:cx>tx?37:cy<ty?40:38,false,now);
        }
        if(use_offensive_skills&&!skill_skipped_&&m.read(actor+0x148)==0x458ed7){
            bool injured=false,down=false;for(unsigned i=0;i<m.read(globals::party_count);++i){const auto r=BattleRules::party_record(m.read(globals::party_actor_ids+i*4));injured|=m.read(r+0x1c)>0&&m.read(r+0x1c)*100<m.read(r+0x18)*75;down|=(m.read(r+8)&battle_status::down)!=0;}
            if(injured||down){const auto party=BattleRules::party_record(id);unsigned selected=0xffffffffu,power=0,healing_facing=m.read(actor+0x110);bool can_heal=false;Memory scratch=m;BattleRules preview(scratch);
                for(unsigned row=0;row<10;++row){const auto skill=m.read(party+0x94+row*4);if(skill>=66)continue;const auto def=0x609148+skill*44;
                    if(!recovery_skill(m.read(def+4))||m.read(def+20)>m.read(party+0x24)||(m.read(def+24)&&signed32(m.read(party+0x2c))<signed32(m.read(def+24))))continue;
                    const bool revive=m.read(def+4)&0x10000;if(revive?!down:!injured)continue;
                    can_heal|=!revive;
                    for(unsigned k=0;k<4;++k){const auto direction=(m.read(actor+0x110)+k)%4;preview.prepare_handler(m.read(def+32),direction);
                        for(auto target:preview.collect_targets(actor,0x100,revive?0:0x600000,revive?std::optional<unsigned>(0x600000):std::nullopt)){const auto r=BattleRules::party_record(m.read(globals::party_actor_ids+(target&65535u)*4));
                            if((revive?(m.read(r+8)&battle_status::down)!=0:m.read(r+0x1c)*100<m.read(r+0x18)*75)&&m.read(def+16)>power){selected=skill;power=m.read(def+16);healing_facing=direction;}}
                    }
                }
                if(selected!=0xffffffffu){if(healing_facing!=m.read(actor+0x110)){constexpr unsigned keys[]={38,40,37,39};return press(keys[healing_facing],true,now);}skill_pending_=selected;heal_pending_=true;potion_pending_=false;return press(90,false,now);}
                if(can_heal){unsigned lowest=75;for(unsigned i=0;i<m.read(globals::party_count);++i){if(i==context)continue;const auto r=BattleRules::party_record(m.read(globals::party_actor_ids+i*4)),hp=m.read(r+0x1c),cap=m.read(r+0x18);if(hp&&cap&&hp*100/cap<lowest){lowest=hp*100/cap;const auto target=Actors::slot(i);support_target={{signed32(m.read(target+0x128)),signed32(m.read(target+0x12c))}};}}}
            }
        }
        if(m.read(actor+0x148)==0x458ed7&&m.read(BattleRules::party_record(id)+0x1c)*100<m.read(BattleRules::party_record(id)+0x18)*60){
            const auto item=recovery_item(false,m.read(BattleRules::party_record(id)+0x18)-m.read(BattleRules::party_record(id)+0x1c));
            if(item!=0xffffffffu){potion_item_=item;potion_target_=context;potion_pending_=true;heal_pending_=false;return press(90,false,now);}
        }
        if(use_offensive_skills&&!skill_skipped_&&m.read(actor+0x148)==0x458ed7){
            //306/308 revive through the original item handler. Non-healers
            //must be able to bring the healer back instead of fighting alone.
            for(unsigned item:{306u,308u}){
                if(!m.read(0x806e30+item*4))continue;
                for(unsigned i=0;i<m.read(globals::party_count);++i){
                    const auto target=Actors::slot(i),party=BattleRules::party_record(m.read(globals::party_actor_ids+i*4));
                    if(!(m.read(party+8)&battle_status::down))continue;
                    Memory scratch=m;BattleRules preview(scratch);preview.prepare_handler(item==308?0:0x40,m.read(actor+0x110));
                    const int tx=signed32(m.read(target+0x128))-x,ty=signed32(m.read(target+0x12c))-y;
                    for(unsigned step=0;step<22;++step){
                        const int dx=tx-signed32(scratch.read(0x77fc08)),dy=ty-signed32(scratch.read(0x77fc0c));
                        if(!dx&&!dy)break;
                        if(dx&&preview.shift_footprint(dx<0?2:3))continue;
                        if(dy&&preview.shift_footprint(dy<0?0:1))continue;
                        break;
                    }
                    const auto targets=preview.collect_targets(actor,0x100,0,battle_status::down);
                    if(std::any_of(targets.begin(),targets.end(),[&](unsigned value){return (value&65535u)==i;})){
                        potion_item_=item;potion_target_=i;potion_pending_=true;heal_pending_=false;return press(90,false,now);
                    }
                    if(!support_target&&(m.read(globals::party_actor_ids+i*4)==3||m.read(globals::party_actor_ids+i*4)==9))
                        support_target={{signed32(m.read(target+0x128)),signed32(m.read(target+0x12c))}};
                }
            }
        }
        if(use_offensive_skills&&m.read(actor+0x148)==0x458ed7){
            const auto party=BattleRules::party_record(id);unsigned cost=0xffffffffu;
            for(unsigned row=0;row<10;++row){const auto skill=m.read(party+0x94+row*4);if(skill>=66)continue;const auto def=0x609148+skill*44,type=m.read(def+4),mp=m.read(def+20);if(mp&&(((type>>24)==0&&(type&1))||recovery_skill(type)))cost=std::min(cost,mp);}
            if(cost!=0xffffffffu&&(m.read(party+0x24)<cost||m.read(party+0x24)*100<m.read(party+0x20)*35)){
                const auto item=recovery_item(true,m.read(party+0x20)-m.read(party+0x24));
                if(item!=0xffffffffu){potion_item_=item;potion_target_=context;potion_pending_=true;heal_pending_=false;return press(90,false,now);}
            }
        }
        if(id==3&&m.read(actor+0x148)==0x458ed7&&m.read(BattleRules::party_record(id)+0x24)>=5){
            bool need=false;for(unsigned i=0;i<m.read(globals::party_count);++i){const auto r=BattleRules::party_record(m.read(globals::party_actor_ids+i*4)),candidate=Actors::slot(i);const int distance=std::abs(signed32(m.read(candidate+0x128))-x)+std::abs(signed32(m.read(candidate+0x12c))-y);need|=distance<=1&&m.read(r+0x1c)>0&&m.read(r+0x1c)*100<m.read(r+0x18)*65;}
            if(need){heal_pending_=true;return press(90,false,now);}
        }
        // Sonata's original party guard and physical-attack buff matter in
        // long boss fights. Cast them through the skill menu. Guard can protect
        // its caster alone; attack buffs need two recipients.
        if(use_offensive_skills&&(id==9||id==3)&&!skill_skipped_&&m.read(actor+0x148)==0x458ed7){
            const auto party=BattleRules::party_record(id);Memory scratch=m;BattleRules preview(scratch);
            for(unsigned skill:{45u,40u,16u}){
                bool learned=false;for(unsigned row=0;row<10;++row)learned|=m.read(party+0x94+row*4)==skill;
                const auto def=0x609148+skill*44;if(!learned||m.read(def+20)>m.read(party+0x24))continue;
                const unsigned required=skill==40?2:1;unsigned best=required-1,chosen=m.read(actor+0x110);
                for(unsigned k=0;k<4;++k){const auto direction=(m.read(actor+0x110)+k)%4;preview.prepare_handler(m.read(def+32),direction);unsigned count=0;
                    for(auto target:preview.collect_targets(actor,skill==16?0x400:0x100,battle_status::unavailable))
                        count+=!(preview.status(target&65535u)&support_status(skill))&&(skill!=16||silence_useful(target&65535u));
                    if(count>best){best=count;chosen=direction;}
                }
                if(best>=required){constexpr unsigned keys[]={38,40,37,39};if(chosen!=m.read(actor+0x110))return press(keys[chosen],true,now);
                    skill_pending_=skill;heal_pending_=potion_pending_=false;return press(90,false,now);}
            }
        }
        const auto shape=0x5c3800+m.read(record+12)*121;
        const int ox=signed32(m.read(record+16)),oy=signed32(m.read(record+20));
        const int width=signed32(m.read(globals::grid_row_stride)),height=signed32(m.read(globals::grid_height));
        struct Opponent{int x,y;unsigned hp;};std::vector<Opponent> enemies;
        for(unsigned i=0;i<m.read(0x776484);++i){const auto row=BattleRules::enemy_record(i);if((m.read(row+8)&battle_status::unavailable)||!m.read(row+20))continue;const auto target=Actors::slot(m.read(row));enemies.push_back({signed32(m.read(target+0x128)),signed32(m.read(target+0x12c)),m.read(row+20)});}
        if(enemies.empty())return std::nullopt;
        const auto facing=m.read(actor+0x110);unsigned can_hit=4,lowest_hp=0xffffffffu;
        for(unsigned k=0;k<4;++k){const auto direction=(k+std::min(facing,3u))%4;const int dxs[]={ox,-ox,oy,-oy},dys[]={oy,-oy,-ox,ox};
            for(auto [tx,ty,hp]:enemies){const int cx=tx-x+5-dxs[direction],cy=ty-y+5-dys[direction];if(cx<0||cy<0||cx>=11||cy>=11)continue;
                const unsigned indices[]={unsigned(cy*11+cx),unsigned(120-cy*11-cx),unsigned(cx*11+10-cy),unsigned((10-cx)*11+cy)};
                if((m.read(shape+indices[direction],1)&4)&&hp<lowest_hp){can_hit=direction;lowest_hp=hp;}
            }if(!use_offensive_skills&&can_hit<4)break;
        }
        constexpr unsigned keys[]={38,40,37,39};
        if(m.read(actor+0x148)==0x45c14f){
            skill_pending_=0xffffffffu;
            if(!runtime.battle.rules.collect_targets(actor,0x400,0x600000).empty())return press(13,false,now);
            if(can_hit<4&&can_hit!=facing)return press(keys[can_hit],true,now);
            if(m.read(0x77ebfc)==2)skill_skipped_=true;
            return press(90,false,now);
        }
        if(m.read(actor+0x148)!=0x458ed7)return std::nullopt;
        if(support_target){
            constexpr int dx[]={0,0,-1,1},dy[]={-1,1,0,0};const auto [tx,ty]=*support_target;int best=std::abs(tx-x)+std::abs(ty-y);unsigned selected=4;
            const int maximum=signed32(m.read(BattleRules::party_record(id)+0x60))/10;
            for(unsigned d=0;d<4;++d){const int nx=x+dx[d],ny=y+dy[d];if(nx<0||ny<0||nx>=width||ny>=height)continue;const auto cell=unsigned(ny*width+nx),cost=m.read(globals::move_cost_grid+cell*4),attr=m.read(globals::tile_attributes+(unsigned(z)*4096+cell)*4);if(signed32(cost)<0||int(cost&255)>maximum||(attr&0x200)||(m.read(globals::tile_occupancy+(unsigned(z)*4096+cell)*4)&0x10000))continue;
                const int distance=std::abs(tx-nx)+std::abs(ty-ny);if(distance<best){best=distance;selected=d;}}
            if(selected<4)return press(keys[selected],false,now);
        }
        if(use_offensive_skills&&!skill_skipped_){
            const auto party=BattleRules::party_record(id);unsigned selected=0xffffffffu,power=0,skill_facing=facing;
            Memory scratch=m;BattleRules preview(scratch);
            for(unsigned row=0;row<10;++row){
                const auto skill=m.read(party+0x94+row*4);if(skill>=66)continue;const auto definition=0x609148+skill*44;
                const auto type=m.read(definition+4);
                if((type>>24)!=0||!(type&1)||m.read(definition+20)>m.read(party+0x24)||(m.read(definition+24)&&signed32(m.read(party+0x2c))<signed32(m.read(definition+24))))continue;
                // A weapon target does not imply this skill can hit it. Later
                // learned skills have different footprints; query their actual
                // original mask on a copy before opening the skill menu.
                for(unsigned k=0;k<4;++k){const auto direction=(facing+k)%4;preview.prepare_handler(m.read(definition+32),direction);
                    const auto targets=preview.collect_targets(actor,0x400,0x600000);if(targets.empty())continue;
                    const auto score=m.read(definition+16)*unsigned(targets.size());
                    if(score>power){power=score;selected=skill;skill_facing=direction;}
                }
            }
            if(selected!=0xffffffffu){if(skill_facing!=facing)return press(keys[skill_facing],true,now);skill_pending_=selected;heal_pending_=potion_pending_=false;return press(90,false,now);}
        }
        if(can_hit<4)return press(can_hit==facing?13:keys[can_hit],can_hit!=facing,now);
        const auto maximum=signed32(m.read(BattleRules::party_record(id)+0x60))/10;
        if(use_offensive_skills){
            // Manhattan-only steering oscillates behind walls (TSD0_03).
            // Plan on a copy with the original battle neighbor predicate,
            // including its battle rectangle and current actor occupancy.
            Memory scratch=m;RecoveredBattle paths(scratch);
            constexpr int dx[]={0,0,-1,1},dy[]={-1,1,0,0};
            const auto can_attack_from=[&](int px,int py){
                for(unsigned direction=0;direction<4;++direction){
                    const int dxs[]={ox,-ox,oy,-oy},dys[]={oy,-oy,-ox,ox};
                    for(const auto& enemy:enemies){
                        const int cx=enemy.x-px+5-dxs[direction],cy=enemy.y-py+5-dys[direction];
                        if(cx<0||cy<0||cx>=11||cy>=11)continue;
                        const unsigned indices[]={unsigned(cy*11+cx),unsigned(120-cy*11-cx),unsigned(cx*11+10-cy),unsigned((10-cx)*11+cy)};
                        if(m.read(shape+indices[direction],1)&4)return true;
                    }
                }
                return false;
            };
            struct Node{int x,y;unsigned first;};unsigned selected=4;
            for(unsigned flags:{0xad2u,0xac2u}){
                std::vector<Node> queue{{x,y,4}};std::vector<bool> visited(std::size_t(width)*height);visited[unsigned(y*width+x)]=true;
                for(std::size_t q=0;q<queue.size()&&selected==4;++q){const auto at=queue[q];
                    // A ranged weapon can attack across an obstacle even when
                    // no walkable tile adjoins the enemy (Tralok mountain322).
                    if(q&&can_attack_from(at.x,at.y))selected=at.first;
                    if(selected<4)break;
                    for(unsigned d=0;d<4;++d){const int nx=at.x+dx[d],ny=at.y+dy[d];if(nx<1||ny<1||nx>=width-1||ny>=height-1)continue;const auto cell=unsigned(ny*width+nx);if(visited[cell])continue;
                        if(!q){const auto cost=m.read(globals::move_cost_grid+cell*4);const bool regular=signed32(cost)>=0&&int(cost&255)<=maximum&&(m.read(0x7764e0+cell*4)&1);
                            if(!regular&&(flags!=0xac2u||int(m.read(globals::move_cost_grid+unsigned(y*width+x)*4)&255)>=maximum))continue;}
                        if(!paths.invoke_byte(0x44956a,{unsigned(at.x),unsigned(at.y),d,flags}))continue;
                        visited[cell]=true;queue.push_back({nx,ny,q?at.first:d});
                    }
                }
                if(selected<4)break;
            }
            if(selected<4){
                const auto cell=unsigned((y+dy[selected])*width+x+dx[selected]);
                const bool detour=!(m.read(0x7764e0+cell*4)&1)||!paths.invoke_byte(0x44956a,{unsigned(x),unsigned(y),selected,0x2d2});
                //458ed7 allows pushing a party member/body after31 held frames.
                return press(keys[selected],false,now,detour?m.read(globals::phase_interval_ms)*40:48);
            }
            end_pending_=true;return press(90,false,now);
        }
        constexpr int dx[]={0,0,-1,1},dy[]={-1,1,0,0};int best=std::numeric_limits<int>::max();unsigned direction=4;
        for(unsigned k=0;k<4;++k){const int nx=x+dx[k],ny=y+dy[k];if(nx<0||ny<0||nx>=width||ny>=height)continue;const auto cell=unsigned(ny*width+nx),cost=m.read(globals::move_cost_grid+cell*4),attr=m.read(globals::tile_attributes+(unsigned(z)*4096+cell)*4);
            if(signed32(cost)<0||int(cost&255)>maximum||(m.read(globals::tile_occupancy+(unsigned(z)*4096+cell)*4)&0x10000)||(attr&0x200))continue;
            int distance=std::numeric_limits<int>::max();for(auto [tx,ty,hp]:enemies)distance=std::min(distance,std::abs(nx-tx)+std::abs(ny-ty));
            if(distance<best){best=distance;direction=k;}
        }
        if(direction<4)return press(keys[direction],false,now);return std::nullopt;
    }
private:
    unsigned held_key_=0,release_at_=0,next_at_=0,dialog_at_=0,turn_started_=0,skill_pending_=0xffffffffu,potion_item_=299,potion_target_=0xffffffffu;bool heal_pending_=false,potion_pending_=false,battle_seen_=false,end_pending_=false,skill_skipped_=false,cancel_skill_=false;
    static unsigned scan(unsigned key){switch(key){case 38:return 0xc8;case 40:return 0xd0;case 37:return 0xcb;case 39:return 0xcd;case 90:return 0x2c;default:return 0x1c;}}
    core::InputMessage press(unsigned key,bool shift,unsigned now,unsigned duration=48){held_key_=key;release_at_=now+duration;return core::keyboard_message(key,scan(key),true,shift);}
};
} // namespace fsb::lab
