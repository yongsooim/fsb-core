#pragma once
#include "fsb_core/runtime.hpp"
#include "fsb_core/font_raster.hpp"
#include "sprite_assets.hpp"
#include "host_time.hpp"
#include <sstream>

namespace fsb::lab {
inline void register_runtime_assets(core::Runtime& runtime,core::FontRaster& fonts,const std::filesystem::path& assets,unsigned through_event=0){
    if(!runtime.environment.local_time)runtime.environment.local_time=fsb::host::local_time;
    register_sprite_assets(runtime.sprites,assets);
    runtime.graphics.load_skin(read(assets/"pcxset/WHDLGBOX.pcx"));runtime.graphics.load_arrows(read(assets/"pcxset/WHARROW.pcx"));
    fonts.load(read(assets/"fonts/gulim.ttc"),read(assets/"fonts/batang.ttc"),read(assets/"fonts/cp949.bin"));
    runtime.graphics.glyph_renderer=[&fonts](core::Image8& image,const core::DialogGlyph& glyph){fonts.draw(image,glyph);};
    runtime.graphics.glyph_advance=[&fonts](unsigned font,std::uint16_t cp){return fonts.advance(font,cp);};
    std::map<unsigned,std::filesystem::path> map_paths{{469,assets/"MAPSET"}};
    for(auto id:{8u,49u,50u})runtime.audio.register_wave(true,id,read(assets/"audio"/core::Audio::resource_name(runtime.memory,true,id)));
    for(auto id:{90u,119u,120u,121u,122u,123u,124u,125u,126u}){
        auto path=assets/"se_event"/core::Audio::resource_name(runtime.memory,false,id);path.replace_extension(".wav");runtime.audio.register_wave(false,id,read(path));
    }
    if(through_event>=2){
      for(const auto name: {"sequence-assets.tsv","field-assets.tsv","campaign-assets.tsv"}){
        if(std::string(name)=="field-assets.tsv"&&through_event<9)continue;
        if(std::string(name)=="campaign-assets.tsv"&&through_event<18)continue;
        std::ifstream catalog(assets/name);
        if(!catalog)throw std::runtime_error(std::string("missing prepared asset catalog: ")+name);
        std::string line;
        while(std::getline(catalog,line)){
            if(line.empty())continue;
            std::istringstream record(line);std::string kind,id_text,path;
            if(!std::getline(record,kind,'\t')||!std::getline(record,id_text,'\t')||!std::getline(record,path))throw std::runtime_error("invalid sequence asset catalog row");
            const auto id=unsigned(std::stoul(id_text));
            if(kind=="map"){map_paths[id]=assets/path;}else if(kind=="bgm"||kind=="cue")runtime.audio.register_wave(kind=="bgm",id,read(assets/path));
            else throw std::runtime_error("unknown sequence asset catalog kind");
        }
      }
    }
    runtime.map.asset_provider=[map_paths=std::move(map_paths),&runtime](unsigned id){
        const auto found=map_paths.find(id);if(found==map_paths.end())throw core::Fault(id,"map bundle has no prepared catalog binding");
        core::MapAssets data;const auto names=core::Map::asset_names(runtime.memory,id);for(unsigned i=0;i<6;++i)data.files[i]=read(found->second/names[i]);return data;
    };

}
} // namespace fsb::lab
