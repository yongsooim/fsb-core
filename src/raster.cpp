#include "fsb_core/raster.hpp"

namespace fsb::core {
Image8 Image8::pcx(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 897 || bytes[0] != 10 || bytes[2] != 1 || bytes[3] != 8 || bytes[65] != 1)
        throw Fault(0, "expected single-plane RLE INDEX8 PCX");
    const auto u16 = [&](unsigned at) { return unsigned(bytes[at]) | unsigned(bytes[at + 1]) << 8; };
    const int width = int(u16(8)) - int(u16(4)) + 1, height = int(u16(10)) - int(u16(6)) + 1;
    const auto pitch = u16(66);
    if (width < 1 || height < 1 || width > 4096 || height > 4096 || pitch < unsigned(width) || pitch > 8192)
        throw Fault(0, "invalid PCX dimensions");
    const auto palette_offset = bytes.size() - 769;
    if (bytes[palette_offset] != 12) throw Fault(0, "missing PCX palette");
    const auto required = std::size_t(pitch) * height;
    std::vector<std::uint8_t> decoded; decoded.reserve(required);
    std::size_t at = 128;
    while (decoded.size() < required) {
        if (at >= palette_offset) throw Fault(0, "truncated PCX pixel stream");
        auto value = bytes[at++]; unsigned count = 1;
        if ((value & 0xc0) == 0xc0) {
            count = value & 63;
            if (!count || at >= palette_offset) throw Fault(0, "invalid PCX run");
            value = bytes[at++];
        }
        if (count > required - decoded.size()) throw Fault(0, "PCX run exceeds image");
        decoded.insert(decoded.end(), count, value);
    }
    Image8 image; image.width = unsigned(width); image.height = unsigned(height);
    image.pixels.reserve(std::size_t(width) * height);
    for (int y = 0; y < height; ++y)
        image.pixels.insert(image.pixels.end(), decoded.begin() + std::size_t(y) * pitch, decoded.begin() + std::size_t(y) * pitch + width);
    for (unsigned i = 0; i < 256; ++i)
        image.palette[i] = {bytes[palette_offset + 1 + i * 3], bytes[palette_offset + 2 + i * 3], bytes[palette_offset + 3 + i * 3]};
    return image;
}
SpriteFrame SpriteFrame::read(const Memory& m, Address record) {
    SpriteFrame result;
    for (auto byte : m.bytes(record, 32)) { if (!byte) break; result.resource.push_back(static_cast<char>(byte)); }
    result.bank = m.read(record + 32);
    result.x = signed32(m.read(record + 36)); result.y = signed32(m.read(record + 40));
    result.width = signed32(m.read(record + 44)); result.height = signed32(m.read(record + 48));
    result.origin_x = signed32(m.read(record + 52)); result.origin_y = signed32(m.read(record + 56));
    if (result.resource.empty() || result.x < 0 || result.y < 0 || result.width <= 0 || result.height <= 0)
        throw Fault(record, "invalid sprite frame record");
    return result;
}
void draw_sprite(Image8& target, const Image8& source, const SpriteFrame& frame, int anchor_x, int anchor_y, bool color_key, std::optional<Rect> clip) {
    if (source.pixels.size() != std::size_t(source.width) * source.height || target.pixels.size() != std::size_t(target.width) * target.height)
        throw Fault(0, "inconsistent indexed image storage");
    if (frame.x < 0 || frame.y < 0 || frame.width <= 0 || frame.height <= 0 ||
        std::uint64_t(frame.x) + frame.width > source.width || std::uint64_t(frame.y) + frame.height > source.height)
        throw Fault(0, "sprite rectangle outside its source sheet");
    const auto left = std::int64_t(anchor_x) - frame.origin_x, top = std::int64_t(anchor_y) - frame.origin_y;
    for (int y = 0; y < frame.height; ++y) for (int x = 0; x < frame.width; ++x) {
        const auto dx = left + x, dy = top + y;
        if (dx < 0 || dy < 0 || dx >= target.width || dy >= target.height) continue;
        if (clip && (dx < clip->left || dy < clip->top || dx >= clip->right || dy >= clip->bottom)) continue;
        const auto pixel = source.pixels[std::size_t(frame.y + y) * source.width + frame.x + x];
        if (pixel || !color_key) target.set_pixel(std::size_t(dy) * target.width + dx,pixel);
    }
}
} // namespace fsb::core
