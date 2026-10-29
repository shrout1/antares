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

#include "ui/screens/options.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <pn/output>

#include "config/keys.hpp"
#include "config/ledger.hpp"
#include "config/preferences.hpp"
#include "data/interface.hpp"
#include "data/resource.hpp"
#include "drawing/color.hpp"
#include "drawing/styled-text.hpp"
#include "drawing/text.hpp"
#include "game/level.hpp"
#include "game/main.hpp"
#include "game/time.hpp"
#include "sound/driver.hpp"
#include "sound/fx.hpp"
#include "sound/music.hpp"
#include "ui/card.hpp"
#include "ui/interface-handling.hpp"
#include "video/driver.hpp"

using std::make_pair;
using std::pair;
using std::vector;

namespace antares {

static constexpr char kCancelButton[]       = "cancel";
static constexpr char kDoneButton[]         = "done";
static constexpr char kKeyControlsButton[]  = "key-controls";
static constexpr char kVideoControlButton[] = "video-control";
static constexpr char kSoundControlButton[] = "options";

static constexpr char kGameMusicButton[] = "music-during-action";
static constexpr char kIdleMusicButton[] = "music-during-interlude";

static constexpr char kVolUpButton[]   = "volume-up";
static constexpr char kVolDownButton[] = "volume-down";
static constexpr char kVolRect[]       = "volume";

static constexpr char kFullscreenButton[] = "fullscreen";
static constexpr char kWindowedButton[]   = "windowed";

static constexpr char kResolutionsRect[] = "resolutions";

static const int kResolutionRowPadding = 2;

static const Font& resolution_font() { return sys.fonts.small_button; }
static int         resolution_row_height() {
    return resolution_font().height + kResolutionRowPadding;
}

// An empty size fills the display, and is always available.
static bool resolution_available(Size size) {
    return size.as_rect().empty() || sys.video->resolution_supported(size);
}

// A saved resolution the display can't show runs as "Fill Screen", so it's marked instead.
static Size current_resolution() {
    const Size current = sys.prefs->resolution();
    return resolution_available(current) ? current : Size{};
}

static const struct {
    const char* id;
    GameScale   scale;
} kGameScales[] = {
        {"game-scale/native", GameScale::NATIVE},
        {"game-scale/whole", GameScale::WHOLE},
        {"game-scale/fill", GameScale::FILL},
};

static constexpr char        kTabBox[] = "tabs";
static constexpr const char* kTabs[]   = {
        "ship", "command", "shortcuts", "utility", "hot-keys",
};
static constexpr const char* kKeys[] = {
        "accelerate",
        "decelerate",
        "turn-counter-clockwise",
        "turn-clockwise",
        "fire-weapon-1",
        "fire-weapon-2",
        "fire-activate-special",
        "warp",
        "select-friendly",
        "select-hostile",
        "select-base",
        "target",
        "order-to-go",
        "scale-in",
        "scale-out",
        "computer-previous",
        "computer-next",
        "computer-accept-select-do",
        "computer-cancel-back-up",
        "transfer-control",
        "zoom-to-1-1",
        "zoom-to-1-2",
        "zoom-to-1-4",
        "zoom-to-1-16",
        "zoom-to-closest-hostile",
        "zoom-to-closest-object",
        "zoom-to-all",
        "message-next-page-clear",
        "help",
        "lower-volume",
        "raise-volume",
        "mute-music",
        "expert-net-settings",
        "fast-motion",
        "hotkey-1",
        "hotkey-2",
        "hotkey-3",
        "hotkey-4",
        "hotkey-5",
        "hotkey-6",
        "hotkey-7",
        "hotkey-8",
        "hotkey-9",
        "hotkey-10",
};

static constexpr char kConflictTextRect[] = "conflict";

OptionsScreen::OptionsScreen() : _state(SOUND_CONTROL), _revert(sys.prefs->get().copy()) {}

void OptionsScreen::become_front() {
    switch (_state) {
        case SOUND_CONTROL: stack()->push(new SoundControlScreen(&_state)); break;

        case KEY_CONTROL: stack()->push(new KeyControlScreen(&_state)); break;

        case VIDEO_CONTROL: stack()->push(new VideoControlScreen(&_state)); break;

        case ACCEPT: stack()->pop(this); break;

        case CANCEL:
            sys.prefs->set(_revert);
            sys.music.sync();  // TODO(sfiera): do this in driver.
            stack()->pop(this);
            break;
    }
}

SoundControlScreen::SoundControlScreen(OptionsScreen::State* state)
        : InterfaceScreen("options/sound", {0, 0, 640, 480}), _state(state) {
    checkbox(kIdleMusicButton)
            ->bind({
                    [] { return sys.prefs->play_idle_music(); },
                    [](bool on) {
                        sys.prefs->set_play_idle_music(on);
                        sys.music.sync();
                    },
            });

    checkbox(kGameMusicButton)
            ->bind({
                    [] { return sys.prefs->play_music_in_game(); },
                    [](bool on) { sys.prefs->set_play_music_in_game(on); },
            });

    button(kVolUpButton)
            ->bind({
                    [] {
                        sys.prefs->set_volume(sys.prefs->volume() + 1);
                        sys.audio->set_global_volume(sys.prefs->volume());
                    },
                    [] { return sys.prefs->volume() < kMaxVolumePreference; },
            });

    button(kVolDownButton)
            ->bind({
                    [] {
                        sys.prefs->set_volume(sys.prefs->volume() - 1);
                        sys.audio->set_global_volume(sys.prefs->volume());
                    },
                    [] { return sys.prefs->volume() > 0; },
            });

    button(kDoneButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::ACCEPT;
                        stack()->pop(this);
                    },
            });

    button(kCancelButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::CANCEL;
                        stack()->pop(this);
                    },
            });

    button(kVideoControlButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::VIDEO_CONTROL;
                        stack()->pop(this);
                    },
            });

    button(kKeyControlsButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::KEY_CONTROL;
                        stack()->pop(this);
                    },
            });
}

