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

#ifndef ANTARES_GAME_PLAYER_EVENT_HPP_
#define ANTARES_GAME_PLAYER_EVENT_HPP_

namespace antares {

enum class PlayerEventType {
    ACCEL_ON   = 0x00,
    ACCEL_OFF  = 0x01,
    DECEL_ON   = 0x02,
    DECEL_OFF  = 0x03,
    CCW_ON     = 0x04,
    CCW_OFF    = 0x05,
    CW_ON      = 0x06,
    CW_OFF     = 0x07,
    FIRE_1_ON  = 0x08,
    FIRE_1_OFF = 0x09,
    FIRE_2_ON  = 0x0a,
    FIRE_2_OFF = 0x0b,
    FIRE_S_ON  = 0x0c,
    FIRE_S_OFF = 0x0d,
    WARP_ON    = 0x0e,
    WARP_OFF   = 0x0f,

    ZOOM_IN  = 0x010,
    ZOOM_OUT = 0x011,
    ZOOM_1X  = 0x012,
    ZOOM_2X  = 0x013,
    ZOOM_4X  = 0x014,
    ZOOM_16X = 0x015,
    ZOOM_FOE = 0x016,
    ZOOM_OBJ = 0x017,
    ZOOM_ALL = 0x018,

    SELECT_FRIEND = 0x020,
    TARGET_FRIEND = 0x021,
    TARGET_FOE    = 0x022,
    SELECT_BASE   = 0x023,
    TARGET_BASE   = 0x024,
    TARGET_SELF   = 0x025,

    AUTOPILOT     = 0x030,
    ORDER         = 0x031,
    TRANSFER      = 0x032,
    MINI_TRANSFER = 0x033,
    MINI_HOLD     = 0x034,
    MINI_COME     = 0x035,
    MINI_FIRE_1   = 0x036,
    MINI_FIRE_2   = 0x037,
    MINI_FIRE_S   = 0x038,

    NEXT_PAGE         = 0x040,
    MINI_NEXT_PAGE    = 0x041,
    MINI_PREV_PAGE    = 0x042,
    MINI_LAST_MESSAGE = 0x043,

    MINI_BUILD    = 0x050,
    HOTKEY_SET    = 0x051,
    HOTKEY_SELECT = 0x052,
    HOTKEY_TARGET = 0x053,

    SELECT_OBJECT = 0x060,
    TARGET_OBJECT = 0x061,
    GAMEPAD_KEYS  = 0x062,
    STEER         = 0x063,
    CLEAR         = 0x064,
};

struct PlayerEvent {
    PlayerEventType type;
    int             data;

    bool operator==(PlayerEvent other) const;
    bool operator!=(PlayerEvent other) const { return !(*this == other); }
    bool operator<(PlayerEvent other) const;
    bool operator<=(PlayerEvent other) const { return !(other < *this); }
    bool operator>(PlayerEvent other) const { return (other < *this); }
    bool operator>=(PlayerEvent other) const { return !(*this < other); }
};

}  // namespace antares

#endif  // ANTARES_GAME_PLAYER_EVENT_HPP_
