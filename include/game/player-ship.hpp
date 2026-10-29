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

#ifndef ANTARES_GAME_PLAYER_SHIP_HPP_
#define ANTARES_GAME_PLAYER_SHIP_HPP_

#include <vector>

#include "config/keys.hpp"
#include "data/base-object.hpp"
#include "game/cursor.hpp"
#include "game/player-event.hpp"
#include "ui/editable-text.hpp"
#include "ui/event.hpp"

namespace antares {

class GameCursor;
class InputSource;

class PlayerShip : public EventReceiver {
  public:
    PlayerShip();

    virtual void key_down(const KeyDownEvent& event);
    virtual void key_up(const KeyUpEvent& event);

    virtual void mouse_down(const MouseDownEvent& event);
    virtual void mouse_up(const MouseUpEvent& event);
    virtual void mouse_move(const MouseMoveEvent& event);

    virtual void gamepad_button_down(const GamepadButtonDownEvent& event);
    virtual void gamepad_button_up(const GamepadButtonUpEvent& event);
    virtual void gamepad_stick(const GamepadStickEvent& event);

    // Translates the input received since the last call into the local admiral's commands.
    std::vector<PlayerEvent> sample();
    // Executes one turn of an admiral's commands.
    void execute(Handle<Admiral> adm, const std::vector<PlayerEvent>& commands);

    // Messages entered in a net game since the last call.
    std::vector<pn::string> take_messages();

    bool    show_select() const;
    bool    show_target() const;
    int32_t control_direction() const;

    GameCursor&       cursor() { return _cursor; }
    const GameCursor& cursor() const { return _cursor; }

    bool entering_message() const { return _message.editing(); }

  private:
    bool active() const;
    void click(Point where, int button);
    void pick(PlayerEventType type, int32_t direction);

    uint32_t                 _gamepad_keys;
    uint32_t                 _sent_gamepad_keys;
    int32_t                  _sent_steer;
    std::vector<PlayerEvent> _player_events;

    enum GamepadState {
        NO_BUMPER              = 0,
        SELECT_BUMPER          = 1,
        TARGET_BUMPER          = 2,
        EITHER_BUMPER          = SELECT_BUMPER | TARGET_BUMPER,
        OVERRIDE               = 4,
        SELECT_BUMPER_OVERRIDE = SELECT_BUMPER | OVERRIDE,
        TARGET_BUMPER_OVERRIDE = TARGET_BUMPER | OVERRIDE,
    };
    GamepadState _gamepad_state;
    bool         _control_active;
    int32_t      _control_direction;
    GameCursor   _cursor;

    class MessageText : public EditableText {
      public:
        using TextReceiver::range;

        MessageText();

        void start_editing();
        void stop_editing();
        bool editing() const { return _editing; }

        virtual void accept();
        virtual void escape();

        std::vector<pn::string> sent;

      protected:
        virtual void        update(pn::string_view text, range<int> selection, range<int> mark);
        virtual StyledText& styled_text();
        virtual const StyledText& styled_text() const;

      private:
        bool _editing = false;
    };
    MessageText _message;
};

void ResetPlayerShip();
void ChangePlayerShipNumber(Handle<Admiral> whichAdmiral, Handle<SpaceObject> newShip);
void TogglePlayerAutoPilot(Handle<SpaceObject> theShip);
void PlayerShipGiveCommand(Handle<Admiral> whichAdmiral);
void PlayerShipBodyExpire(Handle<SpaceObject> theShip);

}  // namespace antares

#endif  // ANTARES_GAME_PLAYER_SHIP_HPP_
