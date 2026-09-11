#include "fsb_core/recovered_battle.hpp"
#include "fsb_core/effect_script.hpp"
#include "fsb_core/original_audio.hpp"

namespace fsb::core {
bool RecoveredBattle::dispatch_effect_script(Address entry) {
    if((entry<0x447841||entry>0x447df9)&&entry!=0x461d25)return false;
    EffectScript standalone(memory_);
    // Legacy users can supply the existing service boundary without owning a
    // Runtime. Preserve that contract while the product Runtime calls Audio directly.
    standalone.play_cue=[this](unsigned cue){callback(original_audio::play_cue,{cue});};
    standalone.stop_cue=[this](unsigned cue){callback(original_audio::stop_cue,{cue});};
    standalone.invoke_actor=[this](Address entry,Address actor){callback(entry,{actor});};
    auto& effects=effect_scripts?*effect_scripts:standalone;
    using effect_script::Command;
    const auto execute=[&](Command command){effects.execute(argument(0),command);result(0,4);return true;};
    switch(entry) {
    case 0x447841:effects.start(argument(0),argument(1));result(1,8);return true;
    case 0x447898:effects.stop(argument(0));result(1,4);return true;
    case 0x4478ba:effects.tick();result(0);return true;
    case 0x447925:return execute(Command::Stop);
    case 0x44793b:return execute(Command::Restart);
    case 0x447949:return execute(Command::SaveAppearance);
    case 0x44798f:return execute(Command::RestoreAppearance);
    case 0x4479d5:return execute(Command::SetCharacterAnimation);
    case 0x447a2e:return execute(Command::SetIndexedAnimation);
    case 0x447a8d:return execute(Command::SetEffectAnimation);
    case 0x447aec:return execute(Command::SetFrame);
    case 0x447b3b:return execute(Command::Wait);
    case 0x447b84:return execute(Command::SaveTransform);
    case 0x447bba:return execute(Command::RestoreTransform);
    case 0x447bf0:return execute(Command::TranslatePosition);
    case 0x447c54:return execute(Command::TranslateDrawOffset);
    case 0x447cb8:return execute(Command::AdvanceController);
    case 0x447ce5:return execute(Command::NotifyController);
    case 0x447d23:return execute(Command::SetFlags);
    case 0x447d50:return execute(Command::ClearFlags);
    case 0x447d7f:return execute(Command::PlayCue);
    case 0x447dbc:return execute(Command::StopCue);
    case 0x447df9:return execute(Command::SetTimingMode);
    case 0x461d25:result(effects.notify_controller(argument(0),signed32(argument(1))),8);return true;
    default:return false;
    }
}
}
