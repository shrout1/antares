// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2017 The Antares Authors
//
// This file is part of Antares, a tactical space combat game.
//
// Antares is free software: you can redistribute it and/or modify it
// under the terms of the Lesser GNU General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Antares is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with Antares.  If not, see http://www.gnu.org/licenses/

#include "drawing/shapes.hpp"

#include <algorithm>
#include <cmath>

#include "drawing/color.hpp"
#include "drawing/pix-map.hpp"
#include "video/driver.hpp"

using std::min;

namespace antares {

// must be square
void draw_triangle_up(PixMap* destPix, const RgbColor& color) {
    int32_t size = destPix->size().width;
    Rect    r(0, 0, size, 2);
    for (int32_t i = 0; i < size; i += 2) {
        r.bottom = min(r.bottom, size);
        destPix->view(r).fill(color);
        r.inset(1, 0);
        r.offset(0, 2);
    }
}

// Covers each pixel of `destPix` by the fraction of it inside the shape, sampled on a 4×4 grid.
// `inside` takes coordinates in pixels of the shape at `size`.
template <typename Inside>
static void draw_smooth(PixMap* destPix, int32_t size, const RgbColor& color, Inside inside) {
    const int     samples = 4;
    const int32_t pixels  = destPix->size().width;
    const double  unit    = double(size) / pixels;
    for (int32_t y = 0; y < pixels; ++y) {
        for (int32_t x = 0; x < pixels; ++x) {
            int covered = 0;
            for (int i = 0; i < samples; ++i) {
                for (int j = 0; j < samples; ++j) {
                    covered += inside(
                            (x + (j + 0.5) / samples) * unit, (y + (i + 0.5) / samples) * unit);
                }
            }
            RgbColor c = color;
            c.alpha    = color.alpha * covered / (samples * samples);
            destPix->set(x, y, c);
        }
    }
}

void draw_smooth_triangle_up(PixMap* destPix, int32_t size, const RgbColor& color) {
    draw_smooth(destPix, size, color, [size](double x, double y) {
        return std::abs(x - size / 2.0) < (size - y) / 2.0;
    });
}

void draw_smooth_diamond(PixMap* destPix, int32_t size, const RgbColor& color) {
    // Same bounds as draw_compat_diamond().
    const double half = (size - 1) / 2.0;
    draw_smooth(destPix, size, color, [half](double x, double y) {
        return std::abs(x - 1 - half) + std::abs(y - half) < half;
    });
}

static void draw_plus(PixMap::View pix, RgbColor color) {
    int32_t size = pix.size().width;
    if (size <= 3) {
        pix.fill(color);
        return;
    }
    int32_t half = size / 2;
    pix.view(Rect(0, half - 1, size, half + 2)).fill(color);
    pix.view(Rect(half - 1, 0, half + 2, size)).fill(color);
}

// Compatibility shim.  The original implementation of this function
// didn't properly fill the rect, so this function trims the rect
// correspondingly and forwards to draw_plus().
void draw_compat_plus(PixMap* destPix, const RgbColor& color) {
    Rect bounds = destPix->size().as_rect();
    if (bounds.right != 1) {
        ++bounds.left;
        --bounds.bottom;
        if (bounds.right % 2) {
            --bounds.right;
            --bounds.bottom;
        }
    }
    draw_plus(destPix->view(bounds), color);
}

static void draw_diamond(PixMap::View pix, RgbColor color) {
    int32_t size = pix.size().width;
    int32_t half = (size + 1) / 2;
    for (int i = 0; i < half; ++i) {
        Rect r = pix.size().as_rect();
        r.inset(i, half - i - 1);
        pix.view(r).fill(color);
    }
}

// Compatibility shim.  The original implementation of this function
// didn't properly fill the rect, so this function trims the rect
// correspondingly and forwards to draw_diamond().
void draw_compat_diamond(PixMap* destPix, const RgbColor& color) {
    Rect bounds = destPix->size().as_rect();
    if (bounds.right != 1) {
        ++bounds.left;
        --bounds.bottom;
    }
    if (bounds.right == 3) {
        destPix->view(bounds).fill(color);
    } else {
        draw_diamond(destPix->view(bounds), color);
    }
}

void draw_vbracket(const Rects& rects, const Rect& rect, const RgbColor& color) {
    Point ul(rect.left, rect.top);
    Point ur(rect.right - 1, rect.top);
    Point ll(rect.left, rect.bottom - 1);
    Point lr(rect.right - 1, rect.bottom - 1);

    rects.fill({rect.left, rect.top, rect.right, rect.top + 1}, color);
    rects.fill({rect.left, rect.top, rect.left + 1, rect.top + 2}, color);
    rects.fill({rect.right - 1, rect.top, rect.right, rect.top + 2}, color);

    rects.fill({rect.left, rect.bottom - 1, rect.right, rect.bottom}, color);
    rects.fill({rect.left, rect.bottom - 2, rect.left + 1, rect.bottom}, color);
    rects.fill({rect.right - 1, rect.bottom - 2, rect.right, rect.bottom}, color);
}

void draw_shaded_rect(
        const Rects& rects, Rect rect, const RgbColor& fill_color, const RgbColor& light_color,
        const RgbColor& dark_color) {
    rects.fill({rect.left, rect.top, rect.left + 1, rect.bottom}, light_color);
    rects.fill({rect.left, rect.top, rect.right, rect.top + 1}, light_color);

    rects.fill({rect.right - 1, rect.top, rect.right, rect.bottom}, dark_color);
    rects.fill({rect.left, rect.bottom - 1, rect.right, rect.bottom}, dark_color);

    rect.inset(1, 1);
    if ((rect.height() > 0) && (rect.width() > 0)) {
        rects.fill(rect, fill_color);
    }
}

void draw_shaded_rect(
        const Rects& rects, Rect rect, Hue hue, uint8_t fill_color, uint8_t light_color,
        uint8_t dark_color) {
    draw_shaded_rect(
            rects, rect, GetRGBTranslateColorShade(hue, fill_color),
            GetRGBTranslateColorShade(hue, light_color),
            GetRGBTranslateColorShade(hue, dark_color));
}

}  // namespace antares
