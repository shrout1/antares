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

#ifndef ANTARES_NET_HOST_HPP_
#define ANTARES_NET_HOST_HPP_

#include <stdint.h>
#include <memory>
#include <pn/string>

#include "math/units.hpp"
#include "net/packet.hpp"

struct _ENetHost;

namespace antares {
namespace net {

const uint16_t kDefaultPort = 7710;

enum Channel {
    RELIABLE   = 0,
    UNRELIABLE = 1,
};

// A node's connection to its peers.  Peers are numbered from 0.
class Host {
  public:
    struct Event {
        enum Type {
            NONE,
            CONNECT,
            DISCONNECT,
            RECEIVE,
        };
        Type   type = NONE;
        int    peer = -1;
        Packet packet;
    };

    // Each returns nullptr if the socket can't be opened.  An empty `address` listens on all
    // interfaces, and port 0 on any free port.
    static std::unique_ptr<Host> listen(pn::string_view address, uint16_t port, int max_peers);
    static std::unique_ptr<Host> connect(pn::string_view address, uint16_t port);

    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    ~Host();

    uint16_t port() const;

    // Waits up to `timeout` for an event, sending queued packets meanwhile.  Packets that can't be
    // decoded are dropped.
    bool poll(usecs timeout, Event* event);

    void send(int peer, Channel channel, const Packet& packet);
    void broadcast(Channel channel, const Packet& packet);
    void disconnect(int peer);

  private:
    struct Deleter {
        void operator()(_ENetHost* host) const;
    };

    explicit Host(_ENetHost* host);

    std::unique_ptr<_ENetHost, Deleter> _host;
};

}  // namespace net
}  // namespace antares

#endif  // ANTARES_NET_HOST_HPP_
