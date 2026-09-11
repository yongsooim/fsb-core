#include "fsb_core/checkpoint_context.hpp"
#include "field_input_fixture.hpp"
#include "campaign_input_fixture.hpp"
#include "battle_input_fixture.hpp"
#include "debug_stat_fixture.hpp"
#include "runtime_assets.hpp"
#include "input_trace.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace fsb::core;
int main(int argc,char** argv){
    try{
        if(argc<3){std::cerr<<"Usage: fsb_event0_run ASSETS OUTPUT_DIR [duration_ms=15000] [confirm_interval_ms=0] [--through-event N] [--inputs TRACE.tsv] [--capture-every-ms N] [--render-size W,H] [--diagnostics] [--no-pcm] [--load-save FILE] [--save-final FILE]\n";return 2;}
        const std::filesystem::path assets(argv[1]),output(argv[2]);std::filesystem::create_directories(output);
        unsigned arena_return_map=0xffffffffu;
        unsigned duration=15000,confirm=0,capture_interval=250,through_event=0,field_map=0xffffffffu,approach_actor=0xffffffffu;bool diagnostics=false,battle_inputs=false,no_pcm=false;std::filesystem::path input_path,inventory_path,load_save,save_final;
        unsigned render_width=0,render_height=0;fsb::lab::CampaignInputFixture campaign;bool battle_skills=false,reward_boost=false;
        fsb::lab::DebugStatFixture debug_stats;
        std::vector<unsigned> approach_sequence;fsb::lab::FieldWalkFixture field_walk;fsb::lab::WorldInputFixture world_input;std::vector<fsb::lab::FieldWalkFixture> walk_queue;std::size_t walk_cursor=0;
        int argument=3;
        if(argument<argc&&argv[argument][0]!='-')duration=unsigned(std::stoul(argv[argument++]));
        if(argument<argc&&argv[argument][0]!='-')confirm=unsigned(std::stoul(argv[argument++]));
        while(argument<argc){
            const std::string option=argv[argument++];
            if(option=="--diagnostics")diagnostics=true;
            else if(option=="--campaign")through_event=Runtime::full_campaign;
            else if(option=="--reward-boost")reward_boost=true;
            else if(option=="--no-reward-boost")reward_boost=false;
            else if(option=="--debug-stat-boost")debug_stats.enabled=true;
            else if(option=="--campaign-inputs"&&argument<argc)campaign.load(argv[argument++]);
            else if(option=="--no-pcm")no_pcm=true;
            else if(option=="--load-save"&&argument<argc)load_save=argv[argument++];
            else if(option=="--arena-return-map"&&argument<argc)arena_return_map=unsigned(std::stoul(argv[argument++]));
            else if(option=="--save-final"&&argument<argc)save_final=argv[argument++];
            else if(option=="--render-size"&&argument<argc){std::string spec=argv[argument++];std::replace(spec.begin(),spec.end(),',',' ');std::istringstream values(spec);if(!(values>>render_width>>render_height)||!render_width||!render_height||render_width>8192||render_height>8192)throw std::runtime_error("render size needs WIDTH,HEIGHT");}
            else if(option=="--battle-input-fixture")battle_inputs=true;
            else if(option=="--battle-skill-inputs")battle_inputs=battle_skills=true;
            else if(option=="--world-destination"&&argument<argc)world_input.destination=unsigned(std::stoul(argv[argument++]));
            else if(option=="--inventory-from"&&argument<argc)inventory_path=argv[argument++];
            else if(option=="--inputs"&&argument<argc)input_path=argv[argument++];
            else if(option=="--capture-every-ms"&&argument<argc)capture_interval=unsigned(std::stoul(argv[argument++]));
            else if(option=="--through-event"&&argument<argc)through_event=unsigned(std::stoul(argv[argument++]));
            else if(option=="--approach-actors"&&argument<argc){std::string spec=argv[argument++];std::replace(spec.begin(),spec.end(),',',' ');std::istringstream values(spec);unsigned actor;while(values>>actor)approach_sequence.push_back(actor);if(approach_sequence.empty())throw std::runtime_error("empty actor sequence");}
            else if(option=="--approach-actor"&&argument<argc)approach_actor=unsigned(std::stoul(argv[argument++]));
            else if(option=="--walk-after-ms"&&argument<argc)(walk_queue.empty()?field_walk:walk_queue.back()).start_ms=unsigned(std::stoul(argv[argument++]));
            else if(option=="--walk-to"&&argument<argc){std::string spec=argv[argument++];std::replace(spec.begin(),spec.end(),',',' ');std::istringstream values(spec);fsb::lab::FieldWalkFixture goal;if(!(values>>goal.map>>goal.x>>goal.y))throw std::runtime_error("walk goal needs MAP,X,Y");if(field_walk.map==0xffffffffu)field_walk=goal;else walk_queue.push_back(goal);}
            else if(option=="--field-fixture"&&argument<argc)field_map=unsigned(std::stoul(argv[argument++]));
            else throw std::runtime_error("unknown/incomplete replay option: "+option);
        }
        if(!load_save.empty()&&!inventory_path.empty())throw std::runtime_error("save restore and inventory fixture are mutually exclusive");
        if(!load_save.empty()&&field_map!=0xffffffffu)throw std::runtime_error("save restore and isolated field fixture are mutually exclusive");
        if(!inventory_path.empty()&&field_map==0xffffffffu)throw std::runtime_error("inventory fixture requires an isolated --field-fixture");
        if(confirm&&!input_path.empty())throw std::runtime_error("periodic input and an input trace are mutually exclusive");
        if(campaign.enabled()&&(confirm||!input_path.empty()||field_walk.map!=0xffffffffu||world_input.destination!=0xffffffffu||approach_actor!=0xffffffffu||!approach_sequence.empty()))throw std::runtime_error("campaign input script must own field input");
        const auto replay=input_path.empty()?std::vector<fsb::lab::TimedInput>{}:fsb::lab::read_input_trace(input_path);
        Runtime runtime(fsb::lab::read(assets/"FLYINGSB.EXE"));auto& memory=runtime.memory;
        runtime.debug_rewards.enable(reward_boost);
        FontRaster fonts(memory);fsb::lab::register_runtime_assets(runtime,fonts,assets,field_map==0xffffffffu&&load_save.empty()?through_event:std::max(9u,through_event));
        fonts.set_enhanced(render_width!=0);
        std::ofstream trace(output/"instructions.tsv"),states(output/"frames.tsv"),inputs(output/"inputs.tsv"),captures_file(output/"captures.tsv"),pcm(output/"audio.s16le",std::ios::binary),dialogs,visual,battle_trace;
        trace<<"ms\tobject\tpc\topcode\tsubop\tnext_pc\n";states<<"ms\tjobs\troot_pc\tcamera_x\tcamera_y\tviewport_top\tviewport_bottom\tdialogues\tqueue\n";inputs<<"ms\tkind\tkey\tflags\tshift\tcontrol\n";captures_file<<"index\tms\tfile\troot_pc\n";
        if(diagnostics){battle_trace.open(output/"battle.tsv");battle_trace<<"ms\tmajor\tsub\tcontext\tx\ty\tfacing\tcallback\tgate\ttargets\tparty_hp\tenemy_hp\n";dialogs.open(output/"dialogues.tsv");dialogs<<"ms\tcontroller\tchannel\ttext\tcursor\tline\tcolumn\tflags_a\tflags_b\tflags_c\tleft\ttop\tright\tbottom\n";visual.open(output/"visual.bin",std::ios::binary);visual.write("FSBVIS1\0",8);}
        runtime.environment.trace=[&](Address object,const Instruction& ins,bool after){if(after)trace<<memory.read(0x6da2d8)<<"\t0x"<<std::hex<<object<<"\t0x"<<ins.pc<<"\t0x"<<unsigned(ins.opcode)<<"\t0x"<<unsigned(ins.subop)<<"\t0x"<<memory.read(object+0x30)<<std::dec<<'\n';};
        std::map<std::string,std::vector<std::uint8_t>> checkpoint_files;
        if(!load_save.empty()||!save_final.empty()||campaign.enabled()){
            if(!load_save.empty())checkpoint_files["Save1.dat"]=fsb::lab::read(load_save);
            runtime.field_menu.read_file=[&](const std::string& name)->std::optional<std::vector<std::uint8_t>>{const auto f=checkpoint_files.find(name);return f==checkpoint_files.end()?std::nullopt:std::optional<std::vector<std::uint8_t>>(f->second);};
            runtime.field_menu.write_file=[&](const std::string& name,const auto& bytes){checkpoint_files[name]=bytes;return true;};
            runtime.field_menu.remove_file=[&](const std::string& name){return checkpoint_files.erase(name)!=0;};
            if(!load_save.empty()){
                if(!runtime.field_menu.valid_save(checkpoint_files.at("Save1.dat")))throw std::runtime_error("invalid campaign checkpoint");
                runtime.start_saved_game(checkpoint_files.at("Save1.dat"),through_event);
                if(arena_return_map!=0xffffffffu&&!restore_secret_arena_return(memory,arena_return_map))throw std::runtime_error("invalid secret arena return context");
                std::ofstream(output/"save-origin.txt")<<std::filesystem::absolute(load_save).string()<<'\n';
            }
        }
        if(load_save.empty()){if(field_map==0xffffffffu)runtime.start_event0(through_event);else{
            runtime.start_field_fixture(field_map,through_event);
            if(!inventory_path.empty())fsb::lab::restore_inventory_fixture(memory,inventory_path);
            std::ofstream fixture(output/"fixture-state.json");fixture<<"{\"scope\":\"isolated field fixture, not restored campaign state\",\"map\":"<<field_map<<",\"party_mask\":2}\n";
        }}
        campaign.save_checkpoint=[&](unsigned id){
            if(!runtime.field_menu.save(1))throw std::runtime_error("original checkpoint writer refused field save");
            const auto& bytes=checkpoint_files.at("Save1.dat");const auto path=output/("checkpoint-"+std::to_string(id)+".dat");
            std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();if(!file)throw std::runtime_error("checkpoint write failed");
            std::cout<<"checkpoint="<<path.string()<<" map="<<memory.read(globals::current_map_id)<<'\n'<<std::flush;
        };
        if(!inventory_path.empty()){std::ofstream provenance(output/"inventory-fixture.json");provenance<<"{\"source\":"<<std::quoted(std::filesystem::absolute(inventory_path).string())<<",\"copied\":\"gold and inventory only; no battle or event completion state\"}\n";}
        fsb::lab::BattleInputFixture battle_input;battle_input.use_offensive_skills=battle_skills;battle_input.manage_field_dialogue=!campaign.enabled();fsb::lab::FieldInputFixture field_input;field_input.actor_sequence=approach_sequence;field_input.template_id=approach_sequence.empty()?approach_actor:approach_sequence.front();
        unsigned now=0,frames=0,captures=0,next_capture=0,battle_snapshots=0;std::size_t replay_cursor=0;std::string error;bool completed=false,defeated=false,closed=false,setup_snapshot_taken=false;
        std::ofstream stat_grants;
        if(debug_stats.enabled){stat_grants.open(output/"debug-stat-grants.tsv");stat_grants<<"ms\tcharacter\tfield_offset\tbefore\tafter\n";}
        const auto post=[&](const InputMessage& event){runtime.post_input(event);inputs<<now<<'\t'<<event.kind<<'\t'<<event.key<<'\t'<<event.flags<<'\t'<<event.shift<<'\t'<<event.control<<'\n';};
        try{
            for(now=0;now<=duration;++now){
                for(const auto& change:debug_stats.apply(runtime))for(unsigned i=0;i<change.before.size();++i)
                    stat_grants<<now<<'\t'<<change.character<<"\t0x"<<std::hex<<fsb::lab::DebugStatFixture::offsets[i]<<std::dec<<'\t'<<change.before[i]<<'\t'<<change.after[i]<<'\n';
                if(diagnostics&&memory.read(0x80465c)==4&&!setup_snapshot_taken){
                    fsb::lab::guest_snapshot(memory,output/("battle-setup-"+std::to_string(battle_snapshots++)+".bin"));setup_snapshot_taken=true;
                }else if(memory.read(0x80465c)!=4)setup_snapshot_taken=false;
                if(confirm&&now&&now%confirm==0)post(keyboard_message(13,0x1c,true));
                if(confirm&&now>48&&now%confirm==48)post(keyboard_message(13,0x1c,false));
                while(replay_cursor<replay.size()&&replay[replay_cursor].ms==now)post(replay[replay_cursor++].message);
                if(const auto event=campaign.next(runtime,now))post(*event);
                if(const auto event=field_input.next(runtime,now))post(*event);
                if(field_walk.settled()&&walk_cursor<walk_queue.size())field_walk=walk_queue[walk_cursor++];
                if(const auto event=field_walk.next(runtime,now))post(*event);
                if(const auto event=world_input.next(runtime,now))post(*event);
                if(battle_inputs)if(const auto event=battle_input.next(runtime,now))post(*event);
                const auto step=runtime.advance(now,!no_pcm);
                if(runtime.quit_requested()){closed=true;break;}
                for(auto sample:step.pcm){const auto value=std::uint16_t(sample);pcm.put(char(value));pcm.put(char(value>>8));}
                if(runtime.game_over())defeated=true;
                if(campaign.completed()&&memory.read(globals::game_mode)==3&&memory.read(globals::current_event_id)==0xffffffffu&&!memory.read(globals::live_dialogue_count)&&!runtime.messages.size()&&!memory.read(0x803a4c)&&!runtime.palette.busy())break;
                if(step.run_finished){
                    bool next_event=false;
                    const auto next_id=memory.read(0x57fd1c),entry=next_id<168?memory.read(0x6d0144+next_id*4):0;
                    for(auto handle:runtime.arena.members(0)){const auto object=*resolve_compact(memory,handle);
                        next_event|=memory.read(object+0x14)==0x41a042&&memory.read(object+0x30)==entry&&memory.read(object+0x24)==0;
                    }
                    if(resolve_compact(memory,runtime.root())||memory.read(0x57fd20)!=through_event||memory.read(0x768684)||runtime.messages.size()||(!next_event&&next_id!=0xffffffffu))
                        throw Fault(runtime.root_pc(),"event handoff state does not meet the original termination contract");
                    if(through_event==0&&(runtime.root_pc()!=0x61f96a||next_id!=1||memory.read(0x768688)!=0xffffffffu))throw Fault(runtime.root_pc(),"Event0 termination contract mismatch");
                    completed=true;break;
                }
                if(!step.rendered)continue;++frames;
                states<<now<<'\t'<<step.jobs<<"\t0x"<<std::hex<<runtime.root_pc()<<std::dec<<'\t'<<signed32(memory.read(0x7873c0))<<'\t'<<signed32(memory.read(0x7873c4))<<'\t'<<signed32(memory.read(0x6d9d34))<<'\t'<<signed32(memory.read(0x6d9d3c))<<'\t'<<memory.read(0x768684)<<'\t'<<runtime.messages.size()<<'\n';
                if(diagnostics&&memory.read(0x80465c)==9){
                    const auto context=memory.read(0x7757e0),actor=context<768?Actors::slot(context):Actors::slot(0);battle_trace<<now;
                    for(auto a:{0x775cb0u,0x775cacu,0x7757e0u,actor+0x128,actor+0x12c,actor+0x110})battle_trace<<'\t'<<signed32(memory.read(a));
                    for(auto a:{actor+0x148,0x77e570u})battle_trace<<"\t0x"<<std::hex<<memory.read(a)<<std::dec;
                    battle_trace<<'\t'<<memory.read(0x77a50c)<<'\t';
                    for(unsigned i=0;i<memory.read(0x803a20);++i)battle_trace<<(i?",":"")<<signed32(memory.read(BattleRules::party_record(memory.read(0x5d2258+i*4))+0x1c));
                    battle_trace<<'\t';for(unsigned i=0;i<memory.read(0x776484);++i)battle_trace<<(i?",":"")<<signed32(memory.read(BattleRules::enemy_record(i)+20));battle_trace<<'\n';
                }
                if(diagnostics)for(auto handle:runtime.arena.members(5)){
                    const auto object=*resolve_compact(memory,handle);if(memory.read(object+0x14)!=0x40dd27)continue;
                    const auto d=memory.read(object+0x1a4);if(!d)continue;
                    dialogs<<now<<"\t0x"<<std::hex<<object<<"\t0x"<<memory.read(object+0xf4)<<"\t0x"<<memory.read(d+8)<<std::dec<<'\t'<<memory.read(d+0x150)<<'\t'<<memory.read(d+0x144)<<'\t'<<memory.read(d+0x14c);
                    for(auto offset:{0x154,0x158,0x15c})dialogs<<"\t0x"<<std::hex<<memory.read(d+offset)<<std::dec;
                    for(auto offset:{0xc4,0xc8,0xcc,0xd0})dialogs<<'\t'<<signed32(memory.read(d+offset));dialogs<<'\n';
                }
                if(diagnostics){
                    for(unsigned i=0;i<4;++i)visual.put(char(now>>(i*8)));
                    const auto& image=runtime.frame();
                    for(unsigned y=20;y<480;y+=40)for(unsigned x=20;x<640;x+=40){const auto color=image.palette[image.pixels[y*640+x]];for(auto c:{color.r,color.g,color.b})visual.put(char(((c>>2)<<2)|(c>>6)));}
                }
                if(capture_interval&&now>=next_capture){std::ostringstream filename;filename<<"frame-"<<std::setfill('0')<<std::setw(4)<<captures<<".bmp";fsb::lab::bmp(runtime.frame(),output/filename.str());captures_file<<captures++<<'\t'<<now<<'\t'<<filename.str()<<"\t0x"<<std::hex<<runtime.root_pc()<<std::dec<<'\n';next_capture+=capture_interval;}
                if(defeated)break;
            }
        }catch(const Fault& fault){error=fault.what();std::cerr<<"Stopped at"<<now<<"ms: "<<error<<'\n';
            if(!fault.recovered_calls.empty()){std::cerr<<"Original calls (innermost first):";for(auto entry:fault.recovered_calls)std::cerr<<" 0x"<<std::hex<<entry;std::cerr<<std::dec<<'\n';}
            if(diagnostics)fsb::lab::guest_snapshot(memory,output/"fault-memory.bin");}
        if(diagnostics&&through_event>=9)fsb::lab::guest_snapshot(memory,output/"last-state.bin");
        fsb::lab::bmp(runtime.frame(),output/"last-frame.bmp");
        if(render_width){fsb::lab::bmp(present_image(runtime.frame(),render_width,render_height),output/"presented-enhanced.bmp");fsb::lab::bmp(present_image(runtime.frame(),render_width,render_height,PresentationMode::original),output/"presented-original.bmp");}
        bool checkpoint_written=false;std::string checkpoint_error;
        if(!save_final.empty()&&error.empty()&&!defeated)try{
            if(memory.read(0x57fd1c)!=0xffffffffu||memory.read(0x80465c)!=3||memory.read(0x768684)||runtime.messages.size())throw std::runtime_error("checkpoint requires a quiescent field boundary");
            if(!runtime.field_menu.save(1))throw std::runtime_error("checkpoint writer failed");
            const auto& bytes=checkpoint_files.at("Save1.dat");std::ofstream f(save_final,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());f.close();if(!f)throw std::runtime_error("checkpoint file write failed");
            checkpoint_written=true;
        }catch(const std::exception& failure){checkpoint_error=failure.what();std::cerr<<"Checkpoint not saved: "<<checkpoint_error<<'\n';}
        std::ofstream cues(output/"audio-events.tsv");cues<<"kind\tbgm\tid\thandle\toutput_sample\tmillibels\n";
        for(const auto& cue:runtime.audio.events())cues<<unsigned(cue.kind)<<'\t'<<cue.bgm<<'\t'<<cue.id<<'\t'<<cue.handle<<'\t'<<cue.output_sample<<'\t'<<cue.millibels<<'\n';
        std::ofstream event_log(output/"events.tsv");event_log<<"event\texit_pc\tms\n";
        for(const auto& event:runtime.completed_events())event_log<<event.id<<"\t0x"<<std::hex<<event.exit_pc<<std::dec<<'\t'<<event.ms<<'\n';
        const bool fixture_inputs=campaign.enabled()||battle_inputs||field_input.template_id!=0xffffffffu||field_walk.map!=0xffffffffu||world_input.destination!=0xffffffffu;
        std::ofstream report(output/"runtime.json");
        report<<"{\n  \"scope\": \"original event chain with virtual millisecond clock and explicit input fixture\",\n  \"recording_exact_parity_verified\": false,\n  \"event0_script_completed\": "<<(runtime.event0_finished()?"true":"false")<<",\n  \"root_alive\": "<<(resolve_compact(memory,runtime.root())?"true":"false")<<",\n  \"handoff_event_id\": "<<signed32(memory.read(0x57fd1c))<<",\n  \"event0_activation_count\": "<<signed32(memory.read(0x768688))<<",\n  \"live_dialogues\": "<<memory.read(0x768684)<<",\n  \"messages\": "<<runtime.messages.size()<<",\n  \"last_ms\": "<<now<<",\n  \"rendered_frames\": "<<frames<<",\n  \"captures\": "<<captures<<",\n  \"confirm_interval_ms\": "<<confirm<<",\n  \"root_pc\": \"0x"<<std::hex<<runtime.root_pc()<<std::dec<<"\",\n  \"stopped_on_fault\": "<<(error.empty()?"false":"true")<<",\n  \"fault\": \"";
        for(char c:error){if(c=='"'||c=='\\')report<<'\\';report<<c;}
        report<<"\",\n  \"through_event\": "<<through_event<<",\n  \"current_event\": "<<runtime.current_event()<<",\n  \"run_completed\": "<<(completed?"true":"false")<<",\n  \"input_mode\": \""<<(fixture_inputs?(input_path.empty()?"fixture":"trace+fixture"):(input_path.empty()?(confirm?"periodic":"none"):"trace"))<<"\",\n  \"capture_interval_ms\": "<<capture_interval<<",\n  \"replayed_input_events\": "<<replay_cursor<<",\n  \"font_bitmap_cache_entries\": "<<fonts.bitmap_glyphs()<<",\n  \"font_outline_cache_entries\": "<<fonts.outline_glyphs()<<",\n  \"next_event_instructions_executed\": 0";
        report<<",\n  \"pcm_written\": "<<(!no_pcm?"true":"false");
        report<<",\n  \"debug_reward_multiplier\": "<<runtime.debug_rewards.multiplier();
        if(debug_stats.enabled)report<<",\n  \"debug_stat_multiplier\": "<<fsb::lab::DebugStatFixture::multiplier;
        if(closed)report<<",\n  \"quit_requested\": true";
        report<<",\n  \"campaign_input_completed\": "<<(campaign.completed()?"true":"false")<<",\n  \"campaign_input_command\": "<<campaign.cursor();
        report<<",\n  \"checkpoint_written\": "<<(checkpoint_written?"true":"false")<<",\n  \"checkpoint_error\": "<<std::quoted(checkpoint_error);
        if(through_event>=9)report<<",\n  \"game_over\": "<<(defeated?"true":"false")<<",\n  \"fixture_conversation_completed\": "<<(field_input.completed()?"true":"false")<<",\n  \"fixture_walk_completed\": "<<(field_walk.done?"true":"false")<<",\n  \"game_mode\": "<<memory.read(0x80465c)<<",\n  \"map_id\": "<<memory.read(0x5d229c);
        report<<"\n}\n";
        std::cout<<"scheduled_frames="<<frames<<" last_ms="<<now<<" root_pc=0x"<<std::hex<<runtime.root_pc()<<std::dec<<" through_event="<<through_event<<" completed_events="<<runtime.completed_events().size()<<" requested_boundary_completed="<<completed<<"\n";
        return !error.empty()?3:defeated?5:!checkpoint_error.empty()?2:(completed||closed||campaign.completed())?0:4;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
