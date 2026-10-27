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

#ifndef ANTARES_UI_SCREENS_NET_SETUP_HPP_
#define ANTARES_UI_SCREENS_NET_SETUP_HPP_

#include <memory>
#include <pn/string>

#include "drawing/styled-text.hpp"
#include "net/session.hpp"
#include "ui/card.hpp"
#include "ui/editable-text.hpp"

namespace antares {

// Hosts or joins a net game, and sets it up.  Pops once the host starts the game, leaving the
// session in `session`, or when the player leaves, leaving it null.
class NetSetupScreen : public Card {
  public:
    enum Mode { ASK, HOST, JOIN };

    NetSetupScreen(
            Mode mode, pn::string_view address, uint16_t port,
            std::unique_ptr<net::Session>* session);

    virtual void become_front();
    virtual void resign_front();
    virtual void draw() const;

    virtual bool next_timer(wall_time& time);
    virtual void fire_timer();

    virtual void key_down(const KeyDownEvent& event);

  private:
    enum State { ASKING, ADDRESS, CONNECTING, LOBBY, FAILED };
    enum Field { LEVEL, RACE, COLOR };

    class AddressText : public EditableText {
        using TextReceiver::range;

      public:
        explicit AddressText(NetSetupScreen* screen);

        virtual void accept();
        virtual void escape();

        void              draw(Point at) const;
        const StyledText& text_box() const { return _text; }

      protected:
        virtual void        update(pn::string_view text, range<int> selection, range<int> mark);
        virtual StyledText& styled_text() { return _text; }
        virtual const StyledText& styled_text() const { return _text; }

      private:
        NetSetupScreen* _screen;
        StyledText      _text;
    };

    void host();
    void join(pn::string_view address);
    void fail(pn::string_view error);
    void edit_address();
    void change(int by);
    void choose_defaults();

    const Mode                     _mode;
    const uint16_t                 _port;
    pn::string                     _address;
    std::unique_ptr<net::Session>* _session;
    State                          _state = ASKING;
    Field                          _field = RACE;
    bool                           _chose = false;
    pn::string                     _error;
    AddressText                    _address_text;
    wall_time                      _next_timer;
};

}  // namespace antares

#endif  // ANTARES_UI_SCREENS_NET_SETUP_HPP_
