#include "win/renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace cub::win {

TextRenderer::~TextRenderer() {
    if (font_ != nullptr) {
        DeleteObject(font_);
    }
}

HFONT TextRenderer::font(UINT dpi, const std::wstring& name, int pt) {
    if (font_ != nullptr && dpi == font_dpi_ && pt == font_pt_ && name == font_name_) {
        return font_;
    }
    if (font_ != nullptr) {
        DeleteObject(font_);
    }
    font_ = CreateFontW(-MulDiv(pt, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                        FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                        ANTIALIASED_QUALITY,  // grayscale AA: ClearType fringes break alpha
                        DEFAULT_PITCH | FF_SWISS, name.c_str());
    font_dpi_ = dpi;
    font_pt_ = pt;
    font_name_ = name;
    return font_;
}

RECT TextRenderer::render(HWND hwnd, const TextBlock& text, int right_x, int top, int height,
                          UINT dpi, const std::wstring& font_name, int font_pt) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old_font = SelectObject(mem, font(dpi, font_name, font_pt));

    SIZE s1{};
    SIZE s2{};
    GetTextExtentPoint32W(mem, text.line1.c_str(), static_cast<int>(text.line1.size()), &s1);
    GetTextExtentPoint32W(mem, text.line2.c_str(), static_cast<int>(text.line2.size()), &s2);
    TEXTMETRICW tm{};
    GetTextMetricsW(mem, &tm);

    const int pad = MulDiv(3, static_cast<int>(dpi), 96);
    const int w = std::max<LONG>(s1.cx, s2.cx) + 2 * pad;
    const int h = std::max(height, 1);
    const int line_h = tm.tmHeight;
    const int y0 = std::max(0, (h - 2 * line_h) / 2);

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old_bmp = SelectObject(mem, bmp);

    RECT result{right_x - w, top, right_x, top + h};
    if (bits != nullptr) {
        std::memset(bits, 0, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(255, 255, 255));
        constexpr UINT kFlags = DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX | DT_TOP;
        RECT r1{0, y0, w - pad, y0 + line_h};
        RECT r2{0, y0 + line_h, w - pad, y0 + 2 * line_h};
        DrawTextW(mem, text.line1.c_str(), -1, &r1, kFlags);
        DrawTextW(mem, text.line2.c_str(), -1, &r2, kFlags);
        GdiFlush();

        // Coverage → premultiplied BGRA in each line's colour.
        auto* px = static_cast<std::uint32_t*>(bits);
        for (int y = 0; y < h; ++y) {
            const COLORREF c = y < y0 + line_h ? text.color1 : text.color2;
            const std::uint32_t cr = GetRValue(c);
            const std::uint32_t cg = GetGValue(c);
            const std::uint32_t cb = GetBValue(c);
            for (int x = 0; x < w; ++x) {
                std::uint32_t& p = px[static_cast<std::size_t>(y) * w + x];
                const std::uint32_t a =
                    std::max({p & 0xFFu, (p >> 8) & 0xFFu, (p >> 16) & 0xFFu});
                if (a == 0) {
                    p = 0x01000000u;  // alpha 1: keeps the pixel clickable
                } else {
                    p = (a << 24) | ((cr * a / 255) << 16) | ((cg * a / 255) << 8) | (cb * a / 255);
                }
            }
        }

        POINT dst{result.left, result.top};
        SIZE size{w, h};
        POINT src{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
    }

    SelectObject(mem, old_bmp);
    SelectObject(mem, old_font);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return result;
}

}  // namespace cub::win
