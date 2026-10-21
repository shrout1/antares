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

#include "config/plugins.hpp"

#include <algorithm>
#include <pn/input>
#include <pn/map>
#include <pn/value>
#include <set>
#include <zipxx/zipxx.hpp>

#include "config/dirs.hpp"
#include "data/field.hpp"
#include "data/info.hpp"
#include "data/plugin.hpp"

namespace path = sfz::path;

namespace antares {

namespace {

std::vector<pn::string>   plugin_dirs;
bool                      switching_enabled = false;
bool                      switch_requested  = false;
sfz::optional<pn::string> switch_path;

// Reads the procyon files of one plugin, whether it's a directory or a zip archive.
class PluginFiles {
  public:
    PluginFiles(pn::string_view path) {
        if (path::isdir(path)) {
            _dir.emplace(path.copy());
        } else {
            _zip.reset(new zipxx::ZipArchive(path, 0));
        }
    }

    sfz::optional<pn::value> read(pn::string_view name) const {
        pn::value  x;
        pn_error_t e;
        if (_dir.has_value()) {
            pn::input in{pn::format("{0}/{1}", *_dir, name), pn::text};
            if (!in) {
                return sfz::nullopt;
            }
            if (!pn::parse(in, &x, &e)) {
                throw std::runtime_error(
                        pn::format("{0}:{1}: {2}", name, e.lineno, pn_strerror(e.code)).c_str());
            }
        } else {
            auto index = _zip->locate(name);
            if (index < 0) {
                return sfz::nullopt;
            }
            zipxx::ZipFileReader file(*_zip, index);
            if (!pn::parse(file.string().input(), &x, &e)) {
                throw std::runtime_error(
                        pn::format("{0}:{1}: {2}", name, e.lineno, pn_strerror(e.code)).c_str());
            }
        }
        return sfz::make_optional(std::move(x));
    }

    std::vector<pn::string> levels() const {
        std::vector<pn::string> names;
        if (_dir.has_value()) {
            pn::string dir = pn::format("{0}/levels", *_dir);
            if (path::isdir(dir)) {
                for (const auto& entry : sfz::scandir(dir)) {
                    if (is_level(entry.name)) {
                        names.push_back(pn::format("levels/{0}", entry.name));
                    }
                }
            }
        } else {
            for (auto i : sfz::range(_zip->size())) {
                pn::string_view name = _zip->name(i);
                if ((name.find("levels/") == 0) && is_level(name)) {
                    names.push_back(name.copy());
                }
            }
        }
        return names;
    }

  private:
    static bool is_level(pn::string_view name) {
        return (name.size() > 3) && (name.substr(name.size() - 3) == ".pn");
    }

    sfz::optional<pn::string>          _dir;
    std::unique_ptr<zipxx::ZipArchive> _zip;
};

// Level titles are styled text ("\i Chapter 1 \iEasy Street"); keep only the words.
pn::string plain_title(pn::string_view title) {
    pn::string result;
    bool       space  = false;
    bool       escape = false;
    int        skip   = 0;
    for (pn::rune r : title) {
        if (skip) {
            --skip;
            continue;
        } else if (escape) {
            // \i, \r and \t take no argument; \f and \b take two hex digits; \\ is a backslash.
            escape = false;
            if ((r == pn::rune{'f'}) || (r == pn::rune{'b'})) {
                skip = 2;
            }
            if (r != pn::rune{'\\'}) {
                continue;
            }
        } else if (r == pn::rune{'\\'}) {
            escape = true;
            continue;
        } else if ((r == pn::rune{' '}) || (r == pn::rune{'\n'}) || (r == pn::rune{'\t'})) {
            space = !result.empty();
            continue;
        }
        if (space) {
            result += pn::rune{' '};
            space = false;
        }
        result += r;
    }
    return result;
}

PluginSummary summarize(sfz::optional<pn::string_view> plugin_path, pn::string_view files_path) {
    PluginSummary p;
    if (plugin_path.has_value()) {
        p.path.emplace(plugin_path->copy());
        p.title = path::basename(*plugin_path).copy();
    }
    try {
        PluginFiles              files{files_path};
        sfz::optional<pn::value> x = files.read("info.pn");
        if (!x.has_value()) {
            p.error = "not an antares plugin";
            return p;
        }
        Info info    = antares::info(path_value{*x});
        p.title      = info.title.copy();
        p.author     = info.author.copy();
        p.version    = info.version.copy();
        p.identifier = info.identifier.hash.copy();

        for (const pn::string& name : files.levels()) {
            sfz::optional<pn::value> level = files.read(name);
            if (!level.has_value()) {
                continue;
            }
            pn::map_cref    l    = level->as_map();
            pn::string_view type = l.get("type").as_string();
            if ((type == "solo") && l.get("chapter").is_int()) {
                p.chapters[l.get("chapter").as_int()] = plain_title(l.get("title").as_string());
            } else if (type == "net") {
                ++p.net_levels;
            }
        }
    } catch (std::exception& e) {
        p.error = e.what();
    }
    return p;
}

class PluginFinder : public sfz::TreeWalker {
  public:
    PluginFinder(std::set<pn::string>* paths) : _paths(paths) {}

