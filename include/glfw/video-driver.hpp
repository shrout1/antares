// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2015-2017 The Antares Authors
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

#ifndef ANTARES_GLFW_VIDEO_DRIVER_HPP_
#define ANTARES_GLFW_VIDEO_DRIVER_HPP_

#include <queue>
#include <stack>

#include "config/keys.hpp"
#include "config/preferences.hpp"
#include "drawing/color.hpp"
#include "math/geometry.hpp"
#include "video/layout.hpp"
#include "video/opengl-driver.hpp"

struct GLFWwindow;

namespace antares {

class Event;

class GLFWVideoDriver : public OpenGlVideoDriver {
  public:
    GLFWVideoDriver();
    virtual ~GLFWVideoDriver();

    virtual pn::string_view glsl_version() const;

    virtual Size  viewport_size() const { return _layout.viewport_size(); }
    virtual Rect  viewport_rect() const { return _layout.viewport_rect(); }
    virtual bool  resolution_supported(Size resolution, bool fullscreen) const;
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

    virtual void* get_proc_address(const char* proc_name) const;

    void loop(Card* initial);

  protected:
    virtual vec4 region_transform(Region region) const { return _layout.region_transform(region); }

  private:
    void        key(int key, int scancode, int action, int mods);
    void        char_(unsigned int code_point);
    void        edit(int key, int action, int mods);
    void        mouse_button(int button, int action, int mods);
    void        mouse_move(double x, double y);
    void        mouse_scroll(double x, double y);
    void        window_size(int width, int height);
    void        update_size();
    void        fit_window();
    Size        window_size_for(Size resolution) const;
    Rect        work_area(Rect* frame) const;
    void        window_maximize(bool maximized);
    static void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void char_callback(GLFWwindow* w, unsigned int code_point);
    static void mouse_button_callback(GLFWwindow* w, int button, int action, int mods);
    static void mouse_move_callback(GLFWwindow* w, double x, double y);
    static void mouse_scroll_callback(GLFWwindow* w, double x, double y);
    static void window_size_callback(GLFWwindow* w, int width, int height);
    static void framebuffer_size_callback(GLFWwindow* w, int width, int height);
    static void window_maximize_callback(GLFWwindow* w, int maximized);

    bool            _fullscreen;
    Size            _window_size;
    Layout          _layout;
    Size            _resolution;
    GameScale       _game_scale  = GameScale::NATIVE;
    bool            _game_layout = false;
    pn::string_view _glsl_version;
    GLFWwindow*     _window;
    MainLoop*       _loop;
    wall_time       _last_click_usecs;
    int             _last_click_count;
    TextReceiver*   _text;
};

}  // namespace antares

#endif  // ANTARES_GLFW_VIDEO_DRIVER_HPP_
