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

#include "net/packet.hpp"

#include <gmock/gmock.h>
#include <stdexcept>

namespace antares {
namespace net {
namespace {

using PacketTest = testing::Test;

Packet round_trip(const Packet& in) {
    Packet out;
    EXPECT_TRUE(decode(encode(in), &out));
    EXPECT_EQ(in.type, out.type);
    return out;
}

Turns::Event event(uint32_t type, int32_t data) {
    Turns::Event e;
    e.type = type;
    e.data = data;
    return e;
}

pn::data bytes(std::initializer_list<uint8_t> b) {
    pn::data d;
    for (uint8_t byte : b) {
        d += pn::data_view{&byte, 1};
    }
    return d;
}

TEST_F(PacketTest, Hello) {
    Packet in;
    in.type                    = Packet::HELLO;
    in.hello.protocol          = kProtocolVersion;
    in.hello.antares_version   = "0.9.1";
    in.hello.plugin_identifier = "4cab7415715aeeacf1486a352267ae82c0efb220";
    in.hello.plugin_version    = "1.1.1";
    in.hello.name              = "Ishiman Admiral \xc3\xa9";
    in.hello.fingerprint       = 0xdeadbeef;
    Packet out                 = round_trip(in);
    EXPECT_EQ(kProtocolVersion, out.hello.protocol);
    EXPECT_EQ("0.9.1", out.hello.antares_version);
    EXPECT_EQ("4cab7415715aeeacf1486a352267ae82c0efb220", out.hello.plugin_identifier);
    EXPECT_EQ("1.1.1", out.hello.plugin_version);
    EXPECT_EQ(0xdeadbeef, out.hello.fingerprint);
    EXPECT_EQ("Ishiman Admiral \xc3\xa9", out.hello.name);
}

TEST_F(PacketTest, Lobby) {
    Packet welcome;
    welcome.type           = Packet::WELCOME;
    welcome.welcome.player = 1;
    EXPECT_EQ(1, round_trip(welcome).welcome.player);

    Packet reject;
    reject.type          = Packet::REJECT;
    reject.reject.reason = Reject::PLUGIN;
    EXPECT_EQ(Reject::PLUGIN, round_trip(reject).reject.reason);

    Packet start;
    start.type = Packet::START;
    round_trip(start);

    Packet quit;
    quit.type        = Packet::QUIT;
    quit.quit.player = 1;
    EXPECT_EQ(1, round_trip(quit).quit.player);

    Packet choose;
    choose.type        = Packet::CHOOSE;
    choose.choose.race = "can";
    choose.choose.hue  = 5;
    Packet out         = round_trip(choose);
    EXPECT_EQ("can", out.choose.race);
    EXPECT_EQ(5, out.choose.hue);

    Packet ready;
    ready.type = Packet::READY;
    round_trip(ready);
}

TEST_F(PacketTest, Setup) {
    Packet in;
    in.type              = Packet::SETUP;
    in.setup.level       = "net2";
    in.setup.global_seed = -12345;
    in.setup.delay       = 8;
    in.setup.players.emplace_back();
    in.setup.players.back().name = "Host";
    in.setup.players.back().race = "ishiman";
    in.setup.players.back().hue  = 3;
    in.setup.players.emplace_back();
    in.setup.players.back().name = "Guest";
    in.setup.players.back().race = "cantharan";
    in.setup.players.back().hue  = 7;

    Packet out = round_trip(in);
    EXPECT_EQ("net2", out.setup.level);
    EXPECT_EQ(-12345, out.setup.global_seed);
    EXPECT_EQ(8u, out.setup.delay);
    ASSERT_EQ(2u, out.setup.players.size());
    EXPECT_EQ("Host", out.setup.players[0].name);
    EXPECT_EQ("ishiman", out.setup.players[0].race);
    EXPECT_EQ(3, out.setup.players[0].hue);
    EXPECT_EQ("Guest", out.setup.players[1].name);
    EXPECT_EQ("cantharan", out.setup.players[1].race);
    EXPECT_EQ(7, out.setup.players[1].hue);
}

TEST_F(PacketTest, Turns) {
    Packet in;
    in.type         = Packet::TURNS;
    in.turns.player = 1;
    in.turns.first  = 100;
    in.turns.turns.push_back({event(0x01, 0), event(0x20, -1)});
    in.turns.turns.push_back({});
    in.turns.turns.push_back({event(0x52, 9)});
    in.turns.ack       = 98;
    in.turns.sync_turn = 90;
    in.turns.sync      = 0xdeadbeef;
    in.turns.lead      = -3;

    Packet out = round_trip(in);
    EXPECT_EQ(1, out.turns.player);
    EXPECT_EQ(100u, out.turns.first);
    ASSERT_EQ(3u, out.turns.turns.size());
    ASSERT_EQ(2u, out.turns.turns[0].size());
    EXPECT_EQ(0x01u, out.turns.turns[0][0].type);
    EXPECT_EQ(0x20u, out.turns.turns[0][1].type);
    EXPECT_EQ(-1, out.turns.turns[0][1].data);
    EXPECT_TRUE(out.turns.turns[1].empty());
    ASSERT_EQ(1u, out.turns.turns[2].size());
    EXPECT_EQ(9, out.turns.turns[2][0].data);
    EXPECT_EQ(98u, out.turns.ack);
    EXPECT_EQ(90u, out.turns.sync_turn);
    EXPECT_EQ(0xdeadbeefu, out.turns.sync);
    EXPECT_EQ(-3, out.turns.lead);
}

TEST_F(PacketTest, Chat) {
    Packet in;
    in.type        = Packet::CHAT;
    in.chat.player = 0;
    in.chat.text   = "gg";
    EXPECT_EQ("gg", round_trip(in).chat.text);
}

TEST_F(PacketTest, Wire) {
    Packet welcome;
    welcome.type           = Packet::WELCOME;
    welcome.welcome.player = 1;
    EXPECT_EQ(bytes({0x12, 0x02, 0x08, 0x01}), encode(welcome));

    Packet none;
    EXPECT_THROW(encode(none), std::runtime_error);
}

TEST_F(PacketTest, SkipsUnknownFields) {
    Packet out;
    // Packet field 15 (varint), then Welcome with field 9 (fixed32) before player = 1.
    ASSERT_TRUE(decode(
            bytes({0x78, 0x05, 0x12, 0x07, 0x4d, 0x01, 0x02, 0x03, 0x04, 0x08, 0x01}), &out));
    EXPECT_EQ(Packet::WELCOME, out.type);
    EXPECT_EQ(1, out.welcome.player);
}

TEST_F(PacketTest, RejectsMalformed) {
    Packet out;
    EXPECT_FALSE(decode(bytes({}), &out));                        // no body
    EXPECT_FALSE(decode(bytes({0x12}), &out));                    // missing length
    EXPECT_FALSE(decode(bytes({0x12, 0x05, 0x08}), &out));        // length past end
    EXPECT_FALSE(decode(bytes({0x10, 0x01}), &out));              // body isn't length-delimited
    EXPECT_FALSE(decode(bytes({0x12, 0x02, 0x08, 0x80}), &out));  // truncated varint
    EXPECT_FALSE(decode(bytes({0x12, 0x01, 0x0f}), &out));        // wrong wire type
    EXPECT_FALSE(decode(
            bytes({0x12, 0x0b, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}),
            &out));  // varint longer than 64 bits

    // Chat text "\xc0\xaf" (overlong '/'), "\xed\xa0\x80" (surrogate), and "\xff".
    EXPECT_FALSE(decode(bytes({0x3a, 0x04, 0x12, 0x02, 0xc0, 0xaf}), &out));
    EXPECT_FALSE(decode(bytes({0x3a, 0x05, 0x12, 0x03, 0xed, 0xa0, 0x80}), &out));
    EXPECT_FALSE(decode(bytes({0x3a, 0x03, 0x12, 0x01, 0xff}), &out));
    EXPECT_FALSE(decode(bytes({0x3a, 0x03, 0x12, 0x01, 0xc3}), &out));  // truncated rune
}

}  // namespace
}  // namespace net
}  // namespace antares
