#include "debug_panel.hpp"
#include "lab_io.hpp"
#include <iostream>
namespace {void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
int main(int argc,char** argv){try{
    if(argc!=3)return 2;const std::filesystem::path assets=argv[1],out=argv[2];std::filesystem::create_directories(out);
    check(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    {fsb::host::DebugPanel panel(fsb::lab::read(assets/"fonts/gulim.ttc"),out/"snapshots");
    SDL_Event e{};e.type=SDL_EVENT_KEY_DOWN;e.key.key=SDLK_TAB;
    check(panel.handle(e,1000,720)&&panel.visible(),"Tab did not open debug panel");e.key.repeat=true;panel.handle(e,1000,720);check(panel.visible(),"key repeat toggled panel");
    fsb::core::DebugSnapshot sample{1000,{{"overview","상태",{{"맵","다섯손가락마을"},{"인물","미로 · HP 25/25"}}},{"party","파티",{}},{"events","이벤트",{}},{"logs","진단",{}}}};
    panel.update(sample,100);check(!panel.wants_snapshot(150)&&panel.wants_snapshot(200),"live sampling was not limited to10Hz");
    const auto click=[&](int x,int y){SDL_Event mouse{};mouse.type=SDL_EVENT_MOUSE_BUTTON_DOWN;mouse.button.button=SDL_BUTTON_LEFT;mouse.button.x=float(x);mouse.button.y=float(y);check(panel.handle(mouse,1000,720),"panel click escaped into game");mouse.type=SDL_EVENT_MOUSE_BUTTON_UP;mouse.button.x=50;check(panel.handle(mouse,1000,720),"panel mouse release escaped into game");};
    click(590,100);sample.frame_ms=2000;panel.update(sample,400);check(panel.snapshot().frame_ms==1000&&!panel.wants_snapshot(900),"display freeze did not retain inspected state");
    panel.reward_boost(true);click(700,145);check(panel.take_reward_boost_request()==std::optional<bool>(false),"reward OFF control must emit an explicit request while snapshot is frozen");check(!panel.take_reward_boost_request(),"reward request must be consumed once");panel.reward_boost(false);click(700,145);check(panel.take_reward_boost_request()==std::optional<bool>(true),"reward ON control must be reversible");panel.reward_boost(true);
    click(730,100);char* clipboard=SDL_GetClipboardText();check(clipboard&&std::string(clipboard).find("fsb-debug-v1")!=std::string::npos,"snapshot clipboard copy failed");SDL_free(clipboard);
    click(870,100);unsigned saves=0;for(const auto& file:std::filesystem::directory_iterator(out/"snapshots"))if(file.path().extension()==".json")++saves;check(saves>0,"snapshot export did not write JSON");
    click(590,100);panel.update(sample,500);check(panel.snapshot().frame_ms==2000,"unfreeze did not resume observations");
    fsb::lab::bmp(panel.image(440,720,1),out/"panel.bmp");fsb::lab::bmp(panel.image(880,1440,2),out/"panel-retina.bmp");
    e={};e.type=SDL_EVENT_MOUSE_BUTTON_UP;e.button.button=SDL_BUTTON_LEFT;e.button.x=900;check(!panel.handle(e,1000,720),"game-origin mouse release was swallowed by panel");
    e={};e.type=SDL_EVENT_FINGER_DOWN;e.tfinger.fingerID=7;e.tfinger.x=.9f;check(panel.handle(e,1000,720),"debug touch escaped into game");e.type=SDL_EVENT_FINGER_UP;e.tfinger.x=.1f;check(panel.handle(e,1000,720),"debug-origin touch release escaped");
    e={};e.type=SDL_EVENT_FINGER_UP;e.tfinger.fingerID=8;e.tfinger.x=.9f;check(!panel.handle(e,1000,720),"game-origin touch release was swallowed");
    panel.show(false);check(panel.width_points(1000)==0&&!panel.wants_snapshot(1000),"hidden panel still requested inspection");}
    SDL_Quit();std::cout<<"debug panel: input isolation, freeze, clipboard, export and HiDPI passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}
