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

#ifndef ANTARES_UI_INPUT_LOG_HPP_
#define ANTARES_UI_INPUT_LOG_HPP_

#include <stdint.h>

#include "config/keys.hpp"

namespace antares {
namespace input_log {

// Debugging aid: when ANTARES_INPUT_LOG names a file, key events, focus changes and the ship's
// controls are written to it, along with a MISMATCH line whenever the keys held on the keyboard,
// the keys the game was told about and the ship's controls disagree for longer than a moment.

bool enabled();
#ifdef __GNUC__
void write(const char* format, ...) __attribute__((format(printf, 1, 2)));
#else
void write(const char* format, ...);
#endif

// What the game was told by key events, and what the keyboard reports, for the pilot keys.
void told(Key key, bool down);
void held(Key key, bool down);

// Compares the three for the pilot keys (key numbers 0 to 7, which are also the bits of
// `controls`). Controls in `ignored` aren't expected to follow the keys.
void check(int32_t controls, int32_t ignored);

}  // namespace input_log
}  // namespace antares

#endif  // ANTARES_UI_INPUT_LOG_HPP_
