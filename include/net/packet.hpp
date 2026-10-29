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

#ifndef ANTARES_NET_PACKET_HPP_
#define ANTARES_NET_PACKET_HPP_

#include <stdint.h>
#include <pn/data>
#include <pn/string>
#include <vector>

namespace antares {
namespace net {

const uint32_t kProtocolVersion = 3;

// See proto/net.proto.
struct Hello {
    uint32_t   protocol = 0;
    pn::string antares_version;
    pn::string plugin_identifier;
    pn::string plugin_version;
    pn::string name;
    uint32_t   fingerprint = 0;  // from sim_fingerprint()

    Hello copy() const;
};

struct Welcome {
    int32_t player = 0;
};

struct Reject {
    enum Reason {
        PROTOCOL   = 0,
        PLUGIN     = 2,
        FULL       = 3,
        SIMULATION = 4,
    };
    Reason reason = PROTOCOL;
};

struct Setup {
    struct Player {
        pn::string name;
        pn::string race;
        int32_t    hue = 0;
    };
    pn::string          level;
    int32_t             global_seed = 0;
    uint32_t            delay       = 0;
    std::vector<Player> players;

    Setup copy() const;
};

struct Turns {
    struct Event {
        uint32_t type = 0;
        int32_t  data = 0;
    };
    int32_t                         player = 0;
    uint32_t                        first  = 0;
    std::vector<std::vector<Event>> turns;
    uint32_t                        ack       = 0;
    uint32_t                        sync_turn = 0;
    uint32_t                        sync      = 0;
    int32_t                         lead      = 0;
};

struct Chat {
    int32_t    player = 0;
    pn::string text;
};

struct Quit {
    int32_t player = 0;
};

struct Choose {
    pn::string race;
    int32_t    hue = 0;
};

// Only the member named by `type` is meaningful.
struct Packet {
    enum Type {
        NONE    = 0,
        HELLO   = 1,
        WELCOME = 2,
        REJECT  = 3,
        SETUP   = 4,
        START   = 5,
        TURNS   = 6,
        CHAT    = 7,
        QUIT    = 8,
        CHOOSE  = 9,
        READY   = 10,
    };
    Type    type = NONE;
    Hello   hello;
    Welcome welcome;
    Reject  reject;
    Setup   setup;
    Turns   turns;
    Chat    chat;
    Quit    quit;
    Choose  choose;
};

pn::data encode(const Packet& packet);
bool     decode(pn::data_view bytes, Packet* packet);

}  // namespace net
}  // namespace antares

#endif  // ANTARES_NET_PACKET_HPP_
