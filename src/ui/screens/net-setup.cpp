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

#include <stdlib.h>
#include <algorithm>
#include <pn/output>

#include "build/defs.hpp"
#include "data/level.hpp"
#include "data/plugin.hpp"
#include "data/races.hpp"
#include "drawing/color.hpp"
#include "drawing/shapes.hpp"
#include "drawing/text.hpp"
#include "game/globals.hpp"
#include "game/sys.hpp"
#include "game/time.hpp"
#include "video/driver.hpp"

namespace antares {

namespace {

const usecs   kPollInterval = std::chrono::milliseconds(100);
const int32_t kWidth        = 500;

const char* const kHueNames[] = {
        "Gray", "Orange", "Yellow", "Blue",       "Green",       "Purple",   "Indigo", "Salmon",
        "Gold", "Aqua",   "Pink",   "Pale Green", "Pale Purple", "Sky Blue", "Tan",    "Red",
};
const int kHueCount = 16;

std::vector<pn::string_view> net_levels() {
    std::vector<pn::string_view> names;
    for (const auto& kv : plug.levels) {
        if (kv.second.type() == Level::Type::NET) {
            names.push_back(kv.first);
        }
    }
    return names;
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

pn::string player_name() {
    for (const char* var : {"USERNAME", "USER"}) {
        const char* name = getenv(var);
        if (name && *name) {
            return pn::string{name};
        }
    }
    return "Player";
}

net::Hello hello() {
    net::Hello h;
    h.protocol          = net::kProtocolVersion;
    h.antares_version   = kAntaresVersion;
    h.plugin_identifier = plug.info.identifier.hash.copy();
    h.plugin_version    = plug.info.version.copy();
    h.name              = player_name();
    return h;
}

void draw_line(Point* at, pn::string_view text, Hue hue, const Font& font) {
    font.draw(Point(at->h, at->v + font.ascent), text, GetRGBTranslateColorShade(hue, LIGHTEST));
    at->v += font.height + 6;
}

}  // namespace

NetSetupScreen::NetSetupScreen(
        Mode mode, pn::string_view address, uint16_t port, std::unique_ptr<net::Session>* session)
        : _mode(mode),
          _port(port),
          _address(address.copy()),
          _session(session),
          _address_text(this),
          _next_timer(now()) {}

void NetSetupScreen::become_front() {
    switch (_mode) {
        case ASK: _state = ASKING; break;
        case HOST: host(); break;
        case JOIN:
            if (_address.empty()) {
                edit_address();
            } else {
                join(_address);
            }
            break;
    }
}

void NetSetupScreen::resign_front() { sys.video->stop_editing(&_address_text); }

void NetSetupScreen::host() {
    net::Setup                   setup;
    std::vector<pn::string_view> levels = net_levels();
    if (levels.empty()) {
        fail("This scenario has no net levels.");
        return;
    }
    setup.level       = levels[0].copy();
    setup.global_seed = static_cast<int32_t>(now().time_since_epoch().count());
    setup.delay       = net::kDefaultDelay;
    setup.players.emplace_back();
    setup.players[0].name = player_name();

    *_session = net::Session::host("", _port, hello(), std::move(setup));
    if (!*_session) {
        fail(pn::format("Couldn’t host a game on port {0}.", _port));
        return;
    }
    _state = LOBBY;
    _field = LEVEL;
    choose_defaults();
}

void NetSetupScreen::join(pn::string_view address) {
    pn::string_view host  = address;
    uint16_t        port  = _port;
    int             colon = pn::string_view::npos;
    for (int i = address.find(":"); i != pn::string_view::npos; i = address.find(":", i + 1)) {
        colon = i;
    }
    if (colon != pn::string_view::npos) {
        host = address.substr(0, colon);
        int64_t p;
        if (!pn::strtoll(address.substr(colon + 1), &p, nullptr) || (p <= 0) || (p > 65535)) {
            fail(pn::format("“{0}” isn’t a valid port.", address.substr(colon + 1)));
            return;
        }
        port = p;
    }

    _address  = address.copy();
    *_session = net::Session::join(host, port, hello());
    if (!*_session) {
        fail(pn::format("Couldn’t find “{0}”.", host));
        return;
    }
    _state = CONNECTING;
    _field = RACE;
    _chose = false;
}

void NetSetupScreen::fail(pn::string_view error) {
    sys.video->stop_editing(&_address_text);
    _session->reset();
    _error = error.copy();
    _state = FAILED;
}

void NetSetupScreen::edit_address() {
    _state = ADDRESS;
    if (sys.video->start_editing(&_address_text)) {
        _address_text.replace({0, _address_text.size()}, _address);
    }
}

void NetSetupScreen::choose_defaults() {
    net::Setup  setup = (*_session)->setup();
    int         local = (*_session)->local();
    const auto& races = races_for(setup, local);
    if (races.empty() || (setup.players.size() <= local)) {
        return;
    }
    pn::string_view race_name = races[local % races.size()].name();
    int             other     = (setup.players.size() > 1) ? setup.players[1 - local].hue : -1;
    int             hue       = static_cast<int>(race(race_name)->hue);
    if (!allowed(race_name, hue, other)) {
        hue = next_hue(race_name, hue, 1, other);
    }
    (*_session)->choose(race_name, hue);
    _chose = true;
}

void NetSetupScreen::change(int by) {
    net::Setup setup = (*_session)->setup();
    int        local = (*_session)->local();
    if (setup.players.size() <= local) {
        return;
    }
    const net::Setup::Player& me = setup.players[local];
    int other                    = (setup.players.size() > 1) ? setup.players[1 - local].hue : -1;

    switch (_field) {
        case LEVEL: {
            std::vector<pn::string_view> levels = net_levels();
            auto it     = std::find(levels.begin(), levels.end(), pn::string_view{setup.level});
            int  i      = (it == levels.end()) ? 0 : (it - levels.begin());
            setup.level = levels[(i + by + levels.size()) % levels.size()].copy();
            (*_session)->set_setup(std::move(setup));
            choose_defaults();
        } break;

        case RACE: {
            const auto& races = races_for(setup, local);
            if (races.empty()) {
                return;
            }
            int i = 0;
            while ((i < races.size()) && (races[i].name() != me.race)) {
                ++i;
            }
            pn::string_view race_name = races[(i + by + races.size()) % races.size()].name();
            int             hue       = static_cast<int>(race(race_name)->hue);
            if (!allowed(race_name, hue, other)) {
                hue = next_hue(race_name, hue, 1, other);
            }
            (*_session)->choose(race_name, hue);
        } break;

        case COLOR: (*_session)->choose(me.race, next_hue(me.race, me.hue, by, other)); break;
    }
}

bool NetSetupScreen::next_timer(wall_time& time) {
    time = _next_timer;
    return true;
}

void NetSetupScreen::fire_timer() {
    _next_timer = now() + kPollInterval;
    if (!*_session) {
        return;
    }
    switch ((*_session)->state()) {
        case net::Session::CONNECTING: break;
        case net::Session::LOBBY:
            _state = LOBBY;
            if (!_chose && (*_session)->has_peer()) {
                choose_defaults();
            }
            break;
        case net::Session::STARTED:
        case net::Session::PLAYING: stack()->pop(this); break;
        case net::Session::CLOSED: fail((*_session)->error()); break;
    }
}

void NetSetupScreen::key_down(const KeyDownEvent& event) {
    const bool is_host = *_session && (*_session)->is_host();
    switch (_state) {
        case ASKING:
            if (event.key() == Key::H) {
                host();
            } else if (event.key() == Key::J) {
                edit_address();
            } else if (event.key() == Key::ESCAPE) {
                stack()->pop(this);
            }
            break;

        case ADDRESS: break;

        case CONNECTING:
        case LOBBY:
            switch (event.key().value()) {
                case Key::ESCAPE:
                    (*_session)->quit();
                    _session->reset();
                    if (_mode == ASK) {
                        _state = ASKING;
                    } else {
                        stack()->pop(this);
                    }
                    break;
                case Key::UP_ARROW:
                    _field = static_cast<Field>(std::max<int>(is_host ? LEVEL : RACE, _field - 1));
                    break;
                case Key::DOWN_ARROW:
                    _field = static_cast<Field>(std::min<int>(COLOR, _field + 1));
                    break;
                case Key::LEFT_ARROW: change(-1); break;
                case Key::RIGHT_ARROW: change(+1); break;
                case Key::RETURN:
                    if (is_host) {
                        (*_session)->start();
                    }
                    break;
                default: break;
            }
            break;

        case FAILED:
            if (_mode == ASK) {
                _state = ASKING;
            } else {
                stack()->pop(this);
            }
            break;
    }
}

void NetSetupScreen::draw() const {
    Rects().fill(world(), RgbColor::black());
    Point       at(world().center().h - (kWidth / 2), world().center().v - 150);
    const Font& title  = sys.fonts.title;
    const Font& button = sys.fonts.button;
    draw_line(&at, "Network Game", Hue::GOLD, title);
    at.v += 12;

    switch (_state) {
        case ASKING:
            draw_line(
                    &at, pn::format("H - Host a game on port {0}", _port), Hue::SKY_BLUE, button);
            draw_line(&at, "J - Join a game", Hue::SKY_BLUE, button);
            draw_line(&at, "ESC - Cancel", Hue::GRAY, button);
            break;

        case ADDRESS:
            draw_line(&at, "Host address:", Hue::SKY_BLUE, button);
            _address_text.draw(at);
            at.v += sys.fonts.tactical.height + 12;
            draw_line(&at, "RETURN - Join     ESC - Cancel", Hue::GRAY, button);
            break;

        case CONNECTING:
            draw_line(&at, pn::format("Connecting to {0}...", _address), Hue::SKY_BLUE, button);
            draw_line(&at, "ESC - Cancel", Hue::GRAY, button);
            break;

        case LOBBY: {
            const net::Setup setup   = (*_session)->setup();
            const int        local   = (*_session)->local();
            const bool       is_host = (*_session)->is_host();
            auto             mark    = [this](Field f) { return (_field == f) ? "> " : "  "; };

            draw_line(
                    &at, pn::format("{0}Level: {1}", mark(LEVEL), level_title(setup.level)),
                    is_host ? Hue::SKY_BLUE : Hue::GRAY, button);
            at.v += 6;
            for (int i = 0; i < setup.players.size(); ++i) {
                const net::Setup::Player& p   = setup.players[i];
                Hue                       hue = Hue::GRAY;
                if ((0 <= p.hue) && (p.hue < kHueCount)) {
                    hue = static_cast<Hue>(p.hue);
                }
                pn::string_view race_name =
                        p.race.empty() ? pn::string_view{"?"} : race(p.race)->plural;
                draw_line(
                        &at,
                        pn::format(
                                "{0}{1}: {2}, {3}", (i == local) ? "* " : "  ", p.name, race_name,
                                kHueNames[static_cast<int>(hue)]),
                        hue, button);
            }
            if (setup.players.size() < net::kSessionPlayers) {
                draw_line(
                        &at,
                        pn::format(
                                "  Waiting for a player to join on port {0}...",
                                (*_session)->port()),
                        Hue::GRAY, button);
            }
            at.v += 6;
            draw_line(&at, pn::format("{0}Race", mark(RACE)), Hue::SKY_BLUE, button);
            draw_line(&at, pn::format("{0}Color", mark(COLOR)), Hue::SKY_BLUE, button);
            at.v += 12;
            draw_line(&at, "UP/DOWN - Select     LEFT/RIGHT - Change", Hue::GRAY, button);
            if (is_host) {
                draw_line(
                        &at,
                        (*_session)->has_peer() ? "RETURN - Start     ESC - Leave" : "ESC - Leave",
                        Hue::GRAY, button);
            } else {
                draw_line(&at, "Waiting for the host to start     ESC - Leave", Hue::GRAY, button);
            }
        } break;

        case FAILED:
            draw_line(&at, _error, Hue::RED, button);
            draw_line(&at, "Press any key", Hue::GRAY, button);
            break;
    }
}

NetSetupScreen::AddressText::AddressText(NetSetupScreen* screen)
        : EditableText("", ""), _screen(screen) {}

void NetSetupScreen::AddressText::accept() {
    sys.video->stop_editing(this);
    _screen->join(text());
}

void NetSetupScreen::AddressText::escape() {
    sys.video->stop_editing(this);
    if (_screen->_mode == ASK) {
        _screen->_state = ASKING;
    } else {
        _screen->stack()->pop(_screen);
    }
}

void NetSetupScreen::AddressText::update(
        pn::string_view text, range<int> selection, range<int> mark) {
    _text = StyledText::plain(
            text, {sys.fonts.tactical, kWidth}, GetRGBTranslateColorShade(Hue::GREEN, LIGHTEST));
    _text.select(selection.begin, selection.end);
    _text.mark(mark.begin, mark.end);
}

void NetSetupScreen::AddressText::draw(Point at) const {
    Rect bounds(at.h, at.v, at.h + kWidth, at.v + sys.fonts.tactical.height);
    _text.draw(bounds);
    _text.draw_cursor(bounds, GetRGBTranslateColorShade(Hue::GREEN, LIGHTEST));
}

}  // namespace antares
