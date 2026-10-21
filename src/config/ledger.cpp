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

#include "config/ledger.hpp"

#include <algorithm>
#include <cmath>
#include <pn/array>
#include <pn/input>
#include <pn/map>
#include <pn/output>
#include <pn/value>
#include <sfz/sfz.hpp>

#include "config/dirs.hpp"
#include "config/preferences.hpp"
#include "data/plugin.hpp"
#include "game/sys.hpp"
#include "lang/defines.hpp"

using sfz::makedirs;
using sfz::range;
using std::unique_ptr;
using std::vector;

namespace path = sfz::path;

namespace antares {

Ledger::Ledger() {
    if (sys.ledger) {
        throw std::runtime_error("Ledger is a singleton");
    }
    sys.ledger = this;
}

Ledger::~Ledger() { sys.ledger = nullptr; }

NullLedger::NullLedger() : _chapters{1} {}

void NullLedger::unlock_chapter(int chapter) { _chapters.insert(chapter); }

void NullLedger::unlocked_chapters(std::vector<int>* chapters) {
    *chapters = std::vector<int>(_chapters.begin(), _chapters.end());
}

static pn::string registry_path(pn::string_view identifier) {
    return pn::format("{0}/{1}.pn", dirs().registry, identifier);
}

static pn::string saved_progress_dir() { return pn::format("{0}/saved-progress", dirs().root); }

// Reads the unlocked chapters from a registry or saved-progress file, and the plugin identifier
// recorded in it, if any.
static sfz::optional<std::set<int>> read_progress(
        pn::string_view path, pn::string* identifier = nullptr) {
    pn::input in{path, pn::text};
    if (!in) {
        return sfz::nullopt;
    }

    pn::value x;
    if (!pn::parse(in, &x, nullptr)) {
        throw std::runtime_error(pn::format("{0}: bad ledger", path).c_str());
    }

    std::set<int> chapters;
    pn::map_cref  data     = x.as_map();
    pn::map_cref  unlocked = data.get("unlocked").as_map();
    for (pn::value_cref chapter : unlocked.get("chapters").as_array()) {
        if (chapter.is_int()) {
            chapters.insert(chapter.as_int());
        }
    }
    if (identifier) {
        *identifier = data.get("plugin").as_map().get("identifier").as_string().copy();
    }
    return sfz::make_optional(std::move(chapters));
}

static void write_progress(
        pn::string_view path, const std::set<int>& chapters,
        sfz::optional<std::pair<pn::string_view, pn::string_view>> plugin) {
    pn::array unlocked_chapters;
    for (int chapter : chapters) {
        unlocked_chapters.push_back(chapter);
    }

    pn::map data{
            {"unlocked",
             pn::map{
                     {"chapters", std::move(unlocked_chapters)},
             }},
    };
    if (plugin.has_value()) {
        data["plugin"] = pn::map{
                {"title", plugin->first.copy()},
                {"identifier", plugin->second.copy()},
        };
    }

    makedirs(path::dirname(path), 0755);
    pn::output out{path, pn::text};
    out.dump(data);
}

// Finds the saved copy of a plugin's progress: <identifier>.pn if present, or else any file in
// the saved-progress directory that names the plugin's identifier.
static sfz::optional<pn::string> find_saved_progress(pn::string_view identifier) {
    pn::string dir   = saved_progress_dir();
    pn::string exact = pn::format("{0}/{1}.pn", dir, identifier);
    if (path::isfile(exact)) {
        return sfz::make_optional(std::move(exact));
    }
    if (!path::isdir(dir)) {
        return sfz::nullopt;
    }
    std::vector<pn::string> names;
    for (const auto& entry : sfz::scandir(dir)) {
        names.push_back(entry.name.copy());
    }
    std::sort(names.begin(), names.end());
    for (const pn::string& name : names) {
        pn::string path = pn::format("{0}/{1}", dir, name);
        pn::string found;
        try {
            if (path::isfile(path) && read_progress(path, &found).has_value() &&
                (found == identifier)) {
                return sfz::make_optional(std::move(path));
            }
        } catch (std::runtime_error&) {
            // Not a progress file; skip it.
        }
    }
    return sfz::nullopt;
}

DirectoryLedger::DirectoryLedger() { load(); }

void DirectoryLedger::unlock_chapter(int chapter) {
    std::set<int> chapters = load();
    chapters.insert(chapter);
    save(chapters);
}

void DirectoryLedger::unlocked_chapters(std::vector<int>* chapters) {
    std::set<int> set = load();
    *chapters         = std::vector<int>(set.begin(), set.end());
}

std::set<int> DirectoryLedger::load() {
    auto chapters = load_progress(plug.info.identifier.hash);
    if (!chapters.has_value()) {
        return std::set<int>{1};
    }
    return std::move(*chapters);
}

void DirectoryLedger::save(const std::set<int> chapters) {
    write_progress(registry_path(plug.info.identifier.hash), chapters, sfz::nullopt);
}

sfz::optional<std::set<int>> load_progress(pn::string_view identifier) {
    return read_progress(registry_path(identifier));
}

void reset_progress(pn::string_view identifier) {
    pn::string path = registry_path(identifier);
    if (path::exists(path)) {
        sfz::unlink(path);
    }
}

sfz::optional<std::set<int>> load_saved_progress(pn::string_view identifier) {
    auto path = find_saved_progress(identifier);
    if (!path.has_value()) {
        return sfz::nullopt;
    }
    return read_progress(*path);
}

pn::string save_progress(pn::string_view identifier, pn::string_view title) {
    auto chapters = load_progress(identifier);
    if (!chapters.has_value()) {
        chapters.emplace(std::set<int>{1});
    }
    pn::string path = pn::format("{0}/{1}.pn", saved_progress_dir(), identifier);
    write_progress(path, *chapters, sfz::make_optional(std::make_pair(title, identifier)));
    return path;
}

bool import_progress(pn::string_view identifier) {
    auto chapters = load_saved_progress(identifier);
    if (!chapters.has_value()) {
        return false;
    }
    write_progress(registry_path(identifier), *chapters, sfz::nullopt);
    return true;
}

}  // namespace antares
