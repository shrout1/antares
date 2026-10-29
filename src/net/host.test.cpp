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

#include "net/host.hpp"

#include <gmock/gmock.h>
#include <deque>

namespace antares {
namespace net {
namespace {

using HostTest = testing::Test;

const usecs kTimeout = std::chrono::seconds(5);

struct Node {
    std::unique_ptr<Host>   host;
    std::deque<Host::Event> events;
};

// Services both nodes until `a` has an event, and returns it.
Host::Event next(Node* a, Node* b) {
    for (usecs waited{0}; a->events.empty() && (waited < kTimeout);
         waited += std::chrono::milliseconds(10)) {
        for (Node* n : {a, b}) {
            Host::Event e;
            if (n->host->poll(std::chrono::milliseconds(5), &e)) {
                n->events.push_back(std::move(e));
            }
        }
    }
    Host::Event e;
    if (a->events.empty()) {
        ADD_FAILURE() << "timed out";
        return e;
    }
    e = std::move(a->events.front());
    a->events.pop_front();
    return e;
}

TEST_F(HostTest, Loopback) {
    Node server{Host::listen("127.0.0.1", 0, 1)};
    ASSERT_TRUE(server.host);
    ASSERT_NE(0, server.host->port());
    Node client{Host::connect("127.0.0.1", server.host->port())};
    ASSERT_TRUE(client.host);

    Host::Event e = next(&server, &client);
    ASSERT_EQ(Host::Event::CONNECT, e.type);
    const int guest = e.peer;
    e               = next(&client, &server);
    ASSERT_EQ(Host::Event::CONNECT, e.type);
    const int host = e.peer;

    Packet hello;
    hello.type           = Packet::HELLO;
    hello.hello.protocol = kProtocolVersion;
    hello.hello.name     = "Guest";
    client.host->send(host, RELIABLE, hello);
    e = next(&server, &client);
    ASSERT_EQ(Host::Event::RECEIVE, e.type);
    EXPECT_EQ(guest, e.peer);
    ASSERT_EQ(Packet::HELLO, e.packet.type);
    EXPECT_EQ("Guest", e.packet.hello.name);

    Packet turns;
    turns.type         = Packet::TURNS;
    turns.turns.player = 0;
    turns.turns.first  = 8;
    turns.turns.turns.resize(3);
    server.host->broadcast(UNRELIABLE, turns);
    e = next(&client, &server);
    ASSERT_EQ(Host::Event::RECEIVE, e.type);
    ASSERT_EQ(Packet::TURNS, e.packet.type);
    EXPECT_EQ(8u, e.packet.turns.first);
    EXPECT_EQ(3u, e.packet.turns.turns.size());

    client.host->disconnect(host);
    e = next(&server, &client);
    EXPECT_EQ(Host::Event::DISCONNECT, e.type);
    EXPECT_EQ(guest, e.peer);
}

TEST_F(HostTest, Delay) {
    Node server{Host::listen("127.0.0.1", 0, 1)};
    ASSERT_TRUE(server.host);
    Node client{Host::connect("127.0.0.1", server.host->port())};
    ASSERT_TRUE(client.host);
    ASSERT_EQ(Host::Event::CONNECT, next(&server, &client).type);
    ASSERT_EQ(Host::Event::CONNECT, next(&client, &server).type);

    client.host->set_delay(std::chrono::milliseconds(100), std::chrono::milliseconds(50), 7);
    auto sent = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < 10; ++i) {
        Packet p;
        p.type        = Packet::TURNS;
        p.turns.first = i;
        server.host->broadcast(RELIABLE, p);
    }
    for (uint32_t i = 0; i < 10; ++i) {
        Host::Event e = next(&client, &server);
        ASSERT_EQ(Host::Event::RECEIVE, e.type);
        EXPECT_EQ(i, e.packet.turns.first);  // Reliable packets stay in order.
    }
    EXPECT_GE(std::chrono::steady_clock::now() - sent, std::chrono::milliseconds(50));
}

TEST_F(HostTest, NoAddress) { EXPECT_FALSE(Host::connect("", kDefaultPort)); }

}  // namespace
}  // namespace net
}  // namespace antares
