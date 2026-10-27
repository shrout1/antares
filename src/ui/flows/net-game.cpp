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

#include <algorithm>

#include "data/level.hpp"
#include "game/globals.hpp"
#include "game/level.hpp"

namespace antares {

NetGame::NetGame(NetSetupScreen::Mode mode, pn::string_view address, uint16_t port)
        : _mode(mode), _address(address.copy()), _port(port) {}

void NetGame::become_front() {
    switch (_state) {
        case NEW:
            _state = SETUP;
            stack()->push(new NetSetupScreen(_mode, _address, _port, &_session));
            break;

        case SETUP:
            if (_session) {
                start();
            } else {
                stack()->pop(this);
            }
            break;

        case PLAYING:
            _session->quit();
            stack()->pop(this);
            break;
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
