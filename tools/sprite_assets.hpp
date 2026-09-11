#pragma once
#include "fsb_core/sprites.hpp"
#include "lab_io.hpp"

namespace fsb::lab {
inline void register_sprite_assets(core::Sprites& sprites,const std::filesystem::path& assets) {
    for (const auto& bank : {std::pair{"ASE_PS","!"},std::pair{"ASE_FM","@"},std::pair{"pcxset","/"}})
        for (const auto& entry : std::filesystem::directory_iterator(assets/bank.first))
            if(entry.is_regular_file() && (entry.path().extension()==".pcx"||entry.path().extension()==".bmp")) sprites.register_image(bank.second+entry.path().filename().string(),read(entry.path()));
}
} // namespace fsb::lab
