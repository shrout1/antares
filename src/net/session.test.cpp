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

#include "net/session.hpp"

#include <gmock/gmock.h>
#include <functional>

namespace antares {
namespace net {
namespace {

using SessionTest = testing::Test;

Hello hello(pn::string_view name, pn::string_view version = "1.0") {
    Hello h;
    h.protocol          = kProtocolVersion;
    h.antares_version   = version.copy();
    h.plugin_identifier = "factory";
    h.plugin_version    = "1.1.1";
    h.name              = name.copy();
    return h;
}

Setup setup() {
    Setup s;
    s.level       = "net1";
    s.global_seed = 1234;
    s.delay       = 2;
    s.players.emplace_back();
    s.players.back().name = "Host";
    s.players.back().race = "ish";
    s.players.back().hue  = 1;
    return s;
}

bool eventually(std::function<bool()> f) {
    for (int i = 0; i < 500; ++i) {
        if (f()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

TEST_F(SessionTest, Game) {
    std::unique_ptr<Session> host = Session::host("127.0.0.1", 0, hello("Host"), setup());
    ASSERT_TRUE(host);
    std::unique_ptr<Session> guest = Session::join("127.0.0.1", host->port(), hello("Guest"));
    ASSERT_TRUE(guest);

    ASSERT_TRUE(eventually([&] { return guest->has_peer(); }));
    EXPECT_EQ(Session::LOBBY, guest->state());
    EXPECT_EQ(1, guest->local());
    EXPECT_EQ("Guest", guest->setup().players[1].name);
    EXPECT_EQ(1234, guest->setup().global_seed);

    guest->choose("can", 5);
    ASSERT_TRUE(eventually([&] { return host->setup().players[1].race == "can"; }));
    EXPECT_EQ(5, host->setup().players[1].hue);

    host->start();
    ASSERT_TRUE(eventually([&] { return guest->state() == Session::STARTED; }));
    host->ready(10);
    EXPECT_EQ(Session::STARTED, host->state());
    guest->ready(10);
    ASSERT_TRUE(eventually([&] { return host->state() == Session::PLAYING; }));
    ASSERT_TRUE(eventually([&] { return guest->state() == Session::PLAYING; }));

    Turns::Event build;
    build.type = 0x50;
    build.data = 3;
    for (uint32_t t = 10; t < 20; ++t) {
        host->submit(t, {build});
        guest->submit(t, {});
        ASSERT_TRUE(eventually([&] { return host->ready_turn(t) && guest->ready_turn(t); }));
        std::vector<Lockstep::Turn> x = host->take(t), y = guest->take(t);
        ASSERT_EQ(x[0].size(), y[0].size());
        ASSERT_EQ(x[1].size(), y[1].size());
        host->sync(t, t * 7);
        guest->sync(t, t * 7);
    }
    EXPECT_FALSE(host->desynced());
    EXPECT_FALSE(guest->desynced());

    ASSERT_TRUE(eventually([&] { return host->confirmed() == 21; }));
    host->submit(20, {build});
    EXPECT_EQ(21u, host->confirmed());  // The guest hasn't sent turn 22.
    ASSERT_TRUE(host->known(0, 22).has_value());
    EXPECT_EQ(3, (*host->known(0, 22))[0].data);
    EXPECT_FALSE(host->known(1, 22).has_value());
    guest->submit(20, {});
    ASSERT_TRUE(eventually([&] { return host->confirmed() == 22; }));
    ASSERT_TRUE(host->known(1, 22).has_value());
    host->confirm(22);
    EXPECT_FALSE(host->known(0, 22).has_value());

    host->chat("gg");
    std::vector<pn::string> chats;
    ASSERT_TRUE(eventually([&] {
        chats = guest->take_chats();
        return !chats.empty();
    }));
    EXPECT_EQ("Host: gg", chats[0]);

    guest->quit();
    ASSERT_TRUE(eventually([&] { return host->state() == Session::CLOSED; }));
    EXPECT_EQ("The other player quit.", host->error());
}

TEST_F(SessionTest, RejectsOtherVersions) {
    std::unique_ptr<Session> host = Session::host("127.0.0.1", 0, hello("Host"), setup());
    ASSERT_TRUE(host);
    std::unique_ptr<Session> guest =
            Session::join("127.0.0.1", host->port(), hello("Guest", "2.0"));
    ASSERT_TRUE(guest);
    ASSERT_TRUE(eventually([&] { return guest->state() == Session::CLOSED; }));
    EXPECT_EQ("The host runs a different version of Antares.", guest->error());
    EXPECT_FALSE(host->has_peer());
}

}  // namespace
}  // namespace net
}  // namespace antares
