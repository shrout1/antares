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

#include "ui/screens/net-setup.hpp"

#include <algorithm>
#include <pn/output>

#include "data/level.hpp"
#include "data/plugin.hpp"
#include "data/races.hpp"
#include "drawing/color.hpp"
#include "drawing/text.hpp"
#include "game/sys.hpp"
#include "game/time.hpp"
#include "math/units.hpp"
#include "sound/fx.hpp"
#include "video/driver.hpp"

namespace antares {

static constexpr char kJoinButton[]            = "join";
static constexpr char kHostButton[]            = "host";
static constexpr char kCancelButton[]          = "cancel";
static constexpr char kAddressRect[]           = "address";
static constexpr char kStatusRect[]            = "status";
static constexpr char kBeginButton[]           = "begin";
static constexpr char kPreviousButton[]        = "previous";
static constexpr char kNextButton[]            = "next";
static constexpr char kLevelRect[]             = "level";
static constexpr char kSendTextButton[]        = "send-text";
static constexpr char kEnterTextRect[]         = "enter-text";
static constexpr char kMessagesRect[]          = "messages";
static constexpr char kIncreaseButton[]        = "increase";
static constexpr char kDecreaseButton[]        = "decrease";
static constexpr char kLagRect[]               = "lag";
static constexpr char kNextSpeciesButton[]     = "next-species";
static constexpr char kPreviousSpeciesButton[] = "previous-species";
static constexpr char kSpeciesRect[]           = "species";
static constexpr char kSelectColorRect[]       = "select-color";

namespace {

const usecs    kPollInterval = std::chrono::milliseconds(100);
const uint32_t kMaxDelay     = 20;
const int      kMaxMessages  = 50;

const char* const kHueNames[] = {
        "Gray", "Orange", "Yellow", "Blue",       "Green",       "Purple",   "Indigo", "Salmon",
        "Gold", "Aqua",   "Pink",   "Pale Green", "Pale Purple", "Sky Blue", "Tan",    "Red",
};
const int kHueCount = 16;

// Net levels for as many players as a session holds.
std::vector<pn::string_view> net_levels() {
    std::vector<pn::string_view> names;
    for (const auto& kv : plug.levels) {
        if ((kv.second.type() == Level::Type::NET) &&
            (kv.second.net.players.size() == net::kSessionPlayers)) {
            names.push_back(kv.first);
        }
    }
    return names;
}

const std::vector<NamedHandle<const Race>>& races_for(const net::Setup& setup, int player) {
    static const std::vector<NamedHandle<const Race>> kNone;
    const Level*                                      level = Level::get(setup.level);
    if (!level || (level->type() != Level::Type::NET) || (level->net.players.size() <= player)) {
        return kNone;
    }
    return level->net.players[player].races;
}

const Race* race(pn::string_view name) {
    load_race(NamedHandle<const Race>(name));
    return Race::get(name);
}

bool has_race(const std::vector<NamedHandle<const Race>>& races, pn::string_view name) {
    return std::any_of(races.begin(), races.end(), [name](const NamedHandle<const Race>& r) {
        return r.name() == name;
    });
}

int other_hue(const net::Setup& setup, int local) {
    return (setup.players.size() > 1) ? setup.players[1 - local].hue : -1;
}

bool allowed(pn::string_view race_name, int hue, int other_hue) {
    if ((hue <= 0) || (kHueCount <= hue) || (hue == other_hue)) {
        return false;
    }
    const auto& not_hue = race(race_name)->not_hue;
    return std::find(not_hue.begin(), not_hue.end(), static_cast<Hue>(hue)) == not_hue.end();
}

int next_hue(pn::string_view race_name, int hue, int by, int other_hue) {
    for (int i = 0; i < kHueCount; ++i) {
        hue = (hue + by + kHueCount) % kHueCount;
        if (allowed(race_name, hue, other_hue)) {
            return hue;
        }
    }
    return hue;
}

pn::string level_title(pn::string_view name) {
    const Level* level = Level::get(name);
    if (!level) {
        return name.copy();
    }
    pn::string_view rest = level->base.name;
    rest                 = rest.substr(0, std::min(rest.find("\n"), rest.size()));
    pn::string title;
    for (int i = rest.find("\\"); i != pn::string_view::npos; i = rest.find("\\")) {
        title += rest.substr(0, i);
        rest = rest.substr(std::min(i + 2, rest.size()));
    }
    title += rest;
    return title;
}

Rect inner_bounds(const Widget& w, Point off) {
    Rect r = w.inner_bounds();
    r.offset(off.h, off.v);
    return r;
}

void draw_centered(const Rect& r, pn::string_view text, Hue hue, const Font& font) {
    Point at(r.center().h - (font.string_width(text) / 2), r.center().v - (font.height / 2));
    font.draw(Point(at.h, at.v + font.ascent), text, GetRGBTranslateColorShade(hue, LIGHTEST));
}

int level_index(const std::vector<pn::string_view>& levels, pn::string_view level) {
    auto it = std::find(levels.begin(), levels.end(), level);
    return (it == levels.end()) ? -1 : (it - levels.begin());
}

int race_index(const std::vector<NamedHandle<const Race>>& races, pn::string_view name) {
    for (int i = 0; i < races.size(); ++i) {
        if (races[i].name() == name) {
            return i;
        }
    }
    return -1;
}

// The rows of a list that fit in `box`, scrolled to show `selected`.
struct ListRows {
    int first;
    int count;
    int top;
};

ListRows list_rows(const Rect& box, int size, int selected) {
    const int height = sys.fonts.button.height;
    const int rows   = std::max(1, box.height() / height);
    ListRows  r;
    r.first = std::max(0, std::min(selected - (rows / 2), size - rows));
    r.count = std::min(rows, size - r.first);
    r.top   = box.top + ((box.height() - (rows * height)) / 2);
    return r;
}

Rect list_row(const Rect& box, const ListRows& rows, int i) {
    const int top = rows.top + ((i - rows.first) * sys.fonts.button.height);
    return Rect(box.left, top, box.right, top + sys.fonts.button.height);
}

int list_index(const Rect& box, int size, int selected, Point where) {
    const ListRows rows = list_rows(box, size, selected);
    for (int i = rows.first; i < rows.first + rows.count; ++i) {
        if (list_row(box, rows, i).contains(where)) {
            return i;
        }
    }
    return -1;
}

void draw_list(const Rect& box, const std::vector<pn::string>& items, int selected, Hue hue) {
    const Font&    font = sys.fonts.button;
    const ListRows rows = list_rows(box, items.size(), selected);
    for (int i = rows.first; i < rows.first + rows.count; ++i) {
        const Rect row = list_row(box, rows, i);
        if (i == selected) {
            Rects().fill(row, GetRGBTranslateColorShade(hue, DARK));
        }
        font.draw(
                Point(row.left + 4, row.top + font.ascent), items[i],
                GetRGBTranslateColorShade(hue, (i == selected) ? LIGHTEST : LIGHT));
    }
}

// Colors other than gray, in two rows.
const int kSwatchColumns = 8;

Rect swatch(const Rect& box, int hue) {
    const int i = hue - 1;
    const int w = box.width() / kSwatchColumns;
    const int h = box.height() / 2;
    Rect      r(box.left + ((i % kSwatchColumns) * w), box.top + ((i / kSwatchColumns) * h),
                box.left + ((i % kSwatchColumns + 1) * w), box.top + ((i / kSwatchColumns + 1) * h));
    r.inset(2, 2);
    return r;
}

}  // namespace

NetTextField::NetTextField(
        int32_t width, std::function<void()> accept, std::function<void()> escape)
        : EditableText("", ""),
          _width(width),
          _accept(std::move(accept)),
          _escape(std::move(escape)),
          _text(StyledText::plain("", {sys.fonts.tactical, width})) {}

void NetTextField::accept() { _accept(); }
void NetTextField::escape() { _escape(); }

void NetTextField::update(pn::string_view text, range<int> selection, range<int> mark) {
    _text = StyledText::plain(
            text, {sys.fonts.tactical, _width}, GetRGBTranslateColorShade(Hue::GREEN, LIGHTEST));
    _text.select(selection.begin, selection.end);
    _text.mark(mark.begin, mark.end);
}

void NetTextField::draw(const Rect& bounds, bool editing) const {
    Rect r = bounds;
    r.inset(3, (bounds.height() - sys.fonts.tactical.height) / 2);
    _text.draw(r);
    if (editing) {
        _text.draw_cursor(r, GetRGBTranslateColorShade(Hue::GREEN, LIGHTEST));
    }
}

HostOrJoinScreen::HostOrJoinScreen(Choice* choice)
        : InterfaceScreen("net/host-or-join", {0, 0, 640, 480}), _choice(choice) {
    button(kJoinButton)->bind({[this] {
        *_choice = JOIN;
        stack()->pop(this);
    }});
    button(kHostButton)->bind({[this] {
        *_choice = HOST;
        stack()->pop(this);
    }});
    button(kCancelButton)->bind({[this] {
        *_choice = CANCEL;
        stack()->pop(this);
    }});
}

NetAddressScreen::NetAddressScreen(bool* cancelled, pn::string* address)
        : InterfaceScreen("net/address", {0, 0, 640, 480}),
          _cancelled(cancelled),
          _address(address),
          _field(
                  widget(kAddressRect)->inner_bounds().width() - 6, [this] { join(); },
                  [this] {
                      *_cancelled = true;
                      stack()->pop(this);
                  }) {
    button(kJoinButton)->bind({[this] { join(); }, [this] { return _field.size() > 0; }});
    button(kCancelButton)->bind({[this] {
        *_cancelled = true;
        stack()->pop(this);
    }});
}

void NetAddressScreen::become_front() {
    InterfaceScreen::become_front();
    if (sys.video->start_editing(&_field)) {
        _field.replace({0, _field.size()}, *_address);
    }
}

void NetAddressScreen::resign_front() {
    sys.video->stop_editing(&_field);
    InterfaceScreen::resign_front();
}

void NetAddressScreen::join() {
    if (_field.size() == 0) {
        return;
    }
    *_address   = _field.text().copy();
    *_cancelled = false;
    stack()->pop(this);
}

void NetAddressScreen::overlay() const {
    _field.draw(inner_bounds(*widget(kAddressRect), offset()), true);
}

NetStatusScreen::NetStatusScreen(net::Session* session, pn::string_view status)
        : InterfaceScreen("net/status", {0, 0, 640, 480}),
          _session(session),
          _status(status.copy()),
          _next_timer(now()) {
    button(kCancelButton)->bind({[this] { stack()->pop(this); }});
}

bool NetStatusScreen::next_timer(wall_time& time) {
    time = _next_timer;
    return _session != nullptr;
}

void NetStatusScreen::fire_timer() {
    _next_timer = now() + kPollInterval;
    switch (_session->state()) {
        case net::Session::CONNECTING: break;
        case net::Session::CLOSED:
            _status  = _session->error();
            _session = nullptr;
            break;
        default: stack()->pop(this); break;
    }
}

void NetStatusScreen::overlay() const {
    draw_centered(
            inner_bounds(*widget(kStatusRect), offset()), _status, Hue::TAN, sys.fonts.button);
}

NetLobbyScreen::NetLobbyScreen(net::Session* session)
        : InterfaceScreen("net/lobby", {0, 0, 640, 480}),
          _session(session),
          _field(
                  widget(kEnterTextRect)->inner_bounds().width() - 6, [this] { send(); },
                  [this] {
                      _editing = false;
                      sys.video->stop_editing(&_field);
                  }),
          _next_timer(now()) {
    const bool is_host = _session->is_host();

    button(kBeginButton)
            ->bind({
                    [this] { _session->start(); },
                    [this, is_host] {
                        return is_host && _session->has_peer() &&
                               Level::get(_session->setup().level);
                    },
            });
    button(kCancelButton)->bind({[this] { stack()->pop(this); }});

    button(kPreviousButton)
            ->bind({
                    [this] { change_level(-1); },
                    [is_host] { return is_host && (net_levels().size() > 1); },
            });
    button(kNextButton)
            ->bind({
                    [this] { change_level(+1); },
                    [is_host] { return is_host && (net_levels().size() > 1); },
            });

    button(kSendTextButton)->bind({[this] {
        if (_editing) {
            send();
        } else if (sys.video->start_editing(&_field)) {
            _editing = true;
        }
    }});

    button(kIncreaseButton)
            ->bind({
                    [this] { change_delay(+1); },
                    [this, is_host] { return is_host && (_session->setup().delay < kMaxDelay); },
            });
    button(kDecreaseButton)
            ->bind({
                    [this] { change_delay(-1); },
                    [this, is_host] { return is_host && (_session->setup().delay > 1); },
            });

    auto can_change_race = [this] {
        return races_for(_session->setup(), _session->local()).size() > 1;
    };
    button(kNextSpeciesButton)->bind({[this] { change_race(+1); }, can_change_race});
    button(kPreviousSpeciesButton)->bind({[this] { change_race(-1); }, can_change_race});
}

void NetLobbyScreen::become_front() {
    InterfaceScreen::become_front();
    if (_session->is_host()) {
        net::Setup setup = _session->setup();
        if (!Level::get(setup.level)) {
            std::vector<pn::string_view> levels = net_levels();
            if (!levels.empty()) {
                setup.level = levels[0].copy();
                _session->set_setup(std::move(setup));
            }
        }
        if (!_session->has_peer()) {
            _messages.push_back(
                    pn::format("Waiting for a player to join on port {0}.", _session->port()));
        }
    }
    update();
}

void NetLobbyScreen::resign_front() {
    _editing = false;
    sys.video->stop_editing(&_field);
    InterfaceScreen::resign_front();
}

bool NetLobbyScreen::next_timer(wall_time& time) {
    time = _next_timer;
    return true;
}

void NetLobbyScreen::fire_timer() {
    _next_timer = now() + kPollInterval;
    if (_session->state() != net::Session::LOBBY) {
        stack()->pop(this);
        return;
    }
    for (auto& chat : _session->take_chats()) {
        _messages.push_back(std::move(chat));
    }
    update();
}

void NetLobbyScreen::mouse_down(const MouseDownEvent& event) {
    Point where = event.where();
    Point off   = offset();
    where.offset(-off.h, -off.v);
    if (event.button() != 0) {
        InterfaceScreen::mouse_down(event);
        return;
    }

    const net::Setup setup = _session->setup();
    const int        local = _session->local();
    if (widget(kEnterTextRect)->inner_bounds().contains(where)) {
        if (!_editing && sys.video->start_editing(&_field)) {
            _editing = true;
        }
        return;
    }

    if (_session->is_host()) {
        std::vector<pn::string_view> levels = net_levels();
        int                          i      = list_index(
                widget(kLevelRect)->inner_bounds(), levels.size(),
                level_index(levels, setup.level), where);
        if (i >= 0) {
            sys.sound.select();
            choose_level(i);
            return;
        }
    }

    if (setup.players.size() > local) {
        const auto& races = races_for(setup, local);
        int         i     = list_index(
                widget(kSpeciesRect)->inner_bounds(), races.size(),
                race_index(races, setup.players[local].race), where);
        if (i >= 0) {
            sys.sound.select();
            choose(races[i].name(), -1);
            update();
            return;
        }

        const Rect colors = widget(kSelectColorRect)->inner_bounds();
        for (int hue = 1; hue < kHueCount; ++hue) {
            if (swatch(colors, hue).contains(where)) {
                choose_hue(hue);
                return;
            }
        }
    }

    InterfaceScreen::mouse_down(event);
}

void NetLobbyScreen::change_level(int by) {
    std::vector<pn::string_view> levels = net_levels();
    int                          i = std::max(0, level_index(levels, _session->setup().level));
    choose_level((i + by + levels.size()) % levels.size());
}

void NetLobbyScreen::choose_level(int index) {
    net::Setup setup = _session->setup();
    setup.level      = net_levels()[index].copy();
    _session->set_setup(std::move(setup));
    update();
}

void NetLobbyScreen::change_delay(int by) {
    net::Setup setup = _session->setup();
    setup.delay      = std::max<int>(1, std::min<int>(kMaxDelay, setup.delay + by));
    _session->set_setup(std::move(setup));
}

void NetLobbyScreen::change_race(int by) {
    const net::Setup setup = _session->setup();
    const int        local = _session->local();
    const auto&      races = races_for(setup, local);
    int              i     = 0;
    while ((i < races.size()) && (races[i].name() != setup.players[local].race)) {
        ++i;
    }
    choose(races[(i + by + races.size()) % races.size()].name(), -1);
    update();
}

void NetLobbyScreen::choose_hue(int hue) {
    const net::Setup setup = _session->setup();
    const int        local = _session->local();
    if ((setup.players.size() <= local) || setup.players[local].race.empty() ||
        !allowed(setup.players[local].race, hue, other_hue(setup, local))) {
        return;
    }
    sys.sound.select();
    _session->choose(setup.players[local].race, hue);
    update();
}

// A negative `hue` picks the race's own hue.  Either way, a hue that isn't allowed is replaced
// by the next one that is.
void NetLobbyScreen::choose(pn::string_view race_name, int hue) {
    const net::Setup setup = _session->setup();
    const int        other = other_hue(setup, _session->local());
    if (hue < 0) {
        hue = static_cast<int>(race(race_name)->hue);
    }
    if (!allowed(race_name, hue, other)) {
        hue = next_hue(race_name, hue, 1, other);
    }
    _session->choose(race_name, hue);
}

void NetLobbyScreen::choose_defaults() {
    const int   local = _session->local();
    const auto& races = races_for(_session->setup(), local);
    if (!races.empty()) {
        choose(races[local % races.size()].name(), -1);
    }
}

void NetLobbyScreen::send() {
    const net::Setup setup = _session->setup();
    if ((_field.size() > 0) && (setup.players.size() > _session->local())) {
        _session->chat(_field.text());
        _messages.push_back(
                pn::format("{0}: {1}", setup.players[_session->local()].name, _field.text()));
        _field.replace({0, _field.size()}, "");
    }
    _editing = false;
    sys.video->stop_editing(&_field);
}

// Keeps the local choice valid for the level and the other player, and notes players joining and
// the other player's choice.
void NetLobbyScreen::update() {
    net::Setup setup = _session->setup();
    const int  local = _session->local();
    if (setup.players.size() <= local) {
        return;
    }

    for (int i = _players; i < setup.players.size(); ++i) {
        if (i > local) {
            _messages.push_back(pn::format("{0} joined.", setup.players[i].name));
        }
    }
    _players = setup.players.size();

    const auto& races = races_for(setup, local);
    const auto& me    = setup.players[local];
    if (!races.empty() && !has_race(races, me.race)) {
        choose_defaults();
    } else if (
            !me.race.empty() && !allowed(me.race, me.hue, local ? other_hue(setup, local) : -1)) {
        choose(me.race, me.hue);
    }

    setup = _session->setup();
    if (setup.players.size() > 1) {
        const auto& them = setup.players[1 - local];
        if (!them.race.empty() && (0 <= them.hue) && (them.hue < kHueCount)) {
            pn::string opponent = pn::format(
                    "{0} chose {1}, {2}.", them.name, race(them.race)->plural,
                    kHueNames[them.hue]);
            if (opponent != _opponent) {
                _messages.push_back(opponent.copy());
                _opponent = std::move(opponent);
            }
        }
    }
    if (_messages.size() > kMaxMessages) {
        _messages.erase(_messages.begin(), _messages.end() - kMaxMessages);
    }
}

void NetLobbyScreen::overlay() const {
    const net::Setup setup = _session->setup();
    const int        local = _session->local();
    const Point      off   = offset();

    const Rect                   level  = inner_bounds(*widget(kLevelRect), off);
    std::vector<pn::string_view> levels = net_levels();
    if (levels.empty()) {
        draw_centered(level, "No net levels", Hue::AQUA, sys.fonts.button);
    } else {
        std::vector<pn::string> titles;
        for (pn::string_view l : levels) {
            titles.push_back(level_title(l));
        }
        draw_list(level, titles, level_index(levels, setup.level), Hue::AQUA);
    }

    _field.draw(inner_bounds(*widget(kEnterTextRect), off), _editing);

    Rect messages = inner_bounds(*widget(kMessagesRect), off);
    messages.inset(3, 3);
    StyledText shown;
    pn::string text;
    for (int i = _messages.size() - 1; i >= 0; --i) {
        text              = pn::format("{0}\n{1}", _messages[i], text);
        StyledText styled = StyledText::plain(
                text, {sys.fonts.tactical, messages.width()},
                GetRGBTranslateColorShade(Hue::GREEN, LIGHTEST));
        if (styled.height() > messages.height()) {
            break;
        }
        shown = std::move(styled);
    }
    if (!shown.empty()) {
        shown.draw(messages);
    }

    draw_centered(
            inner_bounds(*widget(kLagRect), off),
            pn::format(
                    "{0} ms",
                    std::chrono::duration_cast<std::chrono::milliseconds>(kMajorTick * setup.delay)
                            .count()),
            Hue::GREEN, sys.fonts.button);

    if (setup.players.size() <= local) {
        return;
    }
    const net::Setup::Player& me    = setup.players[local];
    const auto&               races = races_for(setup, local);
    std::vector<pn::string>   names;
    for (const auto& r : races) {
        names.push_back(race(r.name())->plural.copy());
    }
    draw_list(
            inner_bounds(*widget(kSpeciesRect), off), names, race_index(races, me.race),
            Hue::SALMON);

    const Rect colors = inner_bounds(*widget(kSelectColorRect), off);
    const int  other  = other_hue(setup, local);
    for (int hue = 1; hue < kHueCount; ++hue) {
        const Rect r = swatch(colors, hue);
        if (hue == me.hue) {
            Rect frame = r;
            frame.inset(-2, -2);
            Rects().fill(frame, RgbColor::white());
        }
        const bool ok = !me.race.empty() && allowed(me.race, hue, other);
        Rects().fill(r, GetRGBTranslateColorShade(static_cast<Hue>(hue), ok ? LIGHT : VERY_DARK));
    }
}

}  // namespace antares