SoundControlScreen::~SoundControlScreen() {}

void SoundControlScreen::overlay() const {
    const int volume = sys.prefs->volume();
    Rect      bounds = widget(kVolRect)->inner_bounds();
    Point     off    = offset();
    bounds.offset(off.h, off.v);

    const int notch_width  = bounds.width() / kMaxVolumePreference;
    const int notch_height = bounds.height() - 4;
    Rect      notch_bounds(0, 0, notch_width * kMaxVolumePreference, notch_height);
    notch_bounds.center_in(bounds);

    Rect notch(
            notch_bounds.left, notch_bounds.top, notch_bounds.left + notch_width,
            notch_bounds.bottom);
    notch.inset(3, 6);

    Rects rects;
    for (int i = 0; i < volume; ++i) {
        const RgbColor color = GetRGBTranslateColorShade(Hue::PALE_PURPLE, 2 * (i + 1));
        rects.fill(notch, color);
        notch.offset(notch_width, 0);
    }
}

VideoControlScreen::VideoControlScreen(OptionsScreen::State* state)
        : InterfaceScreen("options/video", {0, 0, 640, 480}), _state(state) {
    // Fullscreen and windowed are a pair: exactly one is checked, like the resolutions.
    checkbox(kFullscreenButton)
            ->bind({
                    [] { return sys.prefs->fullscreen(); },
                    [](bool) { sys.prefs->set_fullscreen(true); },
            });
    checkbox(kWindowedButton)
            ->bind({
                    [] { return !sys.prefs->fullscreen(); },
                    [](bool) { sys.prefs->set_fullscreen(false); },
            });
    // Each whole multiple of 640x480 up to the largest display, so that every pixel of the game
    // is a whole number of the display's pixels.
    const Size largest = sys.video->largest_display();
    const int  most    = std::max(1, std::min(largest.width / 640, largest.height / 480));
    _resolutions.push_back(Size{});
    for (int n = 1; n <= most; ++n) {
        _resolutions.push_back(Size{640 * n, 480 * n});
    }
    const auto current = std::find(_resolutions.begin(), _resolutions.end(), current_resolution());
    if (current != _resolutions.end()) {
        scroll_resolutions((current - _resolutions.begin()) - (visible_resolutions() - 1));
    }
    for (const auto& s : kGameScales) {
        const GameScale scale = s.scale;
        checkbox(s.id)->bind({
                [scale] { return sys.prefs->game_scale() == scale; },
                [scale](bool) { sys.prefs->set_game_scale(scale); },
        });
        _game_scale_labels.push_back(checkbox(s.id)->label().copy());
    }
    update_game_scale_labels();

    button(kDoneButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::ACCEPT;
                        stack()->pop(this);
                    },
            });

    button(kCancelButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::CANCEL;
                        stack()->pop(this);
                    },
            });

    button(kKeyControlsButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::KEY_CONTROL;
                        stack()->pop(this);
                    },
            });

    button(kSoundControlButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::SOUND_CONTROL;
                        stack()->pop(this);
                    },
            });
}

