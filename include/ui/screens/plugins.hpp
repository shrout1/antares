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

#ifndef ANTARES_UI_SCREENS_PLUGINS_HPP_
#define ANTARES_UI_SCREENS_PLUGINS_HPP_

#include <set>
#include <vector>

#include "config/plugins.hpp"
#include "ui/screen.hpp"

namespace antares {

// Lists the installed plugins with each one's progress. Play switches to the selected plugin
// (setting *switched, after which the caller should quit so the game can restart with it); the
// other buttons save, import, or reset the selected plugin's progress.
class PluginsScreen : public InterfaceScreen {
  public:
    PluginsScreen(bool* switched);
    ~PluginsScreen();

    virtual void mouse_down(const MouseDownEvent& event);
    virtual void key_down(const KeyDownEvent& event);
    virtual void gamepad_button_down(const GamepadButtonDownEvent& event);

  protected:
    virtual void overlay() const;

  private:
    enum Confirm {
        NONE,
        IMPORT,
        RESET,
    };

    struct Entry {
        PluginSummary                summary;
        pn::string                   name;  // title, plus the file name if the title isn't unique.
        bool                         current;
        sfz::optional<std::set<int>> progress;
        sfz::optional<std::set<int>> saved;
    };

    const Entry& selected() const { return _entries[_index]; }
    bool         playable(const Entry& e) const;
    void         select(int index);
    void         refresh_progress(Entry* e);
    void         play();
    void         confirm(Confirm action, pn::string message);
    int          visible_rows() const;
    Rect         list_bounds() const;

    void draw_list() const;
    void draw_details() const;

    bool* const        _switched;
    std::vector<Entry> _entries;
    int                _index   = 0;
    int                _top     = 0;
    Confirm            _confirm = NONE;
    pn::string         _status;
};

}  // namespace antares

#endif  // ANTARES_UI_SCREENS_PLUGINS_HPP_
