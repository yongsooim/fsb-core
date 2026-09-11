#pragma once
#include "fsb_core/raster.hpp"
#include "fsb_core/presentation.hpp"
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace fsb::lab {
// The original executable names its resources in mixed case while the files
// came off the disc in upper case, which only agrees on a case-insensitive
// filesystem. Windows and macOS hide that; emscripten's MEMFS and Linux do not,
// and 34 of the catalogued BGM paths differ from the file by case alone.
inline std::filesystem::path resolve_case(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error)) return path;
    const auto folder = path.parent_path();
    if (folder.empty() || !std::filesystem::is_directory(folder, error)) return path;
    const auto lower = [](std::string text) {
        for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    const auto wanted = lower(path.filename().string());
    for (const auto& entry : std::filesystem::directory_iterator(folder, error))
        if (lower(entry.path().filename().string()) == wanted) return entry.path();
    return path;
}
inline std::vector<std::uint8_t> read(const std::filesystem::path& requested) {
    const auto path = resolve_case(requested);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open " + path.string());
    const auto size = file.tellg(); file.seekg(0);
    if (size < 0) throw std::runtime_error("cannot size " + path.string());
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) throw std::runtime_error("cannot read " + path.string());
    return data;
}
// Isolated replay fixtures may carry money/items from an actual shop replay.
// This copies no HP, enemy state, event-completion flags or live object state.
inline void restore_inventory_fixture(core::Memory& memory,const std::filesystem::path& path){
    const auto bytes=read(path);std::size_t cursor=8;
    const auto word=[&](){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(cursor++))<<(i*8);return value;};
    if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!="FSBDRAW1")throw std::runtime_error("invalid inventory snapshot");
    const auto count=word();bool found=false;
    for(unsigned i=0;i<count;++i){const auto base=word(),size=word();if(cursor+size>bytes.size())throw std::runtime_error("truncated inventory snapshot");
        if(base<=0x803a18&&std::uint64_t(base)+size>=0x8073d8){
            for(auto [start,length]:{std::pair{0x803a18u,4u},std::pair{0x806e30u,362u*4}})for(unsigned b=0;b<length;++b)memory.write(start+b,bytes[cursor+start-base+b],1);found=true;
        }cursor+=size;
    }
    if(!found)throw std::runtime_error("snapshot lacks original money/item tables");
}
inline void bmp(const core::Image8& image, const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot write " + path.string());
    const auto u16 = [&](unsigned v) { file.put(static_cast<char>(v)); file.put(static_cast<char>(v >> 8)); };
    const auto u32 = [&](unsigned v) { for (unsigned i = 0; i < 4; ++i) file.put(static_cast<char>(v >> (i * 8))); };
    const auto pitch = (image.width + 3) & ~3u;
    file.write("BM", 2); u32(1078 + pitch * image.height); u32(0); u32(1078);
    u32(40); u32(image.width); u32(image.height); u16(1); u16(8); u32(0); u32(pitch * image.height);
    u32(2835); u32(2835); u32(256); u32(256);
    for (auto color : image.palette) u32((unsigned(color.r) << 16) | (unsigned(color.g) << 8) | color.b);
    for (unsigned row = image.height; row-- > 0;) {
        file.write(reinterpret_cast<const char*>(image.pixels.data() + row * image.width), image.width);
        for (auto x = image.width; x < pitch; ++x) file.put(0);
    }
    if (!file) throw std::runtime_error("incomplete BMP output " + path.string());
}
inline void bmp(const core::ImageRgba& image,const std::filesystem::path& path){
    std::ofstream file(path,std::ios::binary);if(!file)throw std::runtime_error("cannot write RGBA BMP");
    const auto u16=[&](unsigned v){file.put(char(v));file.put(char(v>>8));};const auto u32=[&](unsigned v){for(unsigned i=0;i<4;++i)file.put(char(v>>(i*8)));};
    file.write("BM",2);u32(54+unsigned(image.pixels.size()));u32(0);u32(54);u32(40);u32(image.width);u32(image.height);u16(1);u16(32);u32(0);u32(unsigned(image.pixels.size()));u32(2835);u32(2835);u32(0);u32(0);
    for(unsigned y=image.height;y-->0;)for(unsigned x=0;x<image.width;++x){const auto at=(std::size_t(y)*image.width+x)*4;file.put(char(image.pixels[at+2]));file.put(char(image.pixels[at+1]));file.put(char(image.pixels[at]));file.put(char(image.pixels[at+3]));}
    if(!file)throw std::runtime_error("incomplete RGBA BMP");
}
inline void guest_snapshot(const core::Memory& memory,const std::filesystem::path& path){
    std::ofstream file(path,std::ios::binary);if(!file)throw std::runtime_error("cannot write guest snapshot");
    const auto word=[&](std::uint32_t value){for(unsigned i=0;i<4;++i)file.put(char(value>>(i*8)));};
    const auto snapshot=memory.snapshot_regions();
    file.write("FSBDRAW1",8);word(unsigned(snapshot.size()));
    for(const auto& region:snapshot){word(region.base);word(unsigned(region.bytes.size()));file.write(reinterpret_cast<const char*>(region.bytes.data()),region.bytes.size());}
    if(!file)throw std::runtime_error("incomplete guest snapshot");
}
inline core::Memory read_guest_snapshot(const std::filesystem::path& path){
    const auto bytes=read(path);if(bytes.size()<12||std::string(bytes.begin(),bytes.begin()+8)!="FSBDRAW1")throw std::runtime_error("invalid guest snapshot");
    std::size_t cursor=8;const auto word=[&](){std::uint32_t v=0;for(unsigned i=0;i<4;++i)v|=std::uint32_t(bytes.at(cursor++))<<(i*8);return v;};
    core::Memory memory;const auto count=word();
    for(unsigned i=0;i<count;++i){const auto base=word(),size=word();if(size>bytes.size()-cursor)throw std::runtime_error("truncated guest snapshot");memory.map(base,{bytes.begin()+cursor,bytes.begin()+cursor+size},true);cursor+=size;}
    if(cursor!=bytes.size())throw std::runtime_error("trailing guest snapshot data");return memory;
}
} // namespace fsb::lab
