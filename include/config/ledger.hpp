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

#ifndef ANTARES_CONFIG_LEDGER_HPP_
#define ANTARES_CONFIG_LEDGER_HPP_

#include <pn/string>
#include <set>
#include <sfz/sfz.hpp>
#include <vector>

namespace antares {

class Ledger {
  public:
    Ledger();
    virtual ~Ledger();

    virtual void unlock_chapter(int chapter)                   = 0;
    virtual void unlocked_chapters(std::vector<int>* chapters) = 0;
};

class NullLedger : public Ledger {
  public:
    NullLedger();
    NullLedger(const NullLedger&) = delete;
    NullLedger& operator=(const NullLedger&) = delete;

    virtual void unlock_chapter(int chapter);
    virtual void unlocked_chapters(std::vector<int>* chapters);

  private:
    std::set<int> _chapters;
};

class DirectoryLedger : public Ledger {
  public:
    DirectoryLedger();
    DirectoryLedger(const DirectoryLedger&) = delete;
    DirectoryLedger& operator=(const DirectoryLedger&) = delete;

    virtual void unlock_chapter(int chapter);
    virtual void unlocked_chapters(std::vector<int>* chapters);

  private:
    class Visitor;

    std::set<int> load();
    void          save(const std::set<int> chapters);
};

// Progress for any plugin, by identifier, as stored by DirectoryLedger.
//
// load_progress() returns the unlocked chapters, or nothing if the plugin has never been played.
// Copies of progress are saved and imported in the launcher.
sfz::optional<std::set<int>> load_progress(pn::string_view identifier);
void                         reset_progress(pn::string_view identifier);

}  // namespace antares

#endif  // ANTARES_CONFIG_LEDGER_HPP_
