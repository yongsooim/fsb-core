#include "fsb_core/original_cheats_runtime.hpp"
#include "fsb_core/runtime.hpp"
#include "fsb_core/symbols.hpp"

namespace fsb::core {
Runtime::Runtime(const std::vector<std::uint8_t>& executable,unsigned width,unsigned height)
    :memory(Memory::from_pe32(executable)),arena(memory),actors(memory),surfaces(memory),palette(memory,{}),sprites(memory,surfaces,palette),transition(memory,surfaces),
    viewport(memory),map(memory),audio(memory),effects(memory),dialogue(memory,messages),graphics(memory,surfaces),camera(memory),draw(memory,surfaces,sprites),field(memory,actors,audio),debug_rewards(memory),battle(*this),field_menu(*this),
     pump_(memory,arena,[this](Address cb,Address obj,unsigned jobs){dispatch(cb,obj,jobs);},[this](const ObjectDraw& c){
        if(c.flags&~(blit_flags::fast_wait|blit_flags::fast_source_key))throw Fault(c.object,"unsupported original compact BltFast flags");
        const Rect clip{signed32(memory.read(globals::clip_left)),signed32(memory.read(globals::clip_top)),signed32(memory.read(globals::clip_right)),signed32(memory.read(globals::clip_bottom))};
        surfaces.blit(c.destination,c.x,c.y,c.source,{c.left,c.top,c.right,c.bottom},(c.flags&1)!=0,clip);
     }),width_(width),height_(height){
    arena.initialize();viewport.configure_framebuffer(int(width),int(height));
    primary_=surfaces.create(width,height);back_=surfaces.create(width,height);
    memory.write(globals::primary_surface,primary_);memory.write(globals::back_surface,back_);memory.write(globals::render_target_surface,back_);
    memory.write(globals::presentation_page_count,1); // Explicit original Blt/one-page presentation policy.
    memory.write(globals::runtime_mode_flags,1);memory.write(globals::focus_palette_state,1);
    // The original fullscreen/video-memory profile. Bit8 is /windowed,
    // not "8-bit color"; setting it pins palette endpoints and mutates uploads.
    memory.write(globals::video_mode_flags,runtime_profile::fullscreen_video);audio.initialize();map.attach_sprites(sprites);map.attach_actors(actors);
    environment.palette=&palette;environment.audio=&audio;environment.map=&map;environment.viewport=&viewport;environment.dialogue=&dialogue;environment.sprites=&sprites;
    environment.transition=&transition;environment.battle=&battle;environment.actors=&actors;
    environment.recovered=&battle.recovered;
    environment.debug_rewards=&debug_rewards;
    battle.recovered.effect_scripts=&effects;
    effects.play_cue=[this](unsigned cue){audio.play_cue(cue);};
    effects.stop_cue=[this](unsigned cue){audio.stop_cue(cue);};
    effects.invoke_actor=[this](Address callback,Address actor){battle.recovered.callback(callback,{actor});};
    environment.post_message=[this](std::uint32_t message,std::uint32_t key,std::uint32_t flags){post_input({message,key,flags});};
    dialogue.graphics=&graphics;dialogue.audio=&audio;graphics.sprites=&sprites;
    for(unsigned i=0;i<21;++i)memory.write(0x6db998+i*4,0x24000000+i);
    field.interact=[this](Address actor){battle.recovered.invoke(0x45d395,{actor});};
    field.position_trigger=[this]{
        if(!battle.recovered.invoke_byte(0x412181))return FieldEventTrigger{};
        const auto id=memory.scene_state().current_event;
        const auto entry=event_entry(id);
        if(entry)for(auto h:arena.members((id&0x10000)?1:0)){const auto p=*resolve_compact(memory,h);if(memory.read(p+compact_offset::callback)==routines::event_vm_tick&&memory.read(p+vm_offset::pc)==entry&&!memory.read(p+compact_offset::tick_count))return FieldEventTrigger{true,h};}
        return FieldEventTrigger{true,0}; // Inline dialogue is not a primary event root.
    };
    field.overlay_tick=[this](Address callback,Address slot){battle.recovered.invoke(callback,{slot});};
    map.setup_callback=[this](Address callback){battle.recovered.callback(callback);};
    actors.callback_finalizer=[this](Address callback,Address object){battle.recovered.callback(callback,{object});};
}
Address Runtime::event_entry(unsigned id)const{
    if(id<168)return memory.read(tables::event_definitions+id*4);
    if((id&0xffff0000u)==0x10000u)return memory.read(0x6d08cc+(id&0xffffu)*4); //4120c7 secondary event objects, group1.
    return 0;
}
void Runtime::set_active(bool active){
    memory.write(globals::runtime_mode_flags,(memory.read(globals::runtime_mode_flags)&~1u)|unsigned(active));
    if(!active){
        title_pointer_down_=-1;
        if(memory.read(globals::focus_palette_state)==1){palette.capture(globals::focus_palette_backup);memory.write(globals::focus_palette_state,0);}
        memory.write(globals::inactive_window,1);
    }else{
        memory.write(globals::inactive_window,0);
        if(!memory.read(globals::focus_palette_state)){palette.upload(globals::focus_palette_backup);memory.write(globals::focus_palette_state,1);}
        memory.write(globals::key_released_this_frame,1);memory.write(globals::key_pressed_this_frame,1);
    }
}
void Runtime::initialize_session(unsigned through_event){
    if(started_)throw Fault(0,"runtime already started");
    if(through_event>=168&&through_event!=full_campaign)throw Fault(through_event,"event boundary outside original table");
    through_event_=through_event;
    //460872 resets all768 actor slots before loading. The save loader spawns
    //party members but does not assign each slot's identity; those identities
    //must exist before E8/2 can hand field control to another party member.
    actors.reset_range(0,0x300);
    sprites.initialize();sprites.initialize_scene_sheets();
    // 431907 binds INI rows5..15. With no host settings supplied, use the
    // original descriptor defaults (not zero-filled BSS menu parameters).
    for(unsigned index=5;index<16;++index){
        const auto row=0x4a5230+index*20,pointer=memory.read(row+8);std::string value;
        for(unsigned i=0;memory.read(pointer+i,1);++i)value.push_back(char(memory.read(pointer+i,1)));
        const auto destination=0x772fec+(index-5)*4;memory.write(destination,std::uint32_t(std::stoi(value)));memory.write(row+12,destination);memory.write(row+16,memory.read(destination));
    }
    for(unsigned i=0;i<3;++i){const auto value=memory.read(0x772ff4+i*4);memory.write(0x5b15f0+i,value,1);memory.write(0x5b15f4+i,value/2,1);}
    dialogue.configure_timing(0,30,0);
    //0x431907 installs this rectangle-reset callback immediately aftergroup5 HEAD.
    arena.allocate_after(Arena::head(5),routines::reset_dialogue_rectangles,0x10000,0);
    started_=true;
}
void Runtime::begin_new_game(){
    actors.bootstrap_new_game_actors();
    // 460872 common new-game boot and486dd5's four unnamed event bits.
    memory.write(0x803a18,1000);
    for(unsigned id:{0x127u,0x129u,0x134u,0x36u}){const auto word=0x806b28+(id/32)*4;memory.write(word,memory.read(word)|(1u<<(id&31)));}
    memory.scene_state().game_mode=3;memory.write(0x804a64,7);current_event_=0;root_=arena.activate_event(0);
}
void Runtime::start_event0(unsigned through_event){
    initialize_session(through_event);begin_new_game();
}
void Runtime::start_saved_game(const std::vector<std::uint8_t>& bytes,unsigned through_event){
    if(!field_menu.valid_save(bytes))throw Fault(0,"invalid saved game");
    // The original 460872 title path restores before rendering any field frame.
    initialize_session(through_event);current_event_=0xffffffffu;
    memory.scene_state().current_event=0xffffffffu;
    if(!field_menu.load_snapshot(bytes))throw Fault(0,"saved game could not be restored");
    memory.write(0x804a64,7);memory.write(0x803a4c,3);memory.scene_state().game_mode=0;
}
void Runtime::start_field_fixture(unsigned map_id,unsigned through_event){
    start_event0(through_event);arena.release(root_);root_=0;current_event_=0xffffffffu;
    memory.scene_state().current_event=0xffffffffu;actors.set_party_mask(2,true);
    memory.write(0x5d22a0,map_id);map.enter_field(map_id,0);palette.upload(globals::palette_target);audio.apply_bgm(999);
}
void Runtime::dispatch(Address callback,Address object,unsigned jobs){
    switch(callback){
    case 0x464158:battle.tick_banner(object);break;
    case routines::event_vm_tick:
        if(!root_&&event_entry(memory.scene_state().current_event)&&memory.read(object+vm_offset::pc)==event_entry(memory.scene_state().current_event)){
            root_=compact_handle(memory,object);current_event_=memory.scene_state().current_event;
        }
        Vm(memory,messages,environment,object).run_frame(jobs);
        if(compact_handle(memory,object)==root_&&!memory.read(object+compact_offset::lifecycle)){
            root_exit_pc_=memory.read(object+vm_offset::pc);
            if(current_event_==0&&root_exit_pc_!=scripts::event0_terminator)throw Fault(root_exit_pc_,"Event0 root ended before its original terminator");
            completed_events_.push_back({current_event_,root_exit_pc_,memory.read(globals::frame_time_ms)});
            const auto next_id=memory.scene_state().current_event;
            // Event2 queues itself for a second activation. Its first root
            // terminator is a phase boundary, not completion of the whole event.
            if(next_id==0xffffffffu){
                // Event9 and field-triggered events return to ordinary play.
                // Their closing dialogue/controllers still get real ticks.
                boundary_field_cleanup_=current_event_==through_event_&&!(memory.read(0x768a98)&&memory.scene_state().resume_event==current_event_);
                root_=0;current_event_=0xffffffffu;
            }else if(current_event_==through_event_&&next_id!=current_event_)root_finished_=true;
            else{
                const auto entry=event_entry(next_id);if(!entry)throw Fault(root_exit_pc_,"event ended without an original pending-event activation");
                if(next_id<168&&next_id>through_event_)throw Fault(root_exit_pc_,"event chain passed the requested boundary without completing it");
                Handle next_root=0;
                for(auto candidate:arena.members((next_id&0x10000)?1:0)){
                    const auto next=*resolve_compact(memory,candidate);
                    if(candidate!=root_&&memory.read(next+compact_offset::callback)==routines::event_vm_tick&&memory.read(next+vm_offset::pc)==entry&&!memory.read(next+compact_offset::tick_count)){next_root=candidate;break;}
                }
                if(!next_root)throw Fault(entry,"original activated event root was not found");
                root_=next_root;current_event_=next_id;
            }
        }
        break;
    case routines::camera_follow_tick:camera.tick_followup(object,jobs);break;
    case routines::tile_tween_tick:actors.tick_tile_tween(object,memory.read(globals::shift_key_state)!=0);break;
    case routines::raw_tween_tick:actors.tick_raw_tween(object,jobs);break;
    case routines::audio_fade_tick:audio.tick_fade(object,jobs);break;
    case routines::viewport_tween_tick:case routines::viewport_border_tick:case routines::blit_border_tick:
        viewport.tick(object,jobs);
        for(const auto& fill:viewport.take_fills())surfaces.clear(fill.surface,fill.rect,fill.index);
        break;
    case routines::dialogue_tick:dialogue.tick(object);break;
    case 0x411f0f:graphics.tick_direct_text(object,jobs);break;
    case routines::reset_dialogue_rectangles:
        if(memory.read(object+compact_offset::lifecycle)==0xffffffffu)memory.write(object+compact_offset::lifecycle,0);
        memory.write(globals::occupied_dialogue_count,0);break;
    default:if(battle.handles_callback(callback))battle.tick_callback(callback,object);else throw Fault(callback,"runtime compact callback is not connected");break;
    }
}
void Runtime::game_frame(){
    if(memory.read(globals::shutdown_drain_requested))return; //E2/999 clears the original game callback.
    // 460872 dispatches its own title flow before the in-game dispatcher; the
    // defeat splash (state8) enters that flow and leaves through its title menu.
    if(intro_active_||memory.read(0x804a64)==8){tick_title();return;}
    const auto mode=memory.scene_state().game_mode;
    // 460545 mode0: title load enters the resource phase before field drawing.
    if(mode==0){battle.tick_map_transition();memory.write(0x803a28,0);memory.scene_state().game_mode=3;return;}
    if(mode==4){battle.begin_scripted();return;}
    if(mode==5||mode==6){battle.tick_shop();return;}
    if(mode==7){battle.recovered.invoke(0x45dcc8);memory.scene_state().game_mode=8;return;}
    if(mode==8)return; //460545's authored capture/hold modes await a later mode change.
    if(mode==10){battle.tick_worldmap();return;}
    if(mode==11){field_menu.tick();return;}
    if(mode!=3&&mode!=9)throw Fault(globals::game_mode,"game-mode dispatcher is not connected");
    map.tick_tile_animation();draw.reset();map.update_occupancy();
    //0x45f7f2 screen-shake option phase, before actor/effect callbacks.
    if(signed32(memory.read(0x768728))>=0)memory.write(0x804aa8,0);
    else{
        const auto old=memory.read(0x804aa8);const bool second=signed32(memory.read(0x768838))<0;
        memory.write(0x804aa8,second?2:1);if(old==(second?1u:0u))memory.write(0x773004,second?2:1);
    }
    if(mode==9)battle.tick();
    memory.write(0x77ece0,0);
    // Actual enable bits decide whether a callback exists for this frame.
    // Disabled player motion during Event0 is not an unimplemented callback.
    for(Address object=globals::actor_objects;object+actor_offset::callback<0x857271;object+=layout::actor_size)
        if((memory.read(object+actor_offset::flags)&0x10000)&&memory.read(object+actor_offset::callback)){
            const auto callback=memory.read(object+actor_offset::callback);
            if(callback==routines::map_marker_tick)actors.tick_map_marker(object);
            else if(callback==routines::field_actor_movement)battle.tick_player(object);
            else if(callback==0x45b8a0){const auto motion=actors.tick_npc(object);if(motion.sound)audio.play_cue(*motion.sound);}
            else if(callback==0x447e31)actors.tick_bound_son(object);
            else if(callback==0x45c526){const auto motion=actors.tick_default_visual(object);if(motion.sound)audio.play_cue(*motion.sound);}
            else if(battle.handles_callback(callback))battle.tick_callback(callback,object);
            else throw Fault(callback,"enabled actor callback is not connected");
            memory.write(object+actor_offset::callback_tick_count,memory.read(object+actor_offset::callback_tick_count)+1);
        }
    // 45f82d calls4478ba once after actor callbacks.4478ba already scans every
    // actor; calling it for each active effect advances all scripts repeatedly.
    effects.tick();
    if(mode==3)if(const auto event=field.tick_overlays(map,arena)){root_=event;current_event_=memory.scene_state().current_event;}
    camera.tick_lines();
    if(memory.read(0x6da5dc)&&memory.read(0x804a60,1)){
        if(memory.read(globals::up_held))memory.write(globals::camera_focus_y_q16,memory.read(globals::camera_focus_y_q16)-0x80000);
        if(memory.read(globals::down_held))memory.write(globals::camera_focus_y_q16,memory.read(globals::camera_focus_y_q16)+0x80000);
        if(memory.read(globals::left_held))memory.write(globals::camera_focus_x_q16,memory.read(globals::camera_focus_x_q16)-0x80000);
        if(memory.read(globals::right_held))memory.write(globals::camera_focus_x_q16,memory.read(globals::camera_focus_x_q16)+0x80000);
    }
    const auto focus=Actors::slot(memory.read(globals::camera_focus_actor_index));
    const auto x=signed32(memory.read(focus+8))/65536,y=signed32(memory.read(focus+12))/65536;
    const bool bounded=memory.read(globals::camera_bounds_enabled)!=0;
    camera.clamp_target(x+1,y-23,bounded);camera.update_scroll_bounds(x-128,y-96,bounded);map.update_scroll();
    for(unsigned i=0;Actors::slot(i)+4<0x85712d;++i)if(memory.read(Actors::slot(i)+4)&64)draw.actor(i);
    // Original45f82d phase E: battle ranges tint background tiles, while
    // foreground occluders use the transparent checker pass in battle.
    const bool battle_view=memory.scene_state().game_mode==9;
    const auto effect_flags=memory.read(globals::battle_cursor_effect_flags);
    if(battle_view&&(effect_flags&7))palette.upload(globals::battle_range_palette,224,6);
    const auto tile_mask=battle_view&&memory.read(globals::battle_command_substate)==4&&!memory.read(globals::battle_command_result_phase)?effect_flags:0;
    const auto layers=memory.read(globals::active_map_layer_count),backgrounds=memory.read(globals::background_layer_count);
    for(unsigned layer=0;layer<layers;++layer){
        const auto pass=layer*2+1;
        if(layer<backgrounds)draw.background(map.background_commands(layer,tile_mask),0x804d10);
        memory.write(0x800da8+layer*4,memory.read(globals::draw_pass_counts+layer*8)); //45f82d snapshots the even background pass.
        draw.shadows(pass,memory.read(0x5cd3f0));draw.foreground(layer,battle_view);
        const auto first=memory.read(globals::draw_queue_count);draw.flush(pass);draw.sort(pass,first);
    }
    draw.execute();
    if(memory.read(0x803a4c))battle.tick_map_transition();
    // Field menu/encounter processing is gated by the active event in0x45f82d.
    if(memory.scene_state().game_mode==3&&memory.read(0x77ec54))memory.scene_state().game_mode=4;
    if(memory.scene_state().game_mode==3&&!memory.read(0x802c9c,1)&&memory.scene_state().current_event==0xffffffffu&&!palette.busy()&&!memory.read(0x77ec54)){
        const auto actor=Actors::slot(memory.read(globals::active_party_index));
        if(!memory.read(actor+actor_offset::motion_state)&&!memory.read(globals::field_transition_phase)&&battle.recovered.invoke_byte(0x448b4c)){memory.scene_state().game_mode=4;return;}
        else field_menu.field_input();
        if(memory.scene_state().game_mode==11)return; // Original menu-opening paths skip the cheat tail.
    }
    if(memory.read(0x77ec54))return; // 45faaa: pending scripted encounter skips the cheat tail.
    original_cheats::tick(*this);
}
void Runtime::due_timers(std::uint32_t now){
    auto previous=Address(0x6d4b10),current=memory.read(0x6d4b20);unsigned guard=0;
    while(current){
        if(++guard>10000)throw Fault(current,"timer message list cycle");
        if(memory.read(current)<now){
            inputs_.push_back({memory.read(current+4),memory.read(current+8),memory.read(current+12)});
            memory.write(previous+16,memory.read(current+16));memory.release_allocation(current);current=memory.read(previous+16);
        }else{previous=current;current=memory.read(current+16);}
    }
}
void Runtime::end_frame(){
    // Preserve the terminal PC before4029db marks the root for release.
    // Shutdown cancellation does not invent a normal event-completion record.
    if(memory.read(globals::shutdown_drain_requested)&&!memory.read(globals::shutdown_idle_frames))
        if(const auto root=resolve_compact(memory,root_))root_exit_pc_=memory.read(*root+vm_offset::pc);
    battle.recovered.invoke(routines::finish_runtime_frame);
}
RuntimeStep Runtime::advance(std::uint32_t now,bool produce_pcm){
    if(!started_)throw Fault(0,"runtime must be started before advancing");
    if(quit_requested())return {};
    if(root_finished_){RuntimeStep finished;finished.event0_finished=event0_finished();finished.run_finished=true;return finished;}
    RuntimeStep step;step.event0_finished=event0_finished();audio.set_time(now);
    const auto samples=std::uint64_t(now)*44100/1000;
    if(samples<audio.output_sample()||samples-audio.output_sample()>441000)throw Fault(now,"audio clock discontinuity outside current runtime host scope");
    const auto count=unsigned(samples-audio.output_sample());
    if(produce_pcm)step.pcm=audio.mix(count);else audio.advance_silently(count);
    if(!memory.read(globals::input_message)&&!inputs_.empty()){
        auto event=inputs_.front();inputs_.pop_front();
        if(event.kind==input_message::close){close_requested_=true;return step;}
        if(intro_active_&&event.kind==input_message::mouse_button&&event.positioned)event=title_pointer(event);
        memory.write(globals::input_message,event.kind);memory.write(globals::input_key,event.key);memory.write(globals::input_flags,event.flags);
        apply_input_state(memory,event);
    }
    if((memory.read(globals::runtime_mode_flags)&3)!=1){memory.write(globals::input_message,0);return step;} // Original inactive/busy path skips the frame clock.
    clock_.set_interval(memory.read(globals::phase_interval_ms));clock_.set_fast4(memory.read(globals::fast_animation_gate)!=0);
    step.jobs=clock_.advance(now);memory.write(globals::phase_clock_base_ms,clock_.phase_base());memory.write(globals::job_clock_base_ms,clock_.batch_base());memory.write(globals::frame_job_count,step.jobs);
    if(!step.jobs)return step;
    // Map tiles repaint only the viewport. Unclipped skill-name overlays can
    // cross its margins, so retire their previous-frame ink before callbacks
    // draw this frame (including explicit border fills). Capture/hold modes
    // keep their retained surface. clear() also discards enhanced glyph detail.
    const auto mode=memory.scene_state().game_mode;
    if(!intro_active_&&memory.read(0x804a64)!=8&&(mode==3||mode==9)){
        const auto target=memory.read(globals::render_target_surface);
        const int left=signed32(memory.read(globals::viewport_left)),top=signed32(memory.read(globals::viewport_top));
        const int right=signed32(memory.read(globals::viewport_right)),bottom=signed32(memory.read(globals::viewport_bottom));
        surfaces.clear(target,{0,0,int(width_),top});
        surfaces.clear(target,{0,bottom,int(width_),int(height_)});
        surfaces.clear(target,{0,top,left,bottom});
        surfaces.clear(target,{right,top,int(width_),bottom});
    }
    environment.input_pause=memory.read(globals::shift_key_state)!=0;environment.dialog_busy=false;
    environment.key_down=memory.read(globals::input_message)==0x100;environment.input_high_blocked=(memory.read(globals::input_flags)&0x40000000)!=0;
    pump_.latch_frame_time(now);
    for(unsigned group=0;group<3;++group){
        pump_.update_group(group,step.jobs,[this]{return root_finished_;});
        if(root_finished_){step.event0_finished=event0_finished();step.run_finished=true;return step;}
    }
    step.event0_finished=event0_finished();
    game_frame();
    if(quit_requested())return step;
    for(unsigned group=3;group<6;++group)pump_.update_group(group,step.jobs);
    graphics.text_overlays(palette);
    due_timers(now);pump_.update_group(6,step.jobs);
    if(!transition.present(now))surfaces.blit(primary_,0,0,memory.read(globals::render_target_surface),{0,0,int(width_),int(height_)});
    end_frame();audio.tick_global_fade(step.jobs);palette.advance(step.jobs);
    surfaces.get(primary_).palette=palette.colors();step.rendered=true;
    if(boundary_field_cleanup_&&!memory.read(globals::live_dialogue_count)&&!messages.size()&&!palette.busy()&&arena.members(0).empty()&&arena.members(1).empty()){
        root_finished_=true;step.run_finished=true;
    }
    return step;
}
} // namespace fsb::core
