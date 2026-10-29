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

#ifndef ANTARES_VIDEO_LAYOUT_HPP_
#define ANTARES_VIDEO_LAYOUT_HPP_

#include "config/preferences.hpp"
#include "math/geometry.hpp"
#include "video/opengl-driver.hpp"

namespace antares {

// Where each region of the screen goes in a window, for drivers that scale the game to fit it.
// The battlefield is drawn at the game scale; the panels and all other screens are scaled up to
// fit the display, keeping their 640x480 proportions, by a whole number unless the game scale is
// "fill". A fixed resolution is drawn at exactly that many pixels, centered in the framebuffer
// with black bars around it (scaled down only if the framebuffer can't hold it).
class Layout {
  public:
    using Region = VideoDriver::Region;

    // `window` is in the units of mouse positions, `framebuffer` in pixels. An empty
    // `resolution` fills the framebuffer.
    void update(Size window, Size framebuffer, Size resolution, GameScale scale);

    // The resolutions a fixed-resolution option can have: whole multiples of 640x480.
    static bool whole_resolution(Size resolution);

    Size  window_size() const { return _window_size; }
    Size  framebuffer_size() const { return _framebuffer_size; }
    Size  viewport_size() const { return _viewport_size; }
    // Where the viewport is in the framebuffer, in pixels from the bottom left, as for glViewport.
    Rect  viewport_rect() const { return _viewport_rect; }
    Size  screen_size() const { return _screen_size; }
    int   pixel_ratio() const { return _pixel_ratio; }
    float overlay_scale() const { return _ui_scale / _battlefield_scale; }

    // Pixels per point on the battlefield that `scale` gives at the current size.
    float battlefield_scale(GameScale scale) const;
    // Points in `region` to viewport pixels: scale by (x, y), then offset by (z, w).
    vec4  region_transform(Region region) const;
    Rect  visible_rect(Region region) const;
    Point convert(Point p, Region from, Region to) const;
    // A mouse position in the window to a point in SCREEN or, if `game`, the region under it.
    Point from_window(double x, double y, bool game) const;

  private:
    Point from_pixels(double x, double y, Region region) const;

    Size  _window_size;
    Size  _framebuffer_size;
    Size  _viewport_size;
    Rect  _viewport_rect;
    Size  _screen_size;
    int   _pixel_ratio        = 1;  // pixels per window coordinate
    float _viewport_scale     = 1;  // pixels per point at the viewport's own size
    float _battlefield_scale  = 1;  // pixels per point on the battlefield
    float _ui_scale           = 1;  // pixels per point in panels and SCREEN
    float _fill_scale         = 1;  // _ui_scale if the game scale were "fill"
    int   _left_panel_pixels  = 0;
    int   _right_panel_pixels = 0;
};

}  // namespace antares

#endif  // ANTARES_VIDEO_LAYOUT_HPP_
