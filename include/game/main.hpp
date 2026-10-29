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

#ifndef ANTARES_GAME_MAIN_HPP_
#define ANTARES_GAME_MAIN_HPP_

#include "data/replay.hpp"
#include "ui/card.hpp"
#include "ui/interface-handling.hpp"

namespace antares {

class InputSource;
union Level;

namespace net {
class Session;
}  // namespace net

// For testing rollback. Writes a line to `log` after each major tick, identifying the state of
// the world. With `lookahead`, the game also simulates that many major ticks ahead before each
// major tick, with other controls held and no other input, then copies the world back, so that
// state the copy misses changes the log.
void set_rollback_test(pn::string_view log, int lookahead);

// For testing net games. Writes a line to `log` after each turn that every player's input has
// reached, identifying the state of the world, and ends the game after `last_turn`.
void set_net_test(pn::string_view log, uint32_t last_turn);

// Identifies how this build plays: plays each two-player net level for a while with scripted input
// and hashes the result. Builds that agree on it play net games alike.
uint32_t sim_fingerprint();

// True if `level` has conditions on local state, which differs between players (not net-safe).
bool uses_local_conditions(const Level& level);

enum GameResult {
    NO_GAME      = -1,
    LOSE_GAME    = 0,
    WIN_GAME     = 1,
    RESTART_GAME = 2,
    QUIT_GAME    = 3,
};

class MainPlay : public Card {
  public:
    MainPlay(
            const Level& level, bool replay, InputSource* input, bool show_loading_screen,
            GameResult* game_result, net::Session* session = nullptr);

    virtual void become_front();

  private:
    enum State {
        NEW,
        LOADING,
        BRIEFING,
        PLAYING,
    };
    State _state;

    const Level&        _level;
    const bool          _replay;
    const bool          _show_loading_screen;
    bool                _cancelled;
    GameResult* const   _game_result;
    InputSource*        _input_source;
    net::Session* const _session;
};

}  // namespace antares

#endif  // ANTARES_GAME_MAIN_HPP_
