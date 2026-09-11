#pragma once
#include "raster.hpp"

namespace fsb::core {
struct ImageRgba {unsigned width=0,height=0;std::vector<std::uint8_t> pixels;};
enum class PresentationMode { original, enhanced };
// Output dimensions are physical pixels supplied by the host, not window
// points/DPI guesses. No game state, text metrics or source pixels are changed.
Rect fit_presentation(unsigned source_width,unsigned source_height,unsigned output_width,unsigned output_height,bool integer_scale=false);
ImageRgba present_image(const Image8& image,unsigned width,unsigned height,PresentationMode mode=PresentationMode::enhanced,bool vga6=true);
} // namespace fsb::core
