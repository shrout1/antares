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

#include <functional>
#include <pn/string>
#include <vector>

#include "drawing/styled-text.hpp"
#include "net/session.hpp"
#include "ui/editable-text.hpp"
#include "ui/screen.hpp"

namespace antares {

// One line of text typed into an interface box.
class NetTextField : public EditableText {
    using TextReceiver::range;

  public:
    NetTextField(int32_t width, std::function<void()> accept, std::function<void()> escape);

    virtual void accept();
    virtual void escape();

    void draw(const Rect& bounds, bool editing) const;

  protected:
    virtual void              update(pn::string_view text, range<int> selection, range<int> mark);
    virtual StyledText&       styled_text() { return _text; }
    virtual const StyledText& styled_text() const { return _text; }

  private:
    const int32_t         _width;
    std::function<void()> _accept;
    std::function<void()> _escape;
    StyledText            _text;
};

class HostOrJoinScreen : public InterfaceScreen {
  public:
    enum Choice { CANCEL, HOST, JOIN };

    explicit HostOrJoinScreen(Choice* choice);

  private:
    Choice* _choice;
};

class NetAddressScreen : public InterfaceScreen {
  public:
    NetAddressScreen(bool* cancelled, pn::string* address);

    virtual void become_front();
    virtual void resign_front();

  protected:
    virtual void overlay() const;

  private:
    void join();

    bool*        _cancelled;
    pn::string*  _address;
    NetTextField _field;
};

// Shows `status`, or the session's error once it closes.  Pops once the session reaches the
// lobby, or when cancelled.
class NetStatusScreen : public InterfaceScreen {
  public:
    NetStatusScreen(net::Session* session, pn::string_view status);

    virtual bool next_timer(wall_time& time);
    virtual void fire_timer();

  protected:
    virtual void overlay() const;

  private:
    net::Session* _session;
    pn::string    _status;
    wall_time     _next_timer;
};

// Chooses the level, lag, species, and color, and carries chat.  Pops once the host starts the
// game, the session closes, or the player cancels.
class NetLobbyScreen : public InterfaceScreen {
  public:
    explicit NetLobbyScreen(net::Session* session);

    virtual void become_front();
    virtual void resign_front();

    virtual bool next_timer(wall_time& time);
    virtual void fire_timer();

  protected:
    virtual void overlay() const;

  private:
    void change_level(int by);
    void change_delay(int by);
    void change_race(int by);
    void change_hue(int by);
    void choose(pn::string_view race, int hue);
    void choose_defaults();
    void send();
    void update();

    net::Session*           _session;
    bool                    _editing = false;
    NetTextField            _field;
    std::vector<pn::string> _messages;
    int                     _players = 0;
    pn::string              _opponent;
    wall_time               _next_timer;
};

}  // namespace antares

#endif  // ANTARES_UI_SCREENS_NET_SETUP_HPP_
