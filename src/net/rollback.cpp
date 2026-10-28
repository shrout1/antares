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

#include "net/rollback.hpp"

#include <algorithm>
#include <stdexcept>

namespace antares {
namespace net {

namespace {

const size_t   kMaxTurnsPerPacket = 64;
const uint32_t kMaxTurnsAhead     = 1024;
const size_t   kMaxSyncs          = 256;

void trim(std::map<uint32_t, uint32_t>* syncs) {
    while (syncs->size() > kMaxSyncs) {
        syncs->erase(syncs->begin());
    }
}

}  // namespace

Rollback::Rollback(int players, int local, uint32_t first, uint32_t delay)
        : _local(local), _delay(delay), _players(players), _executed(first - 1) {
    if ((local < 0) || (players <= local) || (first == 0)) {
        throw std::runtime_error("invalid rollback");
    }
    for (Player& p : _players) {
        for (uint32_t t = first; t < first + delay; ++t) {
            p.turns[t];
        }
        p.received = p.acked = first + delay - 1;
    }
}

void Rollback::submit(uint32_t at, Turn turn) {
    Player& me = _players[_local];
    if (at + _delay != me.received + 1) {
        throw std::runtime_error("turn submitted out of order");
    }
    me.turns[++me.received] = std::move(turn);
}

uint32_t Rollback::confirmed() const {
    uint32_t at = _players[0].received;
    for (const Player& p : _players) {
        at = std::min(at, p.received);
    }
    return at;
}

const Rollback::Turn* Rollback::known(int player, uint32_t at) const {
    if ((player < 0) || (_players.size() <= player) || (at <= _executed)) {
        return nullptr;
    }
    auto it = _players[player].turns.find(at);
    if ((it == _players[player].turns.end()) || (_players[player].received < at)) {
        return nullptr;
    }
    return &it->second;
}

void Rollback::confirm(uint32_t at) {
    if ((confirmed() < at) || (at != _executed + 1)) {
        throw std::runtime_error("turn confirmed out of order");
    }
    _executed = at;

    uint32_t acked = at;
    for (int i = 0; i < _players.size(); ++i) {
        if (i != _local) {
            acked = std::min(acked, _players[i].acked);
        }
    }
    for (int i = 0; i < _players.size(); ++i) {
        auto& turns = _players[i].turns;
        turns.erase(turns.begin(), turns.upper_bound((i == _local) ? acked : at));
    }
}

void Rollback::sync(uint32_t at, uint32_t value) {
    _syncs[at] = value;
    trim(&_syncs);
    _sync_turn = at;
    _sync      = value;
    for (Player& p : _players) {
        auto end = p.syncs.upper_bound(at);
        for (auto it = p.syncs.begin(); it != end; ++it) {
            auto ours = _syncs.find(it->first);
            _desynced = _desynced || ((ours != _syncs.end()) && (ours->second != it->second));
        }
        p.syncs.erase(p.syncs.begin(), end);
    }
}

Turns Rollback::outgoing(int player) const {
    if ((player < 0) || (_players.size() <= player) || (player == _local)) {
        throw std::runtime_error("invalid player");
    }
    const Player& me   = _players[_local];
    const Player& them = _players[player];

    Turns t;
    t.player = _local;
    t.first  = them.acked + 1;
    for (uint32_t n = t.first; (n <= me.received) && (t.turns.size() < kMaxTurnsPerPacket); ++n) {
        t.turns.push_back(me.turns.at(n));
    }
    t.ack       = them.received;
    t.sync_turn = _sync_turn;
    t.sync      = _sync;
    return t;
}

void Rollback::receive(const Turns& t) {
    if ((t.player < 0) || (_players.size() <= t.player) || (t.player == _local)) {
        return;
    }
    Player& them = _players[t.player];

    for (size_t i = 0; i < t.turns.size(); ++i) {
        uint32_t n = t.first + i;
        if ((them.received < n) && (n <= them.received + kMaxTurnsAhead)) {
            them.turns.emplace(n, t.turns[i]);
        }
    }
    while (them.turns.find(them.received + 1) != them.turns.end()) {
        ++them.received;
    }

    if ((them.acked < t.ack) && (t.ack <= _players[_local].received)) {
        them.acked = t.ack;
    }

    if (t.sync_turn > 0) {
        auto ours = _syncs.find(t.sync_turn);
        if (ours != _syncs.end()) {
            _desynced = _desynced || (ours->second != t.sync);
        } else if (t.sync_turn > _sync_turn) {
            them.syncs[t.sync_turn] = t.sync;
            trim(&them.syncs);
        }
    }
}

}  // namespace net
}  // namespace antares
