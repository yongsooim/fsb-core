#include "runtime_assets.hpp"
#include "input_trace.hpp"
#include "host_audio.hpp"
#include "host_presentation.hpp"
#include "save_directory.hpp"
#include "debug_panel.hpp"
#include "fsb_core/playback.hpp"
#include "fsb_core/symbols.hpp"
#include "fsb_core/original_cheats.hpp"
#include <ctime>
#include <SDL3/SDL.h>
#include <iostream>
#include <memory>
#include <cmath>
#include <numeric>
#include <iomanip>

namespace {
void sdl_check(bool ok){if(!ok)throw std::runtime_error(SDL_GetError());}
struct SdlLifetime{~SdlLifetime(){SDL_Quit();}};
unsigned legacy_key(SDL_Keycode key){
    if(key>=SDLK_F1&&key<=SDLK_F5)return unsigned(key-SDLK_F1)+112;
    if(key>=SDLK_A&&key<=SDLK_Z)return unsigned(key-SDLK_A)+'A';
    if(key==SDLK_RETURN||key==SDLK_KP_ENTER)return 13;
    if(key==SDLK_SPACE)return 32;
    if(key==SDLK_ESCAPE)return 27;
    if(key==SDLK_LSHIFT||key==SDLK_RSHIFT)return 16;
    if(key==SDLK_LCTRL||key==SDLK_RCTRL)return 17;
    if(key==SDLK_LALT||key==SDLK_RALT)return 18;
    if(key==SDLK_PAUSE)return 19;
    if(key==SDLK_KP_5)return 101;
    if(key==SDLK_KP_1)return 97;
    if(key==SDLK_END)return 35;
    if(key==SDLK_LEFT)return 37;if(key==SDLK_UP)return 38;if(key==SDLK_RIGHT)return 39;if(key==SDLK_DOWN)return 40;
    if(key>=SDLK_0&&key<=SDLK_9)return unsigned(key);
    return 0;
}
unsigned pc_scancode(SDL_Scancode scan){
    if(scan>=SDL_SCANCODE_F1&&scan<=SDL_SCANCODE_F5)return unsigned(scan-SDL_SCANCODE_F1)+0x3b;
    constexpr unsigned letters[]={0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
    if(scan>=SDL_SCANCODE_A&&scan<=SDL_SCANCODE_Z)return letters[scan-SDL_SCANCODE_A];
    if(scan>=SDL_SCANCODE_1&&scan<=SDL_SCANCODE_9)return unsigned(scan-SDL_SCANCODE_1)+2;
    switch(scan){
    case SDL_SCANCODE_ESCAPE:return 0x01;case SDL_SCANCODE_0:return 0x0b;case SDL_SCANCODE_RETURN:return 0x1c;case SDL_SCANCODE_KP_ENTER:return 0x9c;
    case SDL_SCANCODE_SPACE:return 0x39;case SDL_SCANCODE_LSHIFT:return 0x2a;case SDL_SCANCODE_RSHIFT:return 0x36;
    case SDL_SCANCODE_LCTRL:return 0x1d;case SDL_SCANCODE_RCTRL:return 0x9d;case SDL_SCANCODE_LALT:return 0x38;case SDL_SCANCODE_RALT:return 0xb8;
    case SDL_SCANCODE_KP_5:return 0x4c;case SDL_SCANCODE_KP_1:return 0x4f;case SDL_SCANCODE_END:return 0xcf;case SDL_SCANCODE_PAUSE:return 0xc5;
    case SDL_SCANCODE_LEFT:return 0xcb;case SDL_SCANCODE_UP:return 0xc8;case SDL_SCANCODE_RIGHT:return 0xcd;case SDL_SCANCODE_DOWN:return 0xd0;
    default:return 0;
    }
}
}
int main(int argc,char** argv){
    try{
        if(argc<2){std::cerr<<"Usage: fsb_event0_sdl ASSETS [--intro|--no-intro] [--campaign|--through-event N] [--field-fixture MAP] [--speed 1|8|16] [--auto-dialogue|--manual-dialogue] [--render enhanced|original] [--renderer auto|gpu|software] [--window-size W,H (default800,600)] [--logical-size W,H (default from the image)] [--integer-scale] [--audio-backend auto|software] [--save-dir PATH] [--debug] [--debug-dump FILE.json] [--frame-log FILE.tsv] [--ignore-focus] [--mute] [--rgb8] [--headless] [--fast] [--auto-confirm-ms N | --inputs TRACE.tsv] [--limit-ms N] [--exit-at-end] [--verify-present] [--capture FILE.bmp] [--capture-presented FILE.bmp]\n";return 2;}
        bool software_audio=false,mute=false,headless=false,fast=false,exit_at_end=false,verify=false,vga6=true,integer_scale=false,debug_enabled=false,intro=false,reward_boost=false,ignore_focus=false,original_cheat_mode=false;
        auto mode=fsb::core::PresentationMode::enhanced;
        unsigned confirm=0,limit=0,through_event=0,field_map=0xffffffffu,window_width=0,window_height=0,speed=1,logical_width=0,logical_height=0;std::filesystem::path capture,input_path,capture_presented,save_directory;
        std::optional<bool> auto_dialogue;std::string renderer_mode="auto";
        std::filesystem::path debug_dump,frame_log;
        for(int i=2;i<argc;++i){
            const std::string arg=argv[i];
            if(arg=="--mute")mute=true;else if(arg=="--rgb8")vga6=false;else if(arg=="--headless")headless=true;else if(arg=="--fast")fast=true;
            else if(arg=="--campaign")through_event=fsb::core::Runtime::full_campaign;
            else if(arg=="--intro")intro=true;
            else if(arg=="--no-intro")intro=false;
            else if(arg=="--debug")debug_enabled=true;
            else if(arg=="--original-cheats"||arg=="/e2wantedcheatcode")original_cheat_mode=true;
            else if(arg=="--reward-boost")reward_boost=true;
            else if(arg=="--no-reward-boost")reward_boost=false;
            else if(arg=="--debug-dump"&&i+1<argc)debug_dump=std::filesystem::u8path(argv[++i]);
            else if(arg=="--frame-log"&&i+1<argc)frame_log=std::filesystem::u8path(argv[++i]);
            else if(arg=="--ignore-focus")ignore_focus=true;
            else if(arg=="--exit-at-end")exit_at_end=true;else if(arg=="--verify-present")verify=true;
            else if(arg=="--integer-scale")integer_scale=true;
            else if(arg=="--speed"&&i+1<argc)speed=unsigned(std::stoul(argv[++i]));
            else if(arg=="--audio-backend"&&i+1<argc){const std::string value=argv[++i];if(value=="software")software_audio=true;else if(value=="auto")software_audio=false;else throw std::runtime_error("--audio-backend needs auto or software");}
            else if(arg=="--renderer"&&i+1<argc){renderer_mode=argv[++i];if(renderer_mode!="auto"&&renderer_mode!="gpu"&&renderer_mode!="software")throw std::runtime_error("--renderer needs auto, gpu or software");}
            else if(arg=="--auto-dialogue")auto_dialogue=true;
            else if(arg=="--save-dir"&&i+1<argc)save_directory=std::filesystem::u8path(argv[++i]);
            else if(arg=="--manual-dialogue")auto_dialogue=false;
            else if(arg=="--render"&&i+1<argc){const std::string value=argv[++i];if(value=="original")mode=fsb::core::PresentationMode::original;else if(value=="enhanced")mode=fsb::core::PresentationMode::enhanced;else throw std::runtime_error("--render needs original or enhanced");}
            else if(arg=="--logical-size"&&i+1<argc){std::string value=argv[++i];std::replace(value.begin(),value.end(),',',' ');std::istringstream size(value);if(!(size>>logical_width>>logical_height)||!logical_width||!logical_height||logical_width>8192||logical_height>8192)throw std::runtime_error("logical size needs WIDTH,HEIGHT");}
            else if(arg=="--window-size"&&i+1<argc){std::string value=argv[++i];std::replace(value.begin(),value.end(),',',' ');std::istringstream size(value);if(!(size>>window_width>>window_height)||!window_width||!window_height||window_width>8192||window_height>8192)throw std::runtime_error("window size needs WIDTH,HEIGHT");}
            else if(arg=="--capture-presented"&&i+1<argc)capture_presented=argv[++i];
            else if((arg=="--auto-confirm-ms"||arg=="--limit-ms"||arg=="--capture"||arg=="--inputs"||arg=="--through-event"||arg=="--field-fixture")&&i+1<argc){
                const auto value=argv[++i];if(arg=="--capture")capture=std::filesystem::u8path(value);
                else if(arg=="--inputs")input_path=std::filesystem::u8path(value);
                else if(arg=="--through-event")through_event=unsigned(std::stoul(value));
                else if(arg=="--field-fixture")field_map=unsigned(std::stoul(value));
                else if(arg=="--auto-confirm-ms")confirm=unsigned(std::stoul(value));else limit=unsigned(std::stoul(value));
            }else throw std::runtime_error("unknown/incomplete host option: "+arg);
        }
        if(fast&&!mute)throw std::runtime_error("--fast requires --mute; the virtual clock is for renderer verification");
        if(fast&&speed!=1)throw std::runtime_error("--fast is unthrottled; use --speed without --fast for paced playback");
        if(verify&&!headless)throw std::runtime_error("--verify-present requires a headless window");
        if(confirm&&!input_path.empty())throw std::runtime_error("periodic input and an input trace are mutually exclusive");
        const auto replay=input_path.empty()?std::vector<fsb::lab::TimedInput>{}:fsb::lab::read_input_trace(input_path);
        fsb::core::PlaybackClock playback(speed);
        std::size_t replay_cursor=0;
        const auto assets=std::filesystem::u8path(argv[1]);
        // The image itself carries the resolution the original shipped with; the
        // hardcoded 640x480 was the port's, not the game's.
        const auto image=fsb::lab::read(assets/"FLYINGSB.EXE");
        if(!logical_width){const auto probe=fsb::core::Memory::from_pe32(image);
            logical_width=probe.read(fsb::core::globals::framebuffer_width);
            logical_height=probe.read(fsb::core::globals::framebuffer_height);}
        fsb::core::Runtime runtime(image,logical_width,logical_height);fsb::core::FontRaster fonts(runtime.memory);
        runtime.debug_rewards.enable(reward_boost);
        const auto calendar_now=std::time(nullptr);const auto* calendar=std::localtime(&calendar_now);
        fsb::original_cheats::configure(runtime.memory,original_cheat_mode,calendar?unsigned(calendar->tm_year+1900):0);
        if(field_map!=0xffffffffu)intro=false;
        fsb::lab::register_runtime_assets(runtime,fonts,assets,intro?std::max(9u,through_event):through_event);if(intro)runtime.start_intro(through_event);else if(field_map==0xffffffffu)runtime.start_event0(through_event);else runtime.start_field_fixture(field_map,through_event);
        // Text is drawn the way the original engine draws it: the embedded
        // bitmap glyphs, with no separately rasterized outline overlay. Only the
        // final composition is done at the display's pixel density.
        fonts.set_enhanced(false);
        sdl_check(SDL_Init(SDL_INIT_VIDEO));SdlLifetime lifetime;
        if(save_directory.empty()){char* folder=SDL_GetPrefPath("FSB","PortableCore");sdl_check(folder!=nullptr);save_directory=std::filesystem::u8path(folder);SDL_free(folder);}
        fsb::host::SaveDirectory saves(save_directory);saves.attach(runtime.field_menu);
        const auto saved_settings=saves.read("Settings.dat");
        bool settings_loaded=false;if(saved_settings)settings_loaded=runtime.field_menu.load_settings(*saved_settings);
        std::cout<<"save_directory="<<save_directory.string()<<" settings_loaded="<<settings_loaded<<'\n';
        auto attempted_settings=runtime.field_menu.settings();std::string settings_error;
        const auto persist_settings=[&]{const auto current=runtime.field_menu.settings();if(current!=attempted_settings){attempted_settings=current;if(saves.write("Settings.dat",current))settings_error.clear();else settings_error="Settings could not be written";}};
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Flying Super Board - Portable Core",window_width?int(window_width):(headless?640:800)+(debug_enabled?440:0),window_height?int(window_height):headless?480:600,SDL_WINDOW_HIGH_PIXEL_DENSITY|SDL_WINDOW_RESIZABLE|(headless?SDL_WINDOW_HIDDEN:SDL_WindowFlags(0))),SDL_DestroyWindow);
        sdl_check(bool(window));
        // With text input on, the platform keeps an input method session on the
        // window and every key reaches it first. The game commands are letters,
        // so under a Hangul input source pressing one starts a composition, and
        // the arrows that follow are taken as candidate navigation until it
        // commits: movement stops answering for a moment and then resumes. This
        // host reads raw key up/down and never SDL_EVENT_TEXT_INPUT, so the
        // session has nothing to do here.
        SDL_StopTextInput(window.get());
        auto renderer=fsb::host::create_renderer(window.get(),renderer_mode=="software"||(headless&&renderer_mode=="auto"),renderer_mode=="gpu");
        if(!headless)SDL_SetRenderVSync(renderer.get(),1);
        fsb::host::HostPresentation presentation(renderer.get());
        std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> debug_texture(nullptr,SDL_DestroyTexture);
        fsb::host::DebugPanel debug(fsb::lab::read(assets/"fonts/gulim.ttc"),save_directory/"debug");debug.show(debug_enabled);
        const auto debug_cp949=fsb::lab::read(assets/"fonts/cp949.bin");
        const fsb::core::ImageRgba* debug_frame=nullptr;int debug_left=0;
        unsigned debug_width=0,debug_height=0;std::uint64_t debug_revision=0;
        unsigned texture_width=0,texture_height=0;int output_width=0,output_height=0;
        fsb::core::ImageRgba captured_canvas;fsb::core::Rect destination{};
        std::unique_ptr<fsb::host::HostAudio> audio_output;
        // Honor an explicitly selected SDL audio driver, including dummy tests.
        if(!mute){audio_output=std::make_unique<fsb::host::HostAudio>(software_audio||SDL_getenv("SDL_AUDIODRIVER")!=nullptr);runtime.audio.set_output(audio_output.get());}
        unsigned presentations=0,verified=0,simulation_frames=0;bool quit=false,finished=false;
        std::uint64_t meter_time=SDL_GetTicks();unsigned meter_presentations=0,meter_frames=0;double presentation_hz=0,simulation_hz=0,simulation_cost=0,presentation_cost=0;bool meter_ready=false;
        const auto debug_snapshot=[&]{
            auto snapshot=fsb::core::inspect_runtime(runtime,debug_cp949);const auto now=SDL_GetTicks();
            if(now-meter_time>=500){meter_ready=true;presentation_hz=(presentations-meter_presentations)*1000.0/(now-meter_time);simulation_hz=(simulation_frames-meter_frames)*1000.0/(now-meter_time);meter_time=now;meter_presentations=presentations;meter_frames=simulation_frames;}
            const auto decimal=[](double value){std::ostringstream s;s<<std::fixed<<std::setprecision(1)<<value;return s.str();};
            auto& fields=snapshot.sections.front().fields;
            fields.push_back({"호스트 재생",std::to_string(playback.speed())+"x · 미처리 논리 시간 "+std::to_string(playback.pending())+" ms"});
            fields.push_back({"실제 시간당 프레임",meter_ready?"표시 "+decimal(presentation_hz)+" / 시뮬레이션 "+decimal(simulation_hz):"측정 중 (0.5초)"});
            fields.push_back({"최근 처리 시간","진행 "+decimal(simulation_cost)+" ms / 표시 "+decimal(presentation_cost)+" ms (대기 포함)"});
            fields.push_back({"출력 픽셀",std::to_string(output_width)+" x "+std::to_string(output_height)});
            fields.push_back({"음향 장치",audio_output?audio_output->name():"muted"});
            return snapshot;
        };
        // Frame pacing measurement. A sample is one completed presentation, so
        // `sim_frames` counts the guest frames it collapsed: 0 means the screen
        // was repainted without new content and 2+ means earlier guest frames
        // were never displayed.
        struct FrameSample{double wall_ms,interval_ms,sim_ms,compose_ms,swap_ms;unsigned sim_frames,logical_ms;};
        std::vector<FrameSample> samples;unsigned rendered_since_present=0,skipped_frames=0,redundant_presents=0;
        const auto counter_hz=double(SDL_GetPerformanceFrequency());const auto counter_origin=SDL_GetPerformanceCounter();std::uint64_t previous_sample=0;
        const auto canvas=[&](){
            if(output_width<1||output_height<1)throw std::runtime_error("no drawable frame to capture");
            fsb::core::ImageRgba image{unsigned(output_width),unsigned(output_height),std::vector<std::uint8_t>(std::size_t(output_width)*output_height*4)};
            for(std::size_t i=3;i<image.pixels.size();i+=4)image.pixels[i]=255;
            const auto presented=fsb::core::present_image(runtime.frame(),unsigned(destination.right-destination.left),unsigned(destination.bottom-destination.top),mode,vga6);
            for(unsigned y=0;y<presented.height;++y)std::copy_n(presented.pixels.begin()+std::size_t(y)*presented.width*4,presented.width*4,image.pixels.begin()+(std::size_t(y+destination.top)*image.width+destination.left)*4);
            if(debug_frame)for(unsigned y=0;y<debug_frame->height;++y)std::copy_n(debug_frame->pixels.begin()+std::size_t(y)*debug_frame->width*4,debug_frame->width*4,image.pixels.begin()+(std::size_t(y)*image.width+debug_left)*4);
            return image;
        };
        const auto present=[&]{
            const auto present_begin=SDL_GetPerformanceCounter();
            const auto& frame=runtime.frame();
            presentation.prepare_window();
            int drawable_width=0,drawable_height=0;sdl_check(SDL_GetRenderOutputSize(renderer.get(),&drawable_width,&drawable_height));if(drawable_width<1||drawable_height<1)return;
            output_width=drawable_width;output_height=drawable_height;
            int points_w=0,points_h=0;SDL_GetWindowSize(window.get(),&points_w,&points_h);
            const double dpi=double(output_width)/std::max(1,points_w);const auto panel_width=int(std::lround(debug.width_points(points_w)*dpi));debug_left=output_width-panel_width;
            destination=fsb::core::fit_presentation(frame.width,frame.height,unsigned(debug_left),unsigned(output_height),integer_scale);
            // Both copies are opaque and the panel covers exactly what the frame
            // does not, so clearing first only repaints pixels they overwrite.
            // Keep the clear whenever the fit leaves a letterbox band exposed.
            const bool covers_output=destination.left<=0&&destination.top<=0&&destination.right>=debug_left&&destination.bottom>=output_height;
            if(!covers_output){sdl_check(SDL_SetRenderDrawColor(renderer.get(),0,0,0,255));sdl_check(SDL_RenderClear(renderer.get()));}
            presentation.draw(frame,destination,mode,vga6);
            texture_width=presentation.texture_width();texture_height=presentation.texture_height();
            debug_frame=nullptr;
            if(panel_width){
                debug.reward_boost(runtime.debug_rewards.enabled());
                if(debug.wants_snapshot(SDL_GetTicks()))debug.update(debug_snapshot(),SDL_GetTicks());
                debug_frame=&debug.image(unsigned(panel_width),unsigned(output_height),dpi);
                if(!debug_texture||debug_width!=debug_frame->width||debug_height!=debug_frame->height){debug_texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STREAMING,int(debug_frame->width),int(debug_frame->height)));sdl_check(bool(debug_texture));debug_width=debug_frame->width;debug_height=debug_frame->height;debug_revision=0;sdl_check(SDL_SetTextureBlendMode(debug_texture.get(),SDL_BLENDMODE_NONE));sdl_check(SDL_SetTextureScaleMode(debug_texture.get(),SDL_SCALEMODE_NEAREST));}
                if(debug_revision!=debug.revision()){sdl_check(SDL_UpdateTexture(debug_texture.get(),nullptr,debug_frame->pixels.data(),int(debug_frame->width)*4));debug_revision=debug.revision();}const SDL_FRect panel{float(debug_left),0,float(panel_width),float(output_height)};sdl_check(SDL_RenderTexture(renderer.get(),debug_texture.get(),nullptr,&panel));
            }
            if((verify&&presentations%127==0)||!capture_presented.empty()){
                auto actual=fsb::host::read_renderer(renderer.get());
                if(verify&&presentations%127==0){
                    const auto expected=canvas();
                    if(actual.width!=expected.width||actual.height!=expected.height||actual.pixels!=expected.pixels)throw std::runtime_error("SDL framebuffer readback differs from CPU presentation reference");
                    ++verified;
                }
                if(!capture_presented.empty())captured_canvas=std::move(actual);
            }
            const auto swap_begin=SDL_GetPerformanceCounter();
            SDL_RenderPresent(renderer.get());++presentations;const auto present_end=SDL_GetPerformanceCounter();presentation_cost=(present_end-present_begin)*1000.0/counter_hz;
            if(!frame_log.empty()){
                if(rendered_since_present>1)skipped_frames+=rendered_since_present-1;else if(!rendered_since_present)++redundant_presents;
                samples.push_back({(present_end-counter_origin)*1000.0/counter_hz,previous_sample?(present_end-previous_sample)*1000.0/counter_hz:0.0,simulation_cost,(swap_begin-present_begin)*1000.0/counter_hz,(present_end-swap_begin)*1000.0/counter_hz,rendered_since_present,playback.now()});
                previous_sample=present_end;rendered_since_present=0;
            }
        };
        // The present gate is there to bound composition cost, not to pace the
        // display. Fixed at16ms on a faster panel a finished guest frame waits
        // for a deadline the display has already passed twice, so take whichever
        // of the two is shorter and never a slower one than before.
        unsigned present_interval=16;
        const auto display_mode=[&]{return SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window.get()));};
        const auto measure_present_interval=[&]{
            const auto* display=display_mode();
            present_interval=display&&display->refresh_rate>0?std::min(16u,std::max(1u,unsigned(1000/display->refresh_rate))):16u;
        };
        measure_present_interval();
        present();const auto origin=SDL_GetTicks();auto previous_real=origin,last_present=origin;
        const auto pointer_message=[&](unsigned button,bool down,double point_x,double point_y){
            int w=0,h=0;SDL_GetWindowSize(window.get(),&w,&h);
            // Use the rectangle actually presented in drawable pixels. Fitting
            // again in window points disagrees with Retina integer scaling.
            const int x=int(std::floor((point_x*output_width/std::max(1,w)-destination.left)*runtime.frame().width/std::max(1,destination.right-destination.left)));
            const int y=int(std::floor((point_y*output_height/std::max(1,h)-destination.top)*runtime.frame().height/std::max(1,destination.bottom-destination.top)));
            return fsb::core::pointer_button_message(button,down,x,y);
        };
        unsigned next_confirm=confirm,next_release=0;std::optional<SDL_FingerID> finger;
        bool focused=true,initial_tick=true,dirty=false;std::string shown_save_error;
        const auto pump_audio=[&](bool active,bool stalled){if(audio_output)audio_output->pump(active,stalled);};
        const auto automate=[&](){return !confirm&&input_path.empty()&&auto_dialogue.value_or(playback.speed()>1);};
        const auto title=[&](){const auto label=runtime.intro_active()?std::string("Flying Super Board"):std::string("Flying Super Board - ")+(finished?"Event "+std::to_string(through_event)+" finished | ":"")+(fast?"Unthrottled":std::to_string(playback.speed())+"x")+(automate()?" | Auto dialogue":"");SDL_SetWindowTitle(window.get(),label.c_str());};
        title();
        std::cout<<(input_path.empty()?"Arrows: move/select. Enter/X: confirm. Z/Escape: field/battle menu or cancel. F2/F3/F4/F5: save/load/settings/party. Ctrl/Cmd+Q: quit.\n":"Replaying input trace. Ctrl/Cmd+Q: quit.\n");
        std::cout<<"Hold `: 16x; release: previous speed. F6: 1x. F7: 8x. F8: 16x. F9: auto dialogue on/off. Choices require manual input.\n";
        std::cout<<"Tab: debug panel. Snapshots: "<<(save_directory/"debug").string()<<'\n';
        while(!quit){
            SDL_Event event;
            while(SDL_PollEvent(&event)){
                if(event.type==SDL_EVENT_QUIT){quit=true;break;}
                // SDL3 owns the dropped path for the duration of the event.
                if(event.type==SDL_EVENT_DROP_FILE)continue;
                if(event.type==SDL_EVENT_RENDER_DEVICE_RESET||event.type==SDL_EVENT_RENDER_TARGETS_RESET){presentation.reset();debug_texture.reset();present();}
                int debug_points_w=0,debug_points_h=0;SDL_GetWindowSize(window.get(),&debug_points_w,&debug_points_h);const bool debug_was_visible=debug.visible();
                if(debug.handle(event,debug_points_w,debug_points_h)){
                    if(const auto value=debug.take_reward_boost_request())runtime.debug_rewards.enable(*value);
                    if(debug_was_visible!=debug.visible()){meter_time=SDL_GetTicks();meter_presentations=presentations;meter_frames=simulation_frames;meter_ready=false;if(!headless)SDL_SetWindowSize(window.get(),std::max(640,debug_points_w+(debug.visible()?440:-440)),debug_points_h);}
                    present();continue;
                }
                if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST&&playback.fast_forward_held()){playback.set_fast_forward_held(false);previous_real=SDL_GetTicks();title();}
                // Deactivation stops frame dispatch in the original engine.
                // --ignore-focus withholds the notification so an unattended
                // measurement run is not paused by the window manager; it
                // does not change what the runtime does when told it is idle.
                if((event.type==SDL_EVENT_WINDOW_FOCUS_LOST||event.type==SDL_EVENT_WINDOW_FOCUS_GAINED)&&!headless&&!ignore_focus){focused=event.type==SDL_EVENT_WINDOW_FOCUS_GAINED;runtime.set_active(focused);playback.elapse(0,false);previous_real=SDL_GetTicks();}
                // Dragging onto another monitor changes the refresh the gate
                // is derived from, and MOVED is the only notice of that.
                if(event.type==SDL_EVENT_WINDOW_DISPLAY_CHANGED||event.type==SDL_EVENT_WINDOW_MOVED)measure_present_interval();
                if(event.type==SDL_EVENT_WINDOW_EXPOSED||event.type==SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED||event.type==SDL_EVENT_WINDOW_RESIZED||event.type==SDL_EVENT_WINDOW_DISPLAY_CHANGED||event.type==SDL_EVENT_WINDOW_MOVED)present();
                // The GAME OVER splash is dismissed by any key inside the core
                // title flow, so Esc there belongs to the game, not to the host.
                if(event.type==SDL_EVENT_KEY_DOWN&&((event.key.key==SDLK_ESCAPE&&runtime.finished())||(event.key.key==SDLK_Q&&(event.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_GUI))))){quit=true;break;}
                if(!fast&&(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP)&&(event.key.scancode==SDL_SCANCODE_GRAVE||event.key.key==SDLK_GRAVE)){
                    // Physical scancode also covers the same key in layouts
                    // where the printed character changes (for example Korean).
                    if(event.type==SDL_EVENT_KEY_UP){if(playback.fast_forward_held()){playback.set_fast_forward_held(false);previous_real=SDL_GetTicks();title();}}
                    else if(!event.key.repeat&&focused&&!playback.fast_forward_held()&&!(event.key.mod&(SDL_KMOD_SHIFT|SDL_KMOD_CTRL|SDL_KMOD_ALT|SDL_KMOD_GUI))){playback.set_fast_forward_held(true);previous_real=SDL_GetTicks();title();}
                    continue;
                }
                if(event.type==SDL_EVENT_KEY_DOWN&&!event.key.repeat){
                    const auto key=event.key.key;
                    if(!fast&&(key==SDLK_F6||key==SDLK_F7||key==SDLK_F8)){playback.set_speed(key==SDLK_F6?1:key==SDLK_F7?8:16);previous_real=SDL_GetTicks();title();continue;}
                    if(key==SDLK_F9&&input_path.empty()&&!confirm){auto_dialogue=!automate();title();continue;}
                }
                if(!input_path.empty())continue;
                if(event.type==SDL_EVENT_KEY_DOWN||event.type==SDL_EVENT_KEY_UP){const auto key=legacy_key(event.key.key);const auto mod=event.key.mod;
                    if(key)runtime.post_input(fsb::core::keyboard_message(key,pc_scancode(event.key.scancode),event.type==SDL_EVENT_KEY_DOWN,(mod&SDL_KMOD_SHIFT)!=0,(mod&SDL_KMOD_CTRL)!=0,(mod&SDL_KMOD_ALT)!=0,event.key.repeat));
                }
                if((event.type==SDL_EVENT_MOUSE_BUTTON_DOWN||event.type==SDL_EVENT_MOUSE_BUTTON_UP)&&event.button.which!=SDL_TOUCH_MOUSEID&&(event.button.button==SDL_BUTTON_LEFT||event.button.button==SDL_BUTTON_RIGHT))
                    runtime.post_input(pointer_message(event.button.button==SDL_BUTTON_RIGHT,event.type==SDL_EVENT_MOUSE_BUTTON_DOWN,event.button.x,event.button.y));
                if((event.type==SDL_EVENT_FINGER_DOWN&&!finger)||(event.type==SDL_EVENT_FINGER_UP&&finger&&*finger==event.tfinger.fingerID)){
                    const bool down=event.type==SDL_EVENT_FINGER_DOWN;if(down)finger=event.tfinger.fingerID;else finger.reset();
                    int w=0,h=0;SDL_GetWindowSize(window.get(),&w,&h);runtime.post_input(pointer_message(0,down,event.tfinger.x*w,event.tfinger.y*h));
                }
            }
            if(quit)break;
            if(finished){pump_audio(focused,false);SDL_Delay(10);continue;}
            if(!runtime.field_menu.active())persist_settings();
            const auto file_error=runtime.field_menu.last_error().empty()?settings_error:runtime.field_menu.last_error();
            if(!file_error.empty()&&file_error!=shown_save_error){shown_save_error=file_error;std::cerr<<"Save/load: "<<shown_save_error<<'\n';if(!headless)SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"FSB save/load",shown_save_error.c_str(),window.get());}
            const auto real=SDL_GetTicks(),elapsed=real-previous_real;previous_real=real;
            const bool running=focused&&(runtime.memory.read(fsb::core::globals::runtime_mode_flags)&3)==1;
            playback.elapse(fast?16:elapsed,running);runtime.dialogue.set_auto_advance(automate());
            // Process pause/unpause input without accumulating simulation time.
            if(!running&&!initial_tick){const auto step=runtime.advance(playback.now(),false);dirty|=step.rendered;if(step.rendered){++simulation_frames;++rendered_since_present;}}
            const auto slice_begin=SDL_GetPerformanceCounter();unsigned consumed=0;
            while(initial_tick||playback.pending()){
                if(initial_tick)initial_tick=false;else if(!playback.step())break;
                const auto now=playback.now();
                if(confirm&&now>=next_confirm){runtime.post_input(fsb::core::keyboard_message(13,0x1c,true));next_confirm+=confirm;next_release=now+48;}
                if(next_release&&now>=next_release){runtime.post_input(fsb::core::keyboard_message(13,0x1c,false));next_release=0;}
                while(replay_cursor<replay.size()&&replay[replay_cursor].ms<=now)runtime.post_input(replay[replay_cursor++].message);
                const bool was_intro=runtime.intro_active();const auto step=runtime.advance(now,false);dirty|=step.rendered;if(step.rendered){++simulation_frames;++rendered_since_present;}if(was_intro!=runtime.intro_active())title();
                if(runtime.quit_requested()){quit=true;break;}
                if(step.run_finished){finished=true;title();if(exit_at_end)quit=true;break;}
                if(limit&&now>=limit){quit=true;break;}
                if((runtime.memory.read(fsb::core::globals::runtime_mode_flags)&3)!=1){playback.elapse(0,false);break;}
                // Return to event polling regularly even at16x or under load.
                if(++consumed>=256||(step.rendered&&(SDL_GetPerformanceCounter()-slice_begin)*1000/SDL_GetPerformanceFrequency()>=8))break;
            }
            simulation_cost=(SDL_GetPerformanceCounter()-slice_begin)*1000.0/SDL_GetPerformanceFrequency();
            pump_audio(running&&!fast,elapsed>1000);
            const auto present_time=SDL_GetTicks();
            // Stamp the interval before composing. Stamping afterwards adds the
            // composition cost to every gap, so the16ms gate became16ms plus a
            // full compose and each displayed frame swallowed one or two guest
            // frames in turn.
            if(dirty&&(fast||quit||finished||present_time-last_present>=present_interval)){last_present=present_time;present();dirty=false;}
            // Nothing is owed to the simulation here, so the only reasons to wake
            // are the next input or the next gate opening. Sleeping through the
            // whole slice postpones a key by a full poll cycle; waiting on the
            // queue returns at once. A null event leaves it for the poll above.
            if(!fast&&!playback.pending())SDL_WaitEventTimeout(nullptr,running?1:10);
        }
        persist_settings();
        if(!debug_dump.empty()){std::ofstream out(debug_dump,std::ios::binary);out<<fsb::core::debug_json(debug_snapshot());out.close();if(!out)throw std::runtime_error("debug snapshot could not be written: "+debug_dump.string());}
        if(!capture.empty()){
            auto image=runtime.frame();if(vga6)for(auto& color:image.palette){color.r=std::uint8_t(((color.r>>2)<<2)|(color.r>>6));color.g=std::uint8_t(((color.g>>2)<<2)|(color.g>>6));color.b=std::uint8_t(((color.b>>2)<<2)|(color.b>>6));}
            fsb::lab::bmp(image,capture);
        }
        if(!capture_presented.empty())fsb::lab::bmp(captured_canvas,capture_presented);
        if(!frame_log.empty()){
            std::ofstream log(frame_log,std::ios::binary);log<<"wall_ms\tinterval_ms\tsim_ms\tcompose_ms\tswap_ms\tsim_frames\tlogical_ms\n";
            log<<std::fixed<<std::setprecision(3);
            for(const auto& s:samples)log<<s.wall_ms<<'\t'<<s.interval_ms<<'\t'<<s.sim_ms<<'\t'<<s.compose_ms<<'\t'<<s.swap_ms<<'\t'<<s.sim_frames<<'\t'<<s.logical_ms<<'\n';
            log.close();if(!log)throw std::runtime_error("frame log could not be written: "+frame_log.string());
            const auto* display=display_mode();const bool have_mode=display!=nullptr;
            const char* renderer_name=SDL_GetRendererName(renderer.get());
            int vsync=0;const bool have_vsync=SDL_GetRenderVSync(renderer.get(),&vsync);
            const double refresh_ms=have_mode&&display->refresh_rate>0?1000.0/display->refresh_rate:0.0;
            // The host repaints at most once per gate, so that gate — not the
            // panel — is the cadence a smooth run is expected to hold.
            const double target_ms=present_interval;
            std::vector<double> intervals;for(std::size_t i=1;i<samples.size();++i)intervals.push_back(samples[i].interval_ms);
            const auto share=[&](double bound){return double(std::count_if(intervals.begin(),intervals.end(),[&](double v){return v>bound;}))/std::max<std::size_t>(1,intervals.size());};
            const double jank=share(target_ms*1.5),severe=share(target_ms*2.0);
            unsigned hitches=0;for(std::size_t i=1;i<intervals.size();++i)if(intervals[i]>intervals[i-1]*2.0&&intervals[i]>target_ms*1.5)++hitches;
            auto sorted=intervals;std::sort(sorted.begin(),sorted.end());
            const auto quantile=[&](double q){return sorted.empty()?0.0:sorted[std::min(sorted.size()-1,std::size_t(q*sorted.size()))];};
            const double mean=sorted.empty()?0.0:std::accumulate(sorted.begin(),sorted.end(),0.0)/sorted.size();
            const double percent=100.0/std::max(1u,simulation_frames);
            std::cout<<std::fixed<<std::setprecision(2);
            std::cout<<"frame_log="<<frame_log.string()<<" renderer="<<(renderer_name?renderer_name:"?")<<" vsync="<<(have_vsync&&vsync!=SDL_RENDERER_VSYNC_DISABLED?1:0)<<" display_hz="<<(have_mode?display->refresh_rate:0.0f)<<" refresh_ms="<<refresh_ms<<" target_ms="<<target_ms<<'\n';
            std::cout<<"frame_pacing presents="<<presentations<<" simulation_frames="<<simulation_frames<<" skipped_frames="<<skipped_frames<<" skip_rate="<<skipped_frames*percent<<"% redundant_presents="<<redundant_presents<<'\n';
            std::cout<<"frame_interval_ms mean="<<mean<<" p50="<<quantile(0.5)<<" p95="<<quantile(0.95)<<" p99="<<quantile(0.99)<<" max="<<(sorted.empty()?0.0:sorted.back())<<'\n';
            std::cout<<"frame_jank over_1.5x="<<jank*100<<"% over_2x="<<severe*100<<"% hitches="<<hitches<<" intervals="<<intervals.size()<<'\n';
            const auto median=[&](auto pick){std::vector<double> v;for(const auto& s:samples)v.push_back(pick(s));std::sort(v.begin(),v.end());return v.empty()?0.0:v[v.size()/2];};
            std::cout<<"frame_cost_ms compose_p50="<<median([](const FrameSample& s){return s.compose_ms;})<<" swap_p50="<<median([](const FrameSample& s){return s.swap_ms;})<<" sim_p50="<<median([](const FrameSample& s){return s.sim_ms;})<<'\n';
        }
        int points_w=0,points_h=0;SDL_GetWindowSize(window.get(),&points_w,&points_h);
        std::cout<<"render="<<(mode==fsb::core::PresentationMode::enhanced?"enhanced":"original")<<" window_points="<<points_w<<'x'<<points_h<<" drawable_pixels="<<output_width<<'x'<<output_height<<" content_pixels="<<(destination.right-destination.left)<<'x'<<(destination.bottom-destination.top)<<" scaling="<<(presentation.gpu_scaling()?"gpu":"cpu")<<" texture_pixels="<<texture_width<<'x'<<texture_height<<'\n';
        std::cout<<"speed="<<playback.speed()<<" unthrottled="<<fast<<" auto_dialogue="<<automate()<<" logical_ms="<<playback.now()<<" wall_ms="<<SDL_GetTicks()-origin<<" simulation_frames="<<simulation_frames<<'\n';
        std::cout<<"audio_backend="<<(audio_output?audio_output->name():"muted")<<" software_audio_frames="<<(audio_output?audio_output->software_frames():0)<<" logical_audio_frames="<<runtime.audio.output_sample()<<" audio_clock=device\n";
        std::cout<<"run_completed="<<runtime.finished()<<" through_event="<<through_event<<" event0_script_completed="<<runtime.event0_finished()<<" presentations="<<presentations<<" verified_readbacks="<<verified<<" replayed_input_events="<<replay_cursor<<" root_pc=0x"<<std::hex<<runtime.root_pc()<<"\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