VideoControlScreen::~VideoControlScreen() {}

void VideoControlScreen::overlay() const { draw_resolutions(); }

Rect VideoControlScreen::resolution_bounds() const {
    Rect bounds = widget(kResolutionsRect)->inner_bounds();
    bounds.offset(offset().h, offset().v);
    return bounds;
}

int VideoControlScreen::visible_resolutions() const {
    return std::max(
            1, (widget(kResolutionsRect)->inner_bounds().height() - kResolutionRowPadding) /
                       resolution_row_height());
}

void VideoControlScreen::scroll_resolutions(int rows) {
    const int max_top = std::max<int>(0, _resolutions.size() - visible_resolutions());
    _top              = std::max(0, std::min(_top + rows, max_top));
}

void VideoControlScreen::choose_resolution(int index) {
    if ((index < 0) || (index >= _resolutions.size()) ||
        !resolution_available(_resolutions[index])) {
        return;
    }
    sys.prefs->set_resolution(_resolutions[index]);
    sys.sound.select();
    if (index < _top) {
        _top = index;
    } else if (index >= _top + visible_resolutions()) {
        _top = index - visible_resolutions() + 1;
    }
}

void VideoControlScreen::mouse_down(const MouseDownEvent& event) {
    const Rect bounds = resolution_bounds();
    if (bounds.contains(event.where())) {
        const int row = (event.where().v - bounds.top - kResolutionRowPadding) /
                        resolution_row_height();
        if ((0 <= row) && (row < visible_resolutions())) {
            choose_resolution(_top + row);
        }
        return;
    }
    InterfaceScreen::mouse_down(event);
}

void VideoControlScreen::mouse_scroll(const MouseScrollEvent& event) {
    _scroll -= event.dy();
    const int rows = static_cast<int>(_scroll);  // toward zero, keeping the remainder's sign.
    _scroll -= rows;
    scroll_resolutions(rows);
}

void VideoControlScreen::key_down(const KeyDownEvent& event) {
    // The arrows choose the next resolution up or down that the display can show.
    int step = 0;
    switch (event.key().value()) {
        case Key::UP_ARROW: step = -1; break;
        case Key::DOWN_ARROW: step = 1; break;
        default: return InterfaceScreen::key_down(event);
    }
    const auto current = std::find(_resolutions.begin(), _resolutions.end(), current_resolution());
    for (int i = (current - _resolutions.begin()) + step; (0 <= i) && (i < _resolutions.size());
         i += step) {
        if (resolution_available(_resolutions[i])) {
            choose_resolution(i);
            return;
        }
    }
}