    void file(pn::string_view name, const sfz::Stat& st) const override {
        static const pn::string_view extension = ".antaresplugin";
        if ((name.size() > extension.size()) &&
            (name.substr(name.size() - extension.size()) == extension)) {
            _paths->insert(name.copy());
        } else if (path::basename(name) == "info.pn") {
            _paths->insert(path::dirname(name).copy());
        }
    }

    void pre_directory(pn::string_view name, const sfz::Stat& st) const override {}
    void cycle_directory(pn::string_view name, const sfz::Stat& st) const override {}
    void post_directory(pn::string_view name, const sfz::Stat& st) const override {}
    void symlink(pn::string_view name, const sfz::Stat& st) const override {}
    void broken_symlink(pn::string_view name, const sfz::Stat& st) const override {}
    void other(pn::string_view name, const sfz::Stat& st) const override {}

  private:
    std::set<pn::string>* _paths;
};

}  // namespace

void add_plugin_directory(pn::string_view dir) { plugin_dirs.push_back(dir.copy()); }

std::vector<PluginSummary> list_plugins() {
    std::vector<PluginSummary> plugins;
    // The factory scenario's info.pn and levels are part of the application data.
    plugins.push_back(summarize(sfz::nullopt, application_path()));

    std::set<pn::string>         paths;
    std::vector<pn::string_view> roots{dirs().scenarios};
    for (const pn::string& dir : plugin_dirs) {
        roots.push_back(dir);
    }
    for (pn::string_view root : roots) {
        if (path::isdir(root)) {
            sfz::walk(root, sfz::WALK_LOGICAL, PluginFinder{&paths});
        }
    }
    for (const pn::string& path : paths) {
        if (path == factory_scenario_path()) {
            continue;
        }
        plugins.push_back(summarize(sfz::make_optional<pn::string_view>(path), path));
    }
    return plugins;
}

bool is_loaded(const PluginSummary& p) {
    if (!p.path.has_value()) {
        return !plug.dir.has_value() && !plug.zip;
    }
    pn::string_view current;
    if (plug.dir.has_value()) {
        current = *plug.dir;
    } else if (plug.zip) {
        current = plug.zip->path();
    } else {
        return false;
    }
    // Paths may be spelled differently (relative, through a symlink); compare file names too.
    return (p.identifier == plug.info.identifier.hash) &&
           ((*p.path == current) || (path::basename(*p.path) == path::basename(current)));
}

void enable_plugin_switching() { switching_enabled = true; }
bool plugin_switching_enabled() { return switching_enabled; }

void request_plugin(sfz::optional<pn::string_view> path) {
    switch_requested = true;
    switch_path      = path.has_value() ? sfz::make_optional(path->copy()) : sfz::nullopt;
}

bool                             plugin_requested() { return switch_requested; }
const sfz::optional<pn::string>& requested_plugin() { return switch_path; }

}  // namespace antares
