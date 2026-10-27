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

#ifndef ANTARES_NET_SESSION_HPP_
#define ANTARES_NET_SESSION_HPP_

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "net/host.hpp"
#include "net/lockstep.hpp"
#include "net/packet.hpp"

namespace antares {
namespace net {

const int      kSessionPlayers = 2;
const uint32_t kDefaultDelay   = 4;

// A two-player net game, from connecting through play.  Its connection is serviced on a thread of
// its own; every method may be called from any one other thread.
class Session {
  public:
    enum State {
        CONNECTING,  // client only: waiting for the host's Welcome
        LOBBY,       // exchanging Setup and Choose
        STARTED,     // the host started; loading and briefing
        PLAYING,     // every player is ready
        CLOSED,
    };

    // `hello` describes the local node and player; the host also uses it to vet clients, and
    // `setup` holds the host as its only player.  Each returns nullptr if the socket can't be
    // opened.  An empty `address` listens on all interfaces.
    static std::unique_ptr<Session> host(
            pn::string_view address, uint16_t port, Hello hello, Setup setup);
    static std::unique_ptr<Session> join(pn::string_view address, uint16_t port, Hello hello);

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    ~Session();

    bool       is_host() const { return _is_host; }
    uint16_t   port() const { return _host->port(); }
    State      state() const;
    pn::string error() const;  // why the session closed
    int        local() const;  // the local player's index, once known
    Setup      setup() const;
    bool       has_peer() const;

    // Host: changes the level or delay, keeping the players.
    void set_setup(Setup setup);
    // Either: changes the local player's race and color.
    void choose(pn::string_view race, int32_t hue);
    // Host: starts loading, once a peer has joined.
    void start();
    // After the briefing: `first` is the first turn of play.
    void ready(uint32_t first);
    // Sends Quit and closes.
    void quit();

    // While playing; see Lockstep.  No turn is ready until every player is.
    void                        submit(uint32_t at, Lockstep::Turn turn);
    bool                        ready_turn(uint32_t at) const;
    std::vector<Lockstep::Turn> take(uint32_t at);
    void                        sync(uint32_t at, uint32_t value);
    bool                        desynced() const;

    void                    chat(pn::string_view text);
    std::vector<pn::string> take_chats();

  private:
    Session(std::unique_ptr<Host> host, bool is_host, Hello hello);

    void run();
    void handle(const Host::Event& e);
    void handle_hello(int peer, const Hello& hello);
    void receive(const Packet& p);
    void close(pn::string_view error);
    void send(Channel channel, Packet p);
    void send_setup();

    std::unique_ptr<Host> _host;
    const bool            _is_host;
    const Hello           _hello;

    mutable std::mutex                      _mutex;
    State                                   _state = CONNECTING;
    pn::string                              _error;
    int                                     _local = 0;
    int                                     _peer  = -1;
    Setup                                   _setup;
    bool                                    _peer_ready = false;
    std::unique_ptr<Lockstep>               _lockstep;
    std::vector<std::pair<Channel, Packet>> _outbox;
    std::vector<pn::string>                 _chats;
    bool                                    _turns_dirty = false;

    std::atomic<bool> _stop;
    std::thread       _thread;
};

}  // namespace net
}  // namespace antares

#endif  // ANTARES_NET_SESSION_HPP_
