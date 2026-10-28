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

#include "ui/flows/net-game.hpp"

#include <stdlib.h>
#include <algorithm>
#include <pn/output>

#include "build/defs.hpp"
#include "data/level.hpp"
#include "data/plugin.hpp"
#include "game/globals.hpp"
#include "game/level.hpp"
#include "game/time.hpp"

namespace antares {

namespace {

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

}  // namespace

NetGame::NetGame(Mode mode, pn::string_view address, uint16_t port)
        : _mode(mode), _address(address.copy()), _port(port) {}

void NetGame::become_front() {
    switch (_state) {
        case NEW:
            switch (_mode) {
                case ASK: ask(); break;
                case HOST: host(); break;
                case JOIN:
                    if (_address.empty()) {
                        ask_address();
                    } else {
                        join(_address);
                    }
                    break;
            }
            break;

        case ASKING:
            switch (_choice) {
                case HostOrJoinScreen::HOST: host(); break;
                case HostOrJoinScreen::JOIN: ask_address(); break;
                case HostOrJoinScreen::CANCEL: stack()->pop(this); break;
            }
            break;

        case ADDRESS:
            if (_cancelled) {
                back();
            } else {
                join(_address);
            }
            break;

        case CONNECTING:
            if (_session->state() == net::Session::LOBBY) {
                lobby();
            } else {
                _session->quit();
                _session.reset();
                back();
            }
            break;

        case LOBBY:
            switch (_session->state()) {
                case net::Session::STARTED:
                case net::Session::PLAYING: start(); break;
                case net::Session::CLOSED: fail(_session->error()); break;
                default:
                    _session->quit();
                    _session.reset();
                    back();
                    break;
            }
            break;

        case FAILED: back(); break;

        case PLAYING:
            _session->quit();
            stack()->pop(this);
            break;
    }
}

void NetGame::ask() {
    _state = ASKING;
    stack()->push(new HostOrJoinScreen(&_choice));
}

void NetGame::ask_address() {
    _state = ADDRESS;
    stack()->push(new NetAddressScreen(&_cancelled, &_address));
}

void NetGame::host() {
    net::Setup setup;
    setup.global_seed = static_cast<int32_t>(now().time_since_epoch().count());
    setup.delay       = net::kDefaultDelay;
    setup.players.emplace_back();
    setup.players[0].name = player_name();

    _session = net::Session::host("", _port, hello(), std::move(setup));
    if (!_session) {
        fail(pn::format("Couldn’t host a game on port {0}.", _port));
        return;
    }
    lobby();
}

void NetGame::join(pn::string_view address) {
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

    _session = net::Session::join(host, port, hello());
    if (!_session) {
        fail(pn::format("Couldn’t find “{0}”.", host));
        return;
    }
    _state = CONNECTING;
    stack()->push(new NetStatusScreen(_session.get(), pn::format("Connecting to {0}…", address)));
}

void NetGame::lobby() {
    _state = LOBBY;
    stack()->push(new NetLobbyScreen(_session.get()));
}

void NetGame::fail(pn::string_view error) {
    if (_session) {
        _session->quit();
        _session.reset();
    }
    _state = FAILED;
    stack()->push(new NetStatusScreen(nullptr, error));
}

void NetGame::back() {
    if (_mode == ASK) {
        ask();
    } else {
        stack()->pop(this);
    }
}

void NetGame::start() {
    net::Setup   setup = _session->setup();
    const Level* level = Level::get(setup.level);
    if (!level || (level->type() != Level::Type::NET) ||
        (level->net.players.size() != setup.players.size())) {
        _session->quit();
        stack()->pop(this);
        return;
    }

    std::vector<NetPlayer> players;
    for (int i = 0; i < setup.players.size(); ++i) {
        const net::Setup::Player& p     = setup.players[i];
        const auto&               races = level->net.players[i].races;
        NetPlayer                 player;
        player.name = p.name.copy();
        player.race = NamedHandle<const Race>(p.race);
        auto same   = [&p](const NamedHandle<const Race>& r) { return r.name() == p.race; };
        if (!races.empty() && std::none_of(races.begin(), races.end(), same)) {
            player.race = races[0].copy();
        }
        player.hue = ((0 < p.hue) && (p.hue <= static_cast<int>(Hue::RED)))
                             ? static_cast<Hue>(p.hue)
                             : Hue::GRAY;
        players.push_back(std::move(player));
    }
    set_net_players(std::move(players), _session->local());
    g.random.seed = setup.global_seed;

    _state       = PLAYING;
    _game_result = NO_GAME;
    stack()->push(
            new MainPlay(*level, false, &_input_source, true, &_game_result, _session.get()));
}

}  // namespace antares
