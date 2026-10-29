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

#ifndef ANTARES_MAC_VIDEO_DRIVER_HPP_
#define ANTARES_MAC_VIDEO_DRIVER_HPP_

#include <queue>
#include <stack>

#include "config/keys.hpp"
#include "drawing/color.hpp"
#include "mac/c/CocoaVideoDriver.h"
#include "math/geometry.hpp"
#include "video/layout.hpp"
#include "video/opengl-driver.hpp"

namespace antares {

class Event;

class CocoaVideoDriver : public OpenGlVideoDriver {
  public:
    CocoaVideoDriver();

    virtual pn::string_view glsl_version() const;

    virtual Size  viewport_size() const { return _layout.viewport_size(); }
    virtual Rect  viewport_rect() const { return _layout.viewport_rect(); }
    virtual bool  resolution_supported(Size resolution) const;
    virtual Size  largest_display() const;
    virtual Size  screen_size() const { return _layout.screen_size(); }
    virtual int   scale() const { return _layout.pixel_ratio(); }
    virtual float overlay_scale() const { return _layout.overlay_scale(); }
    virtual float battlefield_scale(GameScale scale) const {
        return _layout.battlefield_scale(scale);
    }

    virtual void  set_game_layout(bool game) { _game_layout = game; }
    virtual Point convert(Point p, Region from, Region to) const {
        return _layout.convert(p, from, to);
    }
    virtual Rect visible_rect(Region region) const { return _layout.visible_rect(region); }

    virtual Point     get_mouse();
    virtual InputMode input_mode() const;

    virtual bool start_editing(TextReceiver* text);
    virtual void stop_editing(TextReceiver* text);

    virtual wall_time now() const;

    void loop(Card* initial);

  protected:
    virtual vec4 region_transform(Region region) const { return _layout.region_transform(region); }

  private:
    static wall_time _now();
    bool             update_size();
    void             fit_window();

    struct EventBridge;
    EventBridge* _bridge = nullptr;

    InputMode      _input_mode = KEYBOARD_MOUSE;
    AntaresWindow* _window     = nullptr;
    Layout         _layout;
    Size           _resolution;
    GameScale      _game_scale  = GameScale::NATIVE;
    bool           _game_layout = false;
    bool           _fullscreen  = false;
};

}  // namespace antares

#endif  // ANTARES_MAC_VIDEO_DRIVER_HPP_
