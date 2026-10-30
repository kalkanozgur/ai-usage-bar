// Renders two right-aligned text lines into a per-pixel-alpha layered window.
//
// GDI cannot write alpha, so text is drawn white-on-black with grayscale
// antialiasing and the coverage is turned into premultiplied BGRA. Empty pixels
// get alpha = 1: invisible, but still hit-testable, so the whole box is clickable.
#pragma once

#include <windows.h>

#include <string>

namespace cub::win {

struct TextBlock {
    std::wstring line1;
    std::wstring line2;
    COLORREF color1 = RGB(255, 255, 255);
    COLORREF color2 = RGB(255, 255, 255);
};

class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();
    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    /// Draws `text` so its right edge sits at `right_x`, vertically centred in [top, top+height).
    /// Returns the window rectangle that was applied.
    RECT render(HWND hwnd, const TextBlock& text, int right_x, int top, int height, UINT dpi,
                const std::wstring& font_name, int font_pt);

private:
    HFONT font(UINT dpi, const std::wstring& name, int pt);

    HFONT font_ = nullptr;
    UINT font_dpi_ = 0;
    int font_pt_ = 0;
    std::wstring font_name_;
};

}  // namespace cub::win