void VideoControlScreen::draw_resolutions() const {
    const Font& font    = resolution_font();
    const Rect  bounds  = resolution_bounds();
    const int   rows    = std::min<int>(visible_resolutions(), _resolutions.size() - _top);
    const Size  current = current_resolution();
    const int   box     = font.ascent;

    {
        Rects rects;
        for (int row = 0; row < rows; ++row) {
            const Size size = _resolutions[_top + row];
            const int  top  = bounds.top + kResolutionRowPadding + (row * resolution_row_height());
            if (size == current) {
                rects.fill(
                        {bounds.left + 2, top, bounds.right - 6, top + resolution_row_height()},
                        GetRGBTranslateColorShade(Hue::PALE_PURPLE, DARK));
            }
            // A checkbox: filled for the chosen resolution.
            Rect check{bounds.left + 6, top + 1, bounds.left + 6 + box, top + 1 + box};
            rects.fill(check, GetRGBTranslateColorShade(Hue::PALE_PURPLE, MEDIUM));
            check.inset(1, 1);
            rects.fill(
                    check, GetRGBTranslateColorShade(
                                   Hue::PALE_PURPLE, (size == current) ? LIGHTEST : VERY_DARK));
        }
        if (_resolutions.size() > visible_resolutions()) {
            const int  h     = bounds.height() - 4;
            const int  shown = visible_resolutions();
            const Rect track = {
                    bounds.right - 4, bounds.top + 2, bounds.right - 2, bounds.top + 2 + h};
            rects.fill(track, GetRGBTranslateColorShade(Hue::PALE_PURPLE, VERY_DARK));
            rects.fill(
                    {track.left, track.top + (h * _top / int(_resolutions.size())), track.right,
                     track.top + (h * std::min<int>(_top + shown, _resolutions.size()) /
                                  int(_resolutions.size()))},
                    GetRGBTranslateColorShade(Hue::PALE_PURPLE, LIGHT));
        }
    }

    for (int row = 0; row < rows; ++row) {
        const Size size = _resolutions[_top + row];
        const int  y    = bounds.top + kResolutionRowPadding + (row * resolution_row_height()) +
                      1 + font.ascent;
        const pn::string label =
                size.as_rect().empty()
                        ? pn::string{"Fill Screen"}
                        : pn::format(
                                  "{0} x {1}  ({2}x)", size.width, size.height, size.width / 640);
        const RgbColor color =
                !resolution_available(size)
                        ? GetRGBTranslateColorShade(Hue::PALE_PURPLE, DARK)
                        : GetRGBTranslateColorShade(
                                  Hue::PALE_PURPLE, (size == current) ? LIGHTEST : LIGHT);
        font.draw({bounds.left + 12 + box, y}, label, color);
    }
}

// The ratios follow the window and the chosen resolution, so check them a few times a second.
static const usecs kLabelUpdateTime = ticks(15);

bool VideoControlScreen::next_timer(wall_time& time) {
    time = _next_label_update;
    return true;
}

void VideoControlScreen::fire_timer() { update_game_scale_labels(); }

void VideoControlScreen::update_game_scale_labels() {
    for (size_t i = 0; i < _game_scale_labels.size(); ++i) {
        // At most two decimal places, without trailing zeros.
        char ratio[16];
        snprintf(ratio, sizeof(ratio), "%.2f", sys.video->battlefield_scale(kGameScales[i].scale));
        char* end = ratio + strlen(ratio);
        while (end[-1] == '0') {
            --end;
        }
        if (end[-1] == '.') {
            --end;
        }
        *end = '\0';
        checkbox(kGameScales[i].id)->label() =
                pn::format("{0} ({1}:1)", _game_scale_labels[i], pn::string_view{ratio});
    }
    _next_label_update = now() + kLabelUpdateTime;
}

static const usecs kFlashTime = ticks(12);

// Indices of the keys controlled by each tab.  The "Ship" tab specifies keys 0..7, the "Command"
// tab specifies keys 8..18, and so on.
static const size_t kKeyIndices[] = {0, 8, 19, 28, 34, 44};

static size_t get_tab_num(size_t key) {
    for (size_t i = 1; i < 5; ++i) {
        if (key < kKeyIndices[i]) {
            return i - 1;
        }
    }
    return 4;
}

static constexpr char kTabNameStrings[]     = "tabs";
static constexpr char kControlNameStrings[] = "controls/long";

KeyControlScreen::KeyControlScreen(OptionsScreen::State* state)
        : InterfaceScreen("options/keys", {0, 0, 640, 480}),
          _state(state),
          _selected_key(-1),
          _flashed_on(false),
          _tabs(Resource::strings(kTabNameStrings)),
          _keys(Resource::strings(kControlNameStrings)) {
    button(kDoneButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::ACCEPT;
                        stack()->pop(this);
                    },
                    [this] { return _conflicts.empty(); },
            });

    button(kCancelButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::CANCEL;
                        stack()->pop(this);
                    },
            });

    button(kSoundControlButton)
            ->bind({
                    [this] {
                        *_state = OptionsScreen::SOUND_CONTROL;
                        stack()->pop(this);
                    },
                    [this] { return _conflicts.empty(); },
            });

    for (int key = kKeyIndices[0]; key < kKeyIndices[5]; ++key) {
        button(kKeys[key])->bind({[this, key] {
            _selected_key = key;
            adjust_interface();
        }});
    }

    adjust_interface();
}

