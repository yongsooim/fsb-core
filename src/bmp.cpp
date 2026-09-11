#include "fsb_core/raster.hpp"

namespace fsb::core {
Image8 Image8::bmp(const std::vector<std::uint8_t>& bytes){
    const auto word=[&](std::size_t at,unsigned width){std::uint32_t value=0;for(unsigned i=0;i<width;++i){if(at+i>=bytes.size())throw Fault(0,"truncated BMP");value|=std::uint32_t(bytes[at+i])<<(i*8);}return value;};
    if(bytes.size()<54||word(0,2)!=0x4d42||word(14,4)!=40||word(26,2)!=1||word(28,2)!=8||word(30,4)!=0)throw Fault(0,"expected original 8-bit uncompressed BMP");
    const auto raw_width=signed32(word(18,4)),raw_height=signed32(word(22,4));if(raw_width<=0||!raw_height||raw_height==(-2147483647-1))throw Fault(0,"invalid BMP dimensions");
    Image8 image;image.width=unsigned(raw_width);image.height=unsigned(raw_height<0?-raw_height:raw_height);
    if(std::uint64_t(image.width)*image.height>64*1024*1024)throw Fault(0,"BMP exceeds image capacity");
    const auto palette_count=word(46,4)?word(46,4):256,offset=word(10,4);if(palette_count>256||offset<54+palette_count*4)throw Fault(0,"invalid indexed BMP palette");
    for(unsigned i=0;i<palette_count;++i)image.palette[i]={std::uint8_t(word(54+i*4+2,1)),std::uint8_t(word(54+i*4+1,1)),std::uint8_t(word(54+i*4,1))};
    const auto pitch=(std::uint64_t(image.width)+3)&~3ull;if(std::uint64_t(offset)+pitch*image.height>bytes.size())throw Fault(0,"truncated BMP pixels");
    image.pixels.resize(std::size_t(image.width)*image.height);
    for(unsigned y=0;y<image.height;++y){const auto source=offset+pitch*(raw_height>0?image.height-1-y:y);for(unsigned x=0;x<image.width;++x)image.pixels[std::size_t(y)*image.width+x]=bytes[std::size_t(source+x)];}
    return image;
}
} // namespace fsb::core
