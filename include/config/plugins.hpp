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

#ifndef ANTARES_CONFIG_PLUGINS_HPP_
#define ANTARES_CONFIG_PLUGINS_HPP_

#include <map>
#include <pn/string>
#include <sfz/sfz.hpp>
#include <vector>

namespace antares {

// A plugin found on disk, summarized without loading it.
struct PluginSummary {
    sfz::optional<pn::string> path;  // none for the factory scenario.
    pn::string                title;
    pn::string                author;
    pn::string                version;
    pn::string                identifier;
    std::map<int, pn::string> chapters;  // solo chapters, by number, with their names.
    int                       net_levels = 0;
    pn::string                error;  // why the plugin couldn't be read, if it couldn't.
};

// Plugins are looked for in dirs().scenarios and in each directory added here, recursively, as
// .antaresplugin files or as directories containing info.pn. The factory scenario comes first.
void                       add_plugin_directory(pn::string_view dir);
std::vector<PluginSummary> list_plugins();
bool                       is_loaded(const PluginSummary& p);  // the plugin being played now?

// Switching plugins restarts the game. Front-ends that can do that call enable_plugin_switching()
// at startup, and after the main loop finishes, check plugin_requested() and restart with
// requested_plugin() (none meaning the factory scenario).
void                             enable_plugin_switching();
bool                             plugin_switching_enabled();
void                             request_plugin(sfz::optional<pn::string_view> path);
bool                             plugin_requested();
const sfz::optional<pn::string>& requested_plugin();

}  // namespace antares

#endif  // ANTARES_CONFIG_PLUGINS_HPP_
