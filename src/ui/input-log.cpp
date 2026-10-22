// Copyright (C) 2026 The Antares Authors
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

#include "ui/input-log.hpp"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <map>

#include "config/preferences.hpp"
#include "game/sys.hpp"

namespace antares {
namespace input_log {

namespace {

using Clock = std::chrono::steady_clock;

// How long two states may disagree before it counts, since events take a frame or two to arrive.
const auto kGrace = std::chrono::milliseconds(200);

FILE* open_log() {
    const char* path = getenv("ANTARES_INPUT_LOG");
    if (!path || !*path) {
        return nullptr;
    }
    FILE* f = fopen(path, "w");
    if (f) {
        setvbuf(f, nullptr, _IOLBF, 0);
    }
    return f;
}

FILE* log_file() {
    static FILE* f = open_log();
    return f;
}

const Clock::time_point start = Clock::now();

std::map<int, bool> told_down;
std::map<int, bool> held_down;

struct Disagreement {
    bool              active   = false;
    bool              reported = false;
    Clock::time_point since;
};
std::map<std::pair<int, int>, Disagreement> disagreements;  // by key number and kind

const char* const kKinds[] = {"keyboard vs game events", "game events vs ship controls"};

void compare(int num, int kind, bool a, bool b, Key key) {
    Disagreement& d   = disagreements[{num, kind}];
    auto          now = Clock::now();
    if (a == b) {
        if (d.reported) {
            write("resolved: %.*s, %s, after %lld ms", int(key.long_name().size()),
                  key.long_name().data(), kKinds[kind],
                  (long long)std::chrono::duration_cast<std::chrono::milliseconds>(now - d.since)
                          .count());
        }
        d = Disagreement{};
        return;
    }
    if (!d.active) {
        d.active = true;
        d.since  = now;
    } else if (!d.reported && (now - d.since > kGrace)) {
        d.reported = true;
        write("MISMATCH: %.*s, %s: %s vs %s", int(key.long_name().size()), key.long_name().data(),
              kKinds[kind], a ? "down" : "up", b ? "down" : "up");
    }
}

}  // namespace

bool enabled() { return log_file() != nullptr; }

void write(const char* format, ...) {
    FILE* f = log_file();
    if (!f) {
        return;
    }
    fprintf(f, "%9.3f ",
            std::chrono::duration_cast<std::chrono::duration<double>>(Clock::now() - start)
                    .count());
    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
    fputc('\n', f);
}

void told(Key key, bool down) { told_down[key.value()] = down; }
void held(Key key, bool down) { held_down[key.value()] = down; }

void check(int32_t controls, int32_t ignored) {
    if (!enabled()) {
        return;
    }
    static int32_t last_controls = -1;
    if (controls != last_controls) {
        write("ship controls %#x", controls);
        last_controls = controls;
    }
    for (int num = 0; num < 8; ++num) {
        Key  key     = sys.prefs->key(num);
        bool is_told = told_down[key.value()];
        auto is_held = held_down.find(key.value());
        if (is_held != held_down.end()) {
            compare(num, 0, is_held->second, is_told, key);
        }
        if (!(ignored & (1 << num))) {
            compare(num, 1, is_told, controls & (1 << num), key);
        }
    }
}

}  // namespace input_log
}  // namespace antares
