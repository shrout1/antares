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

#ifndef ANTARES_NET_LOCKSTEP_HPP_
#define ANTARES_NET_LOCKSTEP_HPP_

#include <stdint.h>
#include <map>
#include <vector>

#include "net/packet.hpp"

namespace antares {
namespace net {

// Every player's input for each turn of a game played in lockstep.  Input made on turn t executes
// on turn t + delay; turns before first + delay have no input.
class Lockstep {
  public:
    typedef std::vector<Turns::Event> Turn;

    Lockstep(int players, int local, uint32_t first, uint32_t delay);

    Lockstep(const Lockstep&) = delete;
    Lockstep& operator=(const Lockstep&) = delete;

    int local() const { return _local; }

    // Records the local player's input from turn `at`.  Turns must be submitted in order.
    void submit(uint32_t at, Turn turn);

    // True if every player's input for turn `at` is known.
    bool ready(uint32_t at) const;

    // Each player's input for turn `at`, which must be ready.
    std::vector<Turn> take(uint32_t at);

    // Records the sync value after executing turn `at`.
    void sync(uint32_t at, uint32_t value);

    // True if any player's sync value differed from ours for the same turn.
    bool desynced() const { return _desynced; }

    // The local turns that `player` hasn't acknowledged, with our acknowledgement of theirs.
    Turns outgoing(int player) const;

    // Ignores turns from an invalid or local player.
    void receive(const Turns& turns);

  private:
    struct Player {
        std::map<uint32_t, Turn>     turns;
        uint32_t                     received;  // last turn known, with no gaps before it
        uint32_t                     acked;     // last local turn the player has acknowledged
        std::map<uint32_t, uint32_t> syncs;     // reported but not yet compared
    };

    const int                    _local;
    const uint32_t               _delay;
    std::vector<Player>          _players;
    uint32_t                     _executed;
    std::map<uint32_t, uint32_t> _syncs;  // ours, recent
    uint32_t                     _sync_turn = 0;
    uint32_t                     _sync      = 0;
    bool                         _desynced  = false;
};

}  // namespace net
}  // namespace antares

#endif  // ANTARES_NET_LOCKSTEP_HPP_