KeyControlScreen::~KeyControlScreen() {}

void KeyControlScreen::key_down(const KeyDownEvent& event) {
    if (_selected_key >= 0) {
        switch (event.key().value()) {
            case Key::ESCAPE:
            case Key::RETURN:
            case Key::CAPS_LOCK:
                sys.sound.warning();
                _selected_key = -1;
                break;

            default:
                sys.prefs->set_key(_selected_key, event.key());
                int old_tab = get_tab_num(_selected_key);
                int new_tab = get_tab_num(++_selected_key);
                if (old_tab != new_tab) {
                    _selected_key = -1;
                }
                break;
        }
        update_conflicts();
        adjust_interface();
    }
}

void KeyControlScreen::key_up(const KeyUpEvent& event) { static_cast<void>(event); }

bool KeyControlScreen::next_timer(wall_time& time) {
    if (_next_flash > wall_time()) {
        time = _next_flash;
        return true;
    }
    return false;
}

void KeyControlScreen::fire_timer() {
    wall_time now = antares::now();
    while (_next_flash < now) {
        _next_flash += kFlashTime;
        _flashed_on = !_flashed_on;
    }
    adjust_interface();
}

void KeyControlScreen::adjust_interface() {
    for (const auto tab : kTabs) {
        dynamic_cast<TabButton&>(*widget(tab)).hue() = Hue::AQUA;
    }

    for (int key = kKeyIndices[0]; key < kKeyIndices[5]; ++key) {
        PlainButton* b       = button(kKeys[key]);
        Key          key_num = sys.prefs->key(key);
        b->key()             = key_num;
        if (key == _selected_key) {
            b->activate();
        } else {
            b->deactivate();
        }
        b->hue() = Hue::AQUA;
    }

    if (_flashed_on) {
        for (vector<pair<size_t, size_t>>::const_iterator it = _conflicts.begin();
             it != _conflicts.end(); ++it) {
            flash_on(it->first);
            flash_on(it->second);
        }
    }
}

void KeyControlScreen::overlay() const {
    if (!_conflicts.empty()) {
        const size_t key_one = _conflicts[0].first;
        const size_t key_two = _conflicts[0].second;

        // TODO(sfiera): permit localization.
        pn::string text = pn::format(
                "{0}: {1} conflicts with {2}: {3}", _tabs.at(get_tab_num(key_one)),
                _keys.at(key_one), _tabs.at(get_tab_num(key_two)), _keys.at(key_two));

        const TextRect& box    = dynamic_cast<const TextRect&>(*widget(kConflictTextRect));
        Rect            bounds = box.inner_bounds();
        Point           off    = offset();
        bounds.offset(off.h, off.v);
        draw_text_in_rect(bounds, text, box.style(), box.hue());
    }
}

void KeyControlScreen::update_conflicts() {
    vector<pair<size_t, size_t>> new_conflicts;
    for (size_t i = 0; i < kKeyExtendedControlNum; ++i) {
        for (size_t j = i + 1; j < kKeyExtendedControlNum; ++j) {
            if (sys.prefs->key(i) == sys.prefs->key(j)) {
                new_conflicts.push_back(make_pair(i, j));
            }
        }
    }

    _conflicts.swap(new_conflicts);

    if (_conflicts.empty()) {
        _next_flash = wall_time();
        _flashed_on = false;
    } else if (_next_flash == wall_time()) {
        _next_flash = now() + kFlashTime;
        _flashed_on = true;
    }
}

void KeyControlScreen::flash_on(size_t key) {
    TabBox* box = dynamic_cast<TabBox*>(widget(kTabBox));
    if (kKeyIndices[box->current_tab()] <= key && key < kKeyIndices[box->current_tab() + 1]) {
        PlainButton* item = button(kKeys[key]);
        item->hue()       = Hue::GOLD;
    } else {
        TabButton* item = dynamic_cast<TabButton*>(widget(kTabs[get_tab_num(key)]));
        item->hue()     = Hue::GOLD;
    }
}

}  // namespace antares
