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

#include <gmock/gmock.h>
#include <stdexcept>

namespace antares {
namespace net {
namespace {

using RollbackTest = testing::Test;

Rollback::Turn turn(uint32_t type, int32_t data) {
    Turns::Event e;
    e.type = type;
    e.data = data;
    return {e};
}

// Each player's input for turn `at`, which must be confirmed, then forgets it.
std::vector<Rollback::Turn> take(Rollback* r, uint32_t at) {
    std::vector<Rollback::Turn> turns;
    for (int i = 0; i < 2; ++i) {
        turns.push_back(*r->known(i, at));
    }
    r->confirm(at);
    return turns;
}

void exchange(Rollback* a, Rollback* b) {
    a->receive(b->outgoing(a->local()));
    b->receive(a->outgoing(b->local()));
}

TEST_F(RollbackTest, Delay) {
    Rollback a(2, 0, 10, 3);
    EXPECT_TRUE((10 <= a.confirmed()));
    EXPECT_TRUE((12 <= a.confirmed()));
    EXPECT_FALSE((13 <= a.confirmed()));
    EXPECT_EQ(2u, take(&a, 10).size());
    EXPECT_THROW(a.confirm(12), std::runtime_error);
    EXPECT_THROW(a.submit(11, {}), std::runtime_error);
}

TEST_F(RollbackTest, Play) {
    Rollback a(2, 0, 1, 2), b(2, 1, 1, 2);
    for (uint32_t t = 1; t <= 20; ++t) {
        a.submit(t, turn(1, t));
        b.submit(t, (t % 3) ? Rollback::Turn{} : turn(2, -t));
        exchange(&a, &b);
        ASSERT_TRUE((t <= a.confirmed()));
        ASSERT_TRUE((t <= b.confirmed()));
        std::vector<Rollback::Turn> x = take(&a, t), y = take(&b, t);
        ASSERT_EQ(2u, x.size());
        ASSERT_EQ(x[0].size(), y[0].size());
        ASSERT_EQ(x[1].size(), y[1].size());
        if (t > 2) {
            ASSERT_EQ(1u, x[0].size());
            EXPECT_EQ(static_cast<int32_t>(t - 2), x[0][0].data);
            EXPECT_EQ(x[0][0].data, y[0][0].data);
            EXPECT_EQ(((t - 2) % 3) ? 0u : 1u, y[1].size());
        }
    }
}

TEST_F(RollbackTest, WaitsForLostTurns) {
    Rollback a(2, 0, 1, 1), b(2, 1, 1, 1);
    a.submit(1, turn(1, 1));
    b.submit(1, {});
    b.receive(a.outgoing(1));  // a never hears from b.
    EXPECT_FALSE((2 <= a.confirmed()));
    EXPECT_TRUE((2 <= b.confirmed()));

    take(&a, 1);
    take(&b, 1);
    take(&b, 2);
    b.submit(2, {});
    a.submit(2, turn(1, 2));
    EXPECT_FALSE((2 <= a.confirmed()));

    Turns resent = b.outgoing(0);
    EXPECT_EQ(2u, resent.first);
    EXPECT_EQ(2u, resent.turns.size());
    a.receive(resent);
    EXPECT_TRUE((3 <= a.confirmed()));
}

TEST_F(RollbackTest, AcknowledgedTurnsAreNotResent) {
    Rollback a(2, 0, 1, 1), b(2, 1, 1, 1);
    for (uint32_t t = 1; t <= 5; ++t) {
        a.submit(t, {});
        b.submit(t, {});
    }
    EXPECT_EQ(5u, a.outgoing(1).turns.size());
    exchange(&a, &b);
    exchange(&a, &b);
    EXPECT_EQ(0u, a.outgoing(1).turns.size());
    EXPECT_EQ(7u, a.outgoing(1).first);
}

TEST_F(RollbackTest, Desync) {
    Rollback a(2, 0, 1, 1), b(2, 1, 1, 1);
    a.sync(1, 100);
    b.sync(1, 100);
    exchange(&a, &b);
    EXPECT_FALSE(a.desynced());
    EXPECT_FALSE(b.desynced());

    a.sync(2, 101);
    exchange(&a, &b);  // b hasn't executed turn 2 yet.
    b.sync(2, 999);
    EXPECT_TRUE(b.desynced());
    exchange(&a, &b);
    EXPECT_TRUE(a.desynced());
}

TEST_F(RollbackTest, KnownBeforeConfirmed) {
    Rollback a(2, 0, 1, 1), b(2, 1, 1, 1);
    a.submit(1, turn(1, 1));
    a.submit(2, turn(1, 2));
    EXPECT_EQ(1u, a.confirmed());
    ASSERT_NE(nullptr, a.known(0, 3));
    EXPECT_EQ(2, (*a.known(0, 3))[0].data);
    EXPECT_EQ(nullptr, a.known(1, 2));  // Not heard from b yet.
    EXPECT_EQ(nullptr, a.known(0, 4));

    b.submit(1, turn(2, 1));
    a.receive(b.outgoing(0));
    EXPECT_EQ(2u, a.confirmed());
    ASSERT_NE(nullptr, a.known(1, 2));
    EXPECT_EQ(1, (*a.known(1, 2))[0].data);

    EXPECT_THROW(a.confirm(2), std::runtime_error);  // Turn 1 comes first.
    a.confirm(1);
    a.confirm(2);
    EXPECT_EQ(nullptr, a.known(0, 2));
    EXPECT_EQ(nullptr, a.known(1, 2));
    ASSERT_NE(nullptr, a.known(0, 3));
    EXPECT_THROW(a.confirm(3), std::runtime_error);
    EXPECT_THROW(a.confirm(2), std::runtime_error);
}

TEST_F(RollbackTest, NothingKnownAfterAGap) {
    Rollback a(2, 0, 1, 1);
    Turns    t;
    t.player = 1;
    t.first  = 3;
    t.turns.push_back(turn(1, 3));
    a.receive(t);  // Turn 2 was lost.
    EXPECT_EQ(nullptr, a.known(1, 3));
    t.first = 2;
    t.turns = {turn(1, 2), turn(1, 3)};
    a.receive(t);
    ASSERT_NE(nullptr, a.known(1, 3));
    EXPECT_EQ(1u, a.confirmed());  // a hasn't submitted turn 2.
}

TEST_F(RollbackTest, IgnoresInvalidPlayers) {
    Rollback a(2, 0, 1, 1);
    Turns    t;
    t.player = 5;
    t.first  = 2;
    t.turns.push_back(turn(1, 1));
    a.receive(t);
    t.player = 0;
    a.receive(t);
    EXPECT_FALSE((2 <= a.confirmed()));
    EXPECT_THROW(a.outgoing(0), std::runtime_error);
}

}  // namespace
}  // namespace net
}  // namespace antares
