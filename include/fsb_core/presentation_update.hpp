#pragma once
#include "fsb_core/presentation.hpp"

namespace fsb::core {
// Reuse the output buffer. An empty rectangle means every displayed RGBA byte
// is unchanged; otherwise the rectangle contains every changed pixel. The
// indexed source, palette and text detail are never modified. False forces a
// complete update (first presentation, context restoration, explicit repaint).
Rect update_presentation(const Image8& image, ImageRgba& output,
    unsigned width, unsigned height, PresentationMode mode=PresentationMode::enhanced,
    bool vga6=true, bool detect_changes=true);
}
