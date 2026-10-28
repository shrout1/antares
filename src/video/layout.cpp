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

#include "video/layout.hpp"

#include <algorithm>
#include <cmath>

#include "game/globals.hpp"

namespace antares {

static const Size kOriginalScreenSize = {640, 480};

void Layout::update(Size window, Size framebuffer, Size resolution, GameScale scale) {
    if ((framebuffer.width <= 0) || (framebuffer.height <= 0)) {
        // Minimized (Windows reports a 0x0 window); keep the last layout until it's restored.
        return;
    }
    _window_size      = window;
    _framebuffer_size = framebuffer;
    _pixel_ratio      = std::max(1, _framebuffer_size.width / std::max(1, _window_size.width));
    if (resolution.as_rect().empty()) {
        _viewport_size  = _framebuffer_size;
        _viewport_scale = _pixel_ratio;
    } else {
        _viewport_scale = std::min(
                float(_framebuffer_size.width) / resolution.width,
                float(_framebuffer_size.height) / resolution.height);
        _viewport_size = {
                int(std::lround(resolution.width * _viewport_scale)),
                int(std::lround(resolution.height * _viewport_scale))};
    }
    _ui_scale = std::max<float>(
            _viewport_scale, std::min(
                                     float(_viewport_size.width) / kOriginalScreenSize.width,
                                     float(_viewport_size.height) / kOriginalScreenSize.height));
    _battlefield_scale           = battlefield_scale(scale);
    _left_panel_pixels           = std::lround(kLeftPanelWidth * _ui_scale);
    _right_panel_pixels          = std::lround(kRightPanelWidth * _ui_scale);
    const int battlefield_pixels = _viewport_size.width - _left_panel_pixels - _right_panel_pixels;
    _screen_size                 = {
            kLeftPanelWidth + int(battlefield_pixels / _battlefield_scale) + kRightPanelWidth,
            int(_viewport_size.height / _battlefield_scale)};
}

float Layout::battlefield_scale(GameScale scale) const {
    switch (scale) {
        case GameScale::NATIVE: return _viewport_scale;
        case GameScale::WHOLE:
            return std::max<float>(
                    _viewport_scale,
                    std::floor(float(_viewport_size.height) / kOriginalScreenSize.height));
        case GameScale::FILL: return _ui_scale;
    }
    return _viewport_scale;
}

vec4 Layout::region_transform(Region region) const {
    const float k = _ui_scale;
    const float r = _battlefield_scale;
    // Panels are laid out around instrument_top(); center them vertically.
    const int   instrument_top = (_screen_size.height / 2) - (kOriginalScreenSize.height / 2);
    const float panel_offset   = ((_viewport_size.height - (kOriginalScreenSize.height * k)) / 2) -
                               (instrument_top * k);
    switch (region) {
        case Region::SCREEN: {
            Rect original = kOriginalScreenSize.as_rect();
            original.center_in(_screen_size.as_rect());
            return {k, k,
                    ((_viewport_size.width - (original.width() * k)) / 2) - (original.left * k),
                    ((_viewport_size.height - (original.height() * k)) / 2) - (original.top * k)};
        }
        case Region::BATTLEFIELD: return {r, r, _left_panel_pixels - (kLeftPanelWidth * r), 0};
        case Region::OVERLAY: return {k, k, _left_panel_pixels - (kLeftPanelWidth * r), 0};
        case Region::LEFT_PANEL: return {k, k, 0, panel_offset};
        case Region::RIGHT_PANEL:
            return {k, k,
                    (_viewport_size.width - _right_panel_pixels) -
                            ((_screen_size.width - kRightPanelWidth) * k),
                    panel_offset};
    }
    return {r, r, 0, 0};
}

Point Layout::from_pixels(double x, double y, Region region) const {
    const vec4 t = region_transform(region);
    return {int(std::floor((x - t.z) / t.x)), int(std::floor((y - t.w) / t.y))};
}

Rect Layout::visible_rect(Region region) const {
    // Whole points only: the region's points that are entirely within the viewport.
    const vec4 t = region_transform(region);
    return Rect(
            int(std::ceil(-t.z / t.x)), int(std::ceil(-t.w / t.y)),
            int(std::floor((_viewport_size.width - t.z) / t.x)),
            int(std::floor((_viewport_size.height - t.w) / t.y)));
}

Point Layout::convert(Point p, Region from, Region to) const {
    const vec4 t = region_transform(from);
    return from_pixels((p.h * t.x) + t.z, (p.v * t.y) + t.w, to);
}

Point Layout::from_window(double x, double y, bool game) const {
    // Window coordinates to viewport pixels, undoing the stretch.
    x             = x * _viewport_size.width / std::max(1, _window_size.width);
    y             = y * _viewport_size.height / std::max(1, _window_size.height);
    Region region = Region::SCREEN;
    if (game) {
        if (x < _left_panel_pixels) {
            region = Region::LEFT_PANEL;
        } else if (x >= (_viewport_size.width - _right_panel_pixels)) {
            region = Region::RIGHT_PANEL;
        } else {
            region = Region::BATTLEFIELD;
        }
    }
    return from_pixels(x, y, region);
}

}  // namespace antares
