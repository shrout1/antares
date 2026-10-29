// Copyright (C) 1997, 1999-2001, 2008 Nathan Lamont
// Copyright (C) 2008-2026 The Antares Authors
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

#include "ui/screens/plugins.hpp"

#include <algorithm>
#include <pn/output>

#include "config/keys.hpp"
#include "config/ledger.hpp"
#include "data/plugin.hpp"
#include "drawing/color.hpp"
#include "drawing/interface.hpp"
#include "drawing/text.hpp"
#include "game/sys.hpp"
#include "sound/fx.hpp"
#include "ui/card.hpp"
#include "video/driver.hpp"

namespace path = sfz::path;

namespace antares {

static constexpr char kPlayButton[]   = "play";
static constexpr char kCancelButton[] = "cancel";
static constexpr char kResetButton[]  = "reset-progress";
static constexpr char kListRect[]     = "list";
static constexpr char kDetailsRect[]  = "details";

static const int kRowPadding = 2;

static const Font& list_font() { return sys.fonts.small_button; }
static int         row_height() { return list_font().height + kRowPadding; }

static pn::string lowercase(pn::string_view s) {
    pn::string result;
    for (pn::rune r : s) {
        if ((pn::rune{'A'} <= r) && (r <= pn::rune{'Z'})) {
            r = pn::rune{r.value() - 'A' + 'a'};
        }
        result += r;
    }
    return result;
}

// "1-7, 9, 12-14"
static pn::string chapter_ranges(const std::set<int>& chapters) {
    pn::string result;
    for (auto it = chapters.begin(); it != chapters.end();) {
        int first = *it, last = *it;
        while ((++it != chapters.end()) && (*it == last + 1)) {
            last = *it;
        }
        if (!result.empty()) {
            result += ", ";
        }
        if (first == last) {
            result += pn::dump(first, pn::dump_short);
        } else {
            result += pn::format("{0}-{1}", first, last);
        }
    }
    return result;
}

// Chapters in `progress` that the plugin actually has.
static std::set<int> unlocked(const PluginSummary& p, const std::set<int>& progress) {
    std::set<int> result;
    for (int chapter : progress) {
        if (p.chapters.find(chapter) != p.chapters.end()) {
            result.insert(chapter);
        }
    }
    return result;
}

// Shortens `text` from the left or right until it fits in `width`.
static pn::string fit(const Font& font, pn::string_view text, int width, bool keep_end = false) {
    if (font.string_width(text) <= width) {
        return text.copy();
    }
    std::vector<pn::rune> runes(text.begin(), text.end());
    while (!runes.empty()) {
        if (keep_end) {
            runes.erase(runes.begin());
        } else {
            runes.pop_back();
        }
        pn::string s;
        if (keep_end) {
            s = "...";
        }
        for (pn::rune r : runes) {
            s += r;
        }
        if (!keep_end) {
            s += "...";
        }
        if (font.string_width(s) <= width) {
            return s;
        }
    }
    return "";
}

PluginsScreen::PluginsScreen(bool* switched)
        : InterfaceScreen("plugins", {0, 0, 640, 480}), _switched(switched) {
    *_switched = false;
    for (PluginSummary& p : list_plugins()) {
        Entry e;
        e.name    = p.title.copy();
        e.current = is_loaded(p);
        e.summary = std::move(p);
        _entries.push_back(std::move(e));
    }

    // Tell apart plugins that share a title by their file names.
    for (Entry& e : _entries) {
        int same = std::count_if(_entries.begin(), _entries.end(), [&e](const Entry& other) {
            return other.summary.title == e.summary.title;
        });
        if ((same > 1) && e.summary.path.has_value()) {
            pn::string_view base = path::basename(*e.summary.path);
            pn::string_view ext  = ".antaresplugin";
            if ((base.size() > ext.size()) && (base.substr(base.size() - ext.size()) == ext)) {
                base = base.substr(0, base.size() - ext.size());
            }
            if (base == e.summary.title) {
                e.name = pn::format(
                        "{0} ({1})", e.summary.title,
                        path::basename(path::dirname(*e.summary.path)));
            } else if (base.find(e.summary.title) == 0) {
                e.name = base.copy();  // already "Title (something)".
            } else if ((base.find(" (") != base.npos) && (base.substr(base.size() - 1) == ")")) {
                // "Other-Spelling (something)": keep the title and the part in parentheses.
                e.name = pn::format("{0}{1}", e.summary.title, base.substr(base.find(" (")));
            } else {
                e.name = pn::format("{0} ({1})", e.summary.title, base);
            }
        }
    }
    std::stable_sort(_entries.begin(), _entries.end(), [](const Entry& x, const Entry& y) {
        return lowercase(x.name) < lowercase(y.name);
    });
    for (Entry& e : _entries) {
        refresh_progress(&e);
    }
    for (int i = 0; i < _entries.size(); ++i) {
        if (_entries[i].current) {
            select(i);
            break;
        }
    }

    button(kPlayButton)
            ->bind({
                    [this] { play(); },
                    [this] { return playable(selected()); },
            });

    button(kCancelButton)->bind({[this] { stack()->pop(this); }});

    button(kResetButton)
            ->bind({
                    [this] {
                        const Entry& e = selected();
                        if (_confirm != RESET) {
                            confirm(RESET,
                                    pn::format(
                                            "Forget all progress in {0}? Press Reset again.",
                                            e.summary.title));
                            return;
                        }
                        _confirm = NONE;
                        reset_progress(e.summary.identifier);
                        refresh_progress(&_entries[_index]);
                        _status = "Progress reset.";
                    },
                    [this] {
                        return selected().summary.error.empty() && selected().progress.has_value();
                    },
            });
}

PluginsScreen::~PluginsScreen() = default;

bool PluginsScreen::playable(const Entry& e) const {
    if (!e.summary.error.empty()) {
        return false;
    }
    return e.current || plugin_switching_enabled();
}

void PluginsScreen::refresh_progress(Entry* e) {
    if (!e->summary.error.empty()) {
        return;
    }
    try {
        e->progress = load_progress(e->summary.identifier);
    } catch (std::exception& x) {
        e->summary.error = pn::format("progress: {0}", x.what());
    }
}

void PluginsScreen::select(int index) {
    if (_entries.empty()) {
        return;
    }
    index = std::max(0, std::min<int>(index, _entries.size() - 1));
    if (index != _index) {
        _confirm = NONE;
        _status.clear();
    }
    _index = index;
    if (_index < _top) {
        _top = _index;
    } else if (_index >= _top + visible_rows()) {
        _top = _index - visible_rows() + 1;
    }
}

void PluginsScreen::play() {
    const Entry& e = selected();
    if (!e.current) {
        request_plugin(
                e.summary.path.has_value() ? sfz::make_optional<pn::string_view>(*e.summary.path)
                                           : sfz::nullopt);
        *_switched = true;
    }
    stack()->pop(this);
}

void PluginsScreen::confirm(Confirm action, pn::string message) {
    _confirm = action;
    _status  = std::move(message);
    sys.sound.warning();
}

Rect PluginsScreen::list_bounds() const {
    Rect bounds = widget(kListRect)->inner_bounds();
    bounds.offset(offset().h, offset().v);
    return bounds;
}

int PluginsScreen::visible_rows() const {
    return std::max(1, (widget(kListRect)->inner_bounds().height() - kRowPadding) / row_height());
}

void PluginsScreen::mouse_down(const MouseDownEvent& event) {
    Rect bounds = list_bounds();
    if (bounds.contains(event.where())) {
        int row = (event.where().v - bounds.top - kRowPadding) / row_height();
        if ((0 <= row) && (row < visible_rows()) && (_top + row < _entries.size())) {
            select(_top + row);
            if ((event.count() == 2) && playable(selected())) {
                play();
            }
        }
        return;
    }
    InterfaceScreen::mouse_down(event);
}

void PluginsScreen::key_down(const KeyDownEvent& event) {
    const int page = visible_rows() - 1;
    switch (event.key().value()) {
        case Key::UP_ARROW: select(_index - 1); return;
        case Key::DOWN_ARROW: select(_index + 1); return;
        case Key::PAGE_UP: select(_index - page); return;
        case Key::PAGE_DOWN: select(_index + page); return;
        case Key::HOME: select(0); return;
        case Key::END: select(_entries.size() - 1); return;
        default: break;
    }

    // A letter selects the next plugin whose name starts with it.
    int letter = event.key().value() - Key::A;
    if ((0 <= letter) && (letter < 26) && !_entries.empty()) {
        pn::rune r{static_cast<uint32_t>('a' + letter)};
        for (int i = 1; i <= _entries.size(); ++i) {
            int        j    = (_index + i) % _entries.size();
            pn::string name = lowercase(_entries[j].name);
            if (!name.empty() && (*name.begin() == r)) {
                select(j);
                break;
            }
        }
        return;
    }
    InterfaceScreen::key_down(event);
}

void PluginsScreen::gamepad_button_down(const GamepadButtonDownEvent& event) {
    const int page = visible_rows() - 1;
    switch (event.button) {
        case Gamepad::Button::UP: select(_index - 1); return;
        case Gamepad::Button::DOWN: select(_index + 1); return;
        case Gamepad::Button::LB: select(_index - page); return;
        case Gamepad::Button::RB: select(_index + page); return;
        default: return InterfaceScreen::gamepad_button_down(event);
    }
}

void PluginsScreen::overlay() const {
    draw_list();
    draw_details();
}

void PluginsScreen::draw_list() const {
    const Font& font   = list_font();
    const Rect  bounds = list_bounds();
    const int   rows   = visible_rows();
    const int   right  = bounds.right - 8;

    Rects rects;
    for (int row = 0; (row < rows) && (_top + row < _entries.size()); ++row) {
        int  i = _top + row;
        Rect r{bounds.left + 2, bounds.top + kRowPadding + (row * row_height()), bounds.right - 6,
               bounds.top + kRowPadding + ((row + 1) * row_height())};
        if (i == _index) {
            rects.fill(r, GetRGBTranslateColorShade(Hue::AQUA, DARK));
        }
    }
    if (_entries.size() > rows) {
        // Scroll bar.
        int h   = bounds.height() - 4;
        int top = bounds.top + 2 + (h * _top / _entries.size());
        int bot = bounds.top + 2 +
                  (h * std::min<int>(_top + rows, _entries.size()) / _entries.size());
        Rect track = {bounds.right - 4, bounds.top + 2, bounds.right - 2, bounds.top + 2 + h};
        rects.fill(track, GetRGBTranslateColorShade(Hue::AQUA, VERY_DARK));
        rects.fill(
                {track.left, top, track.right, bot}, GetRGBTranslateColorShade(Hue::AQUA, LIGHT));
    }

    for (int row = 0; (row < rows) && (_top + row < _entries.size()); ++row) {
        const Entry& e     = _entries[_top + row];
        const int    y     = bounds.top + kRowPadding + (row * row_height()) + 1 + font.ascent;
        RgbColor     color = e.current ? GetRGBTranslateColorShade(Hue::GOLD, LIGHT)
                                       : GetRGBTranslateColorShade(Hue::AQUA, LIGHTEST);

        pn::string progress;
        if (!e.summary.error.empty()) {
            progress = "unreadable";
            color    = GetRGBTranslateColorShade(Hue::SALMON, LIGHT);
        } else if (e.summary.chapters.empty()) {
            progress = "net only";
        } else if (!e.progress.has_value()) {
            progress = pn::format("-/{0}", e.summary.chapters.size());
        } else {
            progress = pn::format(
                    "{0}/{1}", unlocked(e.summary, *e.progress).size(), e.summary.chapters.size());
        }
        int progress_width = font.string_width(progress);
        font.draw({right - progress_width, y}, progress, color);

        pn::string name = fit(font, e.name, right - progress_width - 12 - (bounds.left + 6));
        font.draw({bounds.left + 6, y}, name, color);
    }
}

void PluginsScreen::draw_details() const {
    if (_entries.empty()) {
        return;
    }
    const Entry&         e      = selected();
    const PluginSummary& p      = e.summary;
    const Font&          font   = list_font();
    Rect                 bounds = widget(kDetailsRect)->inner_bounds();
    bounds.offset(offset().h, offset().v);
    const int width = bounds.width() - 8;

    std::vector<std::pair<pn::string, RgbColor>> lines;
    const RgbColor normal = GetRGBTranslateColorShade(Hue::AQUA, LIGHTEST);
    const RgbColor dim    = GetRGBTranslateColorShade(Hue::AQUA, LIGHT);
    const RgbColor gold   = GetRGBTranslateColorShade(Hue::GOLD, LIGHTEST);

    pn::string heading = p.title.copy();
    if (!p.version.empty()) {
        heading += pn::format(" {0}", p.version);
    }
    if (!p.author.empty()) {
        heading += pn::format(" by {0}", p.author);
    }
    lines.emplace_back(fit(font, heading, width), gold);
    lines.emplace_back(
            p.path.has_value() ? fit(font, *p.path, width, true) : pn::string{"Factory scenario"},
            dim);

    if (!p.error.empty()) {
        lines.emplace_back(
                fit(font, pn::format("Can't read this plugin: {0}", p.error), width),
                GetRGBTranslateColorShade(Hue::SALMON, LIGHTEST));
    } else {
        std::set<int> all;
        for (const auto& kv : p.chapters) {
            all.insert(kv.first);
        }
        pn::string levels;
        if (all.empty()) {
            levels = "No solo chapters";
        } else {
            levels = pn::format("Solo chapters: {0} ({1})", all.size(), chapter_ranges(all));
        }
        if (p.net_levels) {
            levels += pn::format(".  Net levels: {0}", p.net_levels);
        }
        lines.emplace_back(fit(font, levels, width), normal);

        if (!all.empty()) {
            pn::string progress;
            if (!e.progress.has_value()) {
                progress =
                        pn::format("Not started. First chapter: {0}", p.chapters.begin()->second);
            } else {
                std::set<int> done = unlocked(p, *e.progress);
                progress           = pn::format(
                        "Unlocked: {0} ({1} of {2})",
                        done.empty() ? pn::string{"none"} : chapter_ranges(done), done.size(),
                        all.size());
                for (const auto& kv : p.chapters) {
                    if (done.find(kv.first) == done.end()) {
                        progress += pn::format(".  Next: {0}", kv.second);
                        break;
                    }
                }
            }
            lines.emplace_back(fit(font, progress, width), normal);
        }
    }
    if (!_status.empty()) {
        lines.emplace_back(
                fit(font, _status, width, _status.size() > 0 && _confirm == NONE),
                GetRGBTranslateColorShade(
                        _confirm == NONE ? Hue::PALE_GREEN : Hue::SALMON, LIGHTEST));
    }

    int y = bounds.top + kRowPadding + 1 + font.ascent;
    for (const auto& line : lines) {
        if (y > bounds.bottom) {
            break;
        }
        font.draw({bounds.left + 4, y}, line.first, line.second);
        y += row_height();
    }
}

}  // namespace antares
